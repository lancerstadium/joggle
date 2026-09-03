#include "joggle/ir_module.h"

#include "ir_internal.h"
#include "type_internal.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace joggle::ir {

struct Module::Storage {
  std::map<std::string, std::shared_ptr<Function>, std::less<>> functions;
};

namespace {

bool valid_name(std::string_view name) {
  if (name.empty() ||
      (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
       name.front() != '_')) {
    return false;
  }
  return std::all_of(name.begin() + 1, name.end(), [](char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == '_';
  });
}

using DependencyMap = std::map<std::string, Version, std::less<>>;

void collect(DependencyMap& dependencies,
             const joggle::Module::Symbol& symbol) {
  const std::string name(symbol.module_name());
  const auto [found, inserted] =
      dependencies.emplace(name, symbol.module_version());
  if (!inserted && found->second != symbol.module_version()) {
    throw std::invalid_argument("IR Module references multiple versions of '" +
                                name + "'");
  }
}

void collect(DependencyMap& dependencies,
             const detail::ParameterValue& value);

void collect(DependencyMap& dependencies, const Type& type) {
  collect(dependencies, type.schema().symbol());
  for (const detail::ParameterValue& parameter :
       detail::TypeAccess::parameters(type)) {
    collect(dependencies, parameter);
  }
}

void collect(DependencyMap& dependencies, const Attribute& attribute) {
  collect(dependencies, attribute.schema().symbol());
  for (const detail::ParameterValue& parameter :
       detail::TypeAccess::parameters(attribute)) {
    collect(dependencies, parameter);
  }
}

void collect(DependencyMap& dependencies,
             const detail::ParameterValue& value) {
  if (const Type* type = value.as_type()) {
    collect(dependencies, *type);
  } else if (const Attribute* attribute = value.as_attribute()) {
    collect(dependencies, *attribute);
  } else if (value.kind() == detail::ParameterValue::Kind::List) {
    for (const detail::ParameterValue& element : value.elements()) {
      collect(dependencies, element);
    }
  }
}

void collect(DependencyMap& dependencies, const Value& value) {
  collect(dependencies, value.type());
  if (const auto known = detail::FunctionAccess::known_value(value)) {
    collect(dependencies, *known);
  }
}

void collect(DependencyMap& dependencies, const Function& function) {
  for (const Value& argument : function.arguments()) {
    collect(dependencies, argument);
  }
  for (const Type& result : function.result_types()) {
    collect(dependencies, result);
  }
  for (const Block& block : function.blocks()) {
    for (const Value& argument : block.arguments()) {
      collect(dependencies, argument);
    }
    for (const Instruction& instruction : block.instructions()) {
      collect(dependencies, instruction.callee().symbol());
      for (const Value& argument : instruction.arguments()) {
        collect(dependencies, argument);
      }
      for (const Value& result : instruction.results()) {
        collect(dependencies, result);
      }
    }
    const Terminator terminator = block.terminator();
    if (const auto condition = terminator.condition()) {
      collect(dependencies, *condition);
    }
    for (const Value& returned : terminator.returned()) {
      collect(dependencies, returned);
    }
    for (std::size_t index = 0; index < terminator.successor_count(); ++index) {
      for (const Value& argument : terminator.arguments(index)) {
        collect(dependencies, argument);
      }
    }
  }
}

void write_indented(std::ostringstream& output, std::string_view source) {
  std::size_t begin = 0;
  while (begin < source.size()) {
    const std::size_t end = source.find('\n', begin);
    const std::string_view line = source.substr(
        begin, end == std::string_view::npos ? source.size() - begin
                                             : end - begin);
    if (line.find_first_not_of(" \t\r") != std::string_view::npos) {
      output << "  " << line;
    }
    output << '\n';
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1U;
  }
}

}  // namespace

Module::Module() : storage_(std::make_unique<Storage>()) {}

Module::Module(const Module& other)
    : storage_(std::make_unique<Storage>(*other.storage_)) {}

Module& Module::operator=(const Module& other) {
  if (this != &other) {
    Module copy(other);
    storage_.swap(copy.storage_);
  }
  return *this;
}

Module::Module(Module&&) noexcept = default;
Module& Module::operator=(Module&&) noexcept = default;
Module::~Module() = default;

bool Module::insert(std::string name, Function function,
                    Diagnostics& diagnostics) {
  if (!valid_name(name)) {
    diagnostics.report("IR Module function name '" + name + "' is invalid");
    return false;
  }
  if (storage_->functions.contains(name)) {
    diagnostics.report("IR Module already contains function '" + name + "'");
    return false;
  }
  storage_->functions.emplace(
      std::move(name), std::make_shared<Function>(std::move(function)));
  return true;
}

Function* Module::function(std::string_view name) {
  const auto found = storage_->functions.find(name);
  if (found == storage_->functions.end()) {
    return nullptr;
  }
  if (found->second.use_count() != 1) {
    found->second = std::make_shared<Function>(found->second->clone());
  }
  return found->second.get();
}

const Function* Module::function(std::string_view name) const {
  const auto found = storage_->functions.find(name);
  return found == storage_->functions.end() ? nullptr : found->second.get();
}

std::vector<std::string> Module::function_names() const {
  std::vector<std::string> result;
  result.reserve(storage_->functions.size());
  for (const auto& [name, unused] : storage_->functions) {
    static_cast<void>(unused);
    result.push_back(name);
  }
  return result;
}

std::size_t Module::size() const { return storage_->functions.size(); }

bool Module::empty() const { return storage_->functions.empty(); }

std::vector<Dependency> dependencies(const Module& module) {
  DependencyMap found;
  for (const std::string& name : module.function_names()) {
    const Function* function =
        static_cast<const Module&>(module).function(name);
    if (function != nullptr) {
      collect(found, *function);
    }
  }
  std::vector<Dependency> result;
  result.reserve(found.size());
  for (const auto& [name, version] : found) {
    result.push_back({name, version});
  }
  return result;
}

static std::string format_source(const Module& module, std::string_view name,
                                 Version version) {
  if (!valid_name(name)) {
    throw std::invalid_argument(
        "a formatted IR Module needs a valid package name");
  }
  const auto referenced = dependencies(module);
  std::ostringstream output;
  output << "joggle 1;\n\nmodule " << name << '@' << to_string(version)
         << " {\n";
  bool imported = false;
  for (const Dependency& dependency : referenced) {
    if (dependency.name == "prelude" || dependency.name == name) {
      continue;
    }
    output << "  import " << dependency.name << '@'
           << to_string(dependency.version) << ";\n";
    imported = true;
  }
  if (imported && !module.empty()) {
    output << '\n';
  }
  for (const std::string& function_name : module.function_names()) {
    const Function* function =
        static_cast<const Module&>(module).function(function_name);
    if (function != nullptr) {
      write_indented(output, joggle::format(*function, function_name));
    }
  }
  output << "}\n";
  return output.str();
}

}  // namespace joggle::ir

namespace joggle {

std::string format(const ir::Module& module, std::string_view name,
                   Version version) {
  return ir::format_source(module, name, version);
}

}  // namespace joggle
