#include "joggle/module.h"

#include "ir_internal.h"
#include "module_storage.h"
#include "prelude.h"
#include "sha256.h"
#include "type_internal.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <utility>

namespace joggle {
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

void collect(DependencyMap& dependencies, const Module::Symbol& symbol) {
  const std::string name(symbol.module_name());
  const auto [found, inserted] =
      dependencies.emplace(name, symbol.module_version());
  if (!inserted && found->second != symbol.module_version()) {
    throw std::invalid_argument("Module references multiple versions of '" +
                                name + "'");
  }
}

void collect(DependencyMap& dependencies, const detail::ParameterValue& value);

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

void collect(DependencyMap& dependencies, const detail::ParameterValue& value) {
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

void collect(DependencyMap& dependencies, const ir::Value& value) {
  collect(dependencies, value.type());
  if (const auto function = value.referenced_function()) {
    collect(dependencies, function->symbol());
  }
  if (const auto known = detail::FunctionAccess::known_value(value)) {
    collect(dependencies, *known);
  }
}

void collect(DependencyMap& dependencies, const ir::Function& function) {
  for (const ir::Value& argument : function.arguments()) {
    collect(dependencies, argument);
  }
  for (const Type& result : function.result_types()) {
    collect(dependencies, result);
  }
  for (const ir::Block& block : function.blocks()) {
    for (const ir::Value& argument : block.arguments()) {
      collect(dependencies, argument);
    }
    for (const ir::Instruction& instruction : block.instructions()) {
      collect(dependencies, instruction.callee().symbol());
      for (const ir::Value& argument : instruction.arguments()) {
        collect(dependencies, argument);
      }
      for (const ir::Value& result : instruction.results()) {
        collect(dependencies, result);
      }
    }
    const ir::Terminator terminator = block.terminator();
    if (const auto condition = terminator.condition()) {
      collect(dependencies, *condition);
    }
    for (const ir::Value& returned : terminator.returned()) {
      collect(dependencies, returned);
    }
    for (std::size_t index = 0; index < terminator.successor_count(); ++index) {
      for (const ir::Value& argument : terminator.arguments(index)) {
        collect(dependencies, argument);
      }
    }
  }
}

std::vector<Module::Dependency>
function_dependencies(const ir::Function& function) {
  DependencyMap found;
  collect(found, function);
  std::vector<Module::Dependency> result;
  result.reserve(found.size());
  for (const auto& [name, version] : found) {
    result.push_back({name, version});
  }
  return result;
}

}  // namespace

bool Module::insert(std::string name, ir::Function function,
                    Diagnostics& diagnostics) {
  if (!valid_name(name)) {
    diagnostics.report("Module function name '" + name + "' is invalid");
    return false;
  }
  if (storage_->materialized_functions.contains(name) ||
      std::any_of(storage_->functions.begin(), storage_->functions.end(),
                  [&](const detail::FunctionDefinition& declaration) {
                    return declaration.name == name;
                  })) {
    diagnostics.report("Module already contains function '" + name + "'");
    return false;
  }

  std::vector<Dependency> referenced;
  try {
    referenced = function_dependencies(function);
  } catch (const std::exception& error) {
    diagnostics.report(error.what());
    return false;
  }

  auto next = std::make_shared<Storage>(*storage_);
  for (const Dependency& dependency : referenced) {
    if (dependency.name == detail::prelude_module_name ||
        dependency.name == next->name) {
      continue;
    }
    const auto found = std::find_if(
        next->imports.begin(), next->imports.end(),
        [&](const Import& import) { return import.name == dependency.name; });
    if (found != next->imports.end()) {
      if (!found->alias.empty()) {
        diagnostics.report("materialized Function references Module '" +
                           dependency.name +
                           "' without its source import alias");
        return false;
      }
      if (!found->version.contains(dependency.version)) {
        diagnostics.report("Module import for '" + dependency.name +
                           "' excludes referenced version " +
                           to_string(dependency.version));
        return false;
      }
    }
  }
  next->materialized_functions.emplace(
      std::move(name), std::make_shared<ir::Function>(std::move(function)));
  Module candidate(next);
  next->digest = detail::sha256(format(candidate));
  next->digest_revisions.clear();
  next->digest_revisions.reserve(next->materialized_functions.size());
  for (const auto& [function_name, materialized] :
       next->materialized_functions) {
    next->digest_revisions.emplace_back(function_name,
                                        materialized->revision());
  }
  storage_ = std::move(next);
  return true;
}

ir::Function* Module::function(std::string_view name) {
  const auto found = storage_->materialized_functions.find(name);
  if (found == storage_->materialized_functions.end()) {
    return nullptr;
  }
  if (storage_.use_count() == 1 && found->second.use_count() == 1) {
    return found->second.get();
  }
  auto next = std::make_shared<Storage>(*storage_);
  auto selected = next->materialized_functions.find(name);
  selected->second = std::make_shared<ir::Function>(selected->second->clone());
  ir::Function* result = selected->second.get();
  storage_ = std::move(next);
  return result;
}

const ir::Function* Module::function(std::string_view name) const {
  const auto found = storage_->materialized_functions.find(name);
  return found == storage_->materialized_functions.end() ? nullptr
                                                         : found->second.get();
}

std::vector<std::string> Module::function_names() const {
  std::vector<std::string> result;
  result.reserve(storage_->materialized_functions.size());
  for (const auto& [name, unused] : storage_->materialized_functions) {
    static_cast<void>(unused);
    result.push_back(name);
  }
  return result;
}

std::vector<Module::Dependency> Module::dependencies() const {
  DependencyMap found;
  for (const auto& [name, function] : storage_->materialized_functions) {
    static_cast<void>(name);
    collect(found, *function);
  }
  std::vector<Dependency> result;
  result.reserve(found.size());
  for (const auto& [name, version] : found) {
    result.push_back({name, version});
  }
  return result;
}

std::size_t Module::function_count() const {
  return storage_->materialized_functions.size();
}

}  // namespace joggle
