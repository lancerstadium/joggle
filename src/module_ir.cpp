#include "joggle/module.h"

#include "ir_internal.h"
#include "module_storage.h"
#include "prelude.h"
#include "joggle/digest.h"
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

void collect(DependencyMap& dependencies, const detail::ParameterValue& value) {
  if (const Type* type = value.as_type()) {
    collect(dependencies, *type);
  } else if (value.kind() == detail::ParameterValue::Kind::List) {
    for (const detail::ParameterValue& element : value.elements()) {
      collect(dependencies, element);
    }
  }
}

void collect(DependencyMap& dependencies, const Value& value) {
  collect(dependencies, value.type());
  if (const auto function = value.referenced_function()) {
    collect(dependencies, function->symbol());
  }
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
    for (const Op& op : block.ops()) {
      collect(dependencies, op.callee().symbol());
      for (const Value& argument : op.arguments()) {
        collect(dependencies, argument);
      }
      for (const Value& result : op.results()) {
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

std::vector<Module::Dependency>
function_dependencies(const Function& function) {
  DependencyMap found;
  collect(found, function);
  std::vector<Module::Dependency> result;
  result.reserve(found.size());
  for (const auto& [name, version] : found) {
    result.push_back({name, version});
  }
  return result;
}

Module::Expression expression(const detail::ValueSyntax& value) {
  Module::Expression result;
  switch (value.kind) {
  case detail::ValueSyntax::Kind::Number:
    result.kind = Module::Expression::Kind::Number;
    break;
  case detail::ValueSyntax::Kind::Boolean:
    result.kind = Module::Expression::Kind::Boolean;
    break;
  case detail::ValueSyntax::Kind::String:
    result.kind = Module::Expression::Kind::String;
    break;
  case detail::ValueSyntax::Kind::List:
    result.kind = Module::Expression::Kind::List;
    break;
  case detail::ValueSyntax::Kind::Reference:
    result.kind = Module::Expression::Kind::Reference;
    break;
  }
  result.text = value.text;
  result.arguments.reserve(value.elements.size());
  for (const detail::ValueSyntax& element : value.elements) {
    result.arguments.push_back(expression(element));
  }
  return result;
}

detail::FunctionDefinition definition(const Function& function,
                                      std::string_view name) {
  const detail::FunctionSyntax syntax =
      detail::materialized_function_syntax(function, name, true);
  detail::FunctionDefinition result;
  result.name = syntax.name;
  result.inputs.reserve(syntax.arguments.size());
  for (const detail::FunctionArgumentSyntax& argument : syntax.arguments) {
    result.inputs.push_back(
        {argument.name, expression(argument.type), false, std::nullopt});
  }
  result.results.reserve(syntax.result_types.size());
  for (const detail::ValueSyntax& type : syntax.result_types) {
    result.results.push_back({"", expression(type), false, std::nullopt});
  }
  result.types.bindings.resize(result.inputs.size());
  return result;
}

}  // namespace

bool Module::insert(std::string name, Function function,
                    Diagnostics& diagnostics) {
  if (!valid_name(name)) {
    diagnostics.report("Module function name '" + name + "' is invalid");
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
  detail::FunctionDefinition declaration;
  try {
    declaration = definition(function, name);
  } catch (const std::exception& error) {
    diagnostics.report(error.what());
    return false;
  }
  next->functions.push_back({std::move(name), std::move(declaration),
                             std::make_shared<Function>(std::move(function))});
  const std::size_t inserted_index = next->functions.size() - 1U;
  const FunctionDecl inserted(next, inserted_index);
  for (std::size_t index = 0; index < inserted_index; ++index) {
    if (next->functions[index].declaration &&
        next->functions[index].name == inserted.name() &&
        FunctionDecl(next, index).signature() == inserted.signature()) {
      diagnostics.report("Module already contains function '" +
                         inserted.signature() + "'");
      return false;
    }
  }
  next->declaration_digest = Module::compute_declaration_digest(next);
  Module candidate(next);
  Module owner = detail::ModuleAccess::declaration_view(candidate);
  const auto declarations = owner.overloads(inserted.name());
  const auto attached = std::find_if(
      declarations.begin(), declarations.end(), [&](const FunctionDecl& value) {
        return value.signature() == inserted.signature();
      });
  if (attached == declarations.end()) {
    diagnostics.report("Module lost inserted function '" +
                       inserted.signature() + "'");
    return false;
  }
  if (!detail::FunctionAccess::attach(
          *next->functions[inserted_index].ir, *attached, std::move(owner),
          diagnostics)) {
    return false;
  }
  next->digest = Module::compute_digest(next);
  next->digest_revisions.clear();
  next->digest_revisions.reserve(next->functions.size());
  for (const detail::FunctionMember& member : next->functions) {
    if (member.ir) {
      next->digest_revisions.emplace_back(member.name, member.ir->revision());
    }
  }
  storage_ = std::move(next);
  return true;
}

Function* Module::body(FunctionDecl declaration) {
  if (declaration.storage_->name != storage_->name ||
      declaration.storage_->version != storage_->version) {
    return nullptr;
  }
  const auto matches = [&](const detail::FunctionMember& function,
                           std::size_t index) {
    if (!function.declaration || !function.ir ||
        function.name != declaration.name()) {
      return false;
    }
    return FunctionDecl(storage_, index).signature() == declaration.signature();
  };
  auto found = storage_->functions.end();
  for (auto current = storage_->functions.begin();
       current != storage_->functions.end(); ++current) {
    const auto index =
        static_cast<std::size_t>(current - storage_->functions.begin());
    if (matches(*current, index)) {
      found = current;
      break;
    }
  }
  if (found == storage_->functions.end()) {
    return nullptr;
  }
  if (storage_.use_count() == 1 && found->ir.use_count() == 1) {
    return found->ir.get();
  }
  auto next = std::make_shared<Storage>(*storage_);
  const auto index =
      static_cast<std::size_t>(found - storage_->functions.begin());
  auto selected = next->functions.begin() + static_cast<std::ptrdiff_t>(index);
  selected->ir = std::make_shared<Function>(*selected->ir);
  Function* result = selected->ir.get();
  storage_ = std::move(next);
  return result;
}

std::vector<Module::Dependency> Module::dependencies() const {
  DependencyMap found;
  for (const detail::FunctionMember& function : storage_->functions) {
    if (function.ir) {
      collect(found, *function.ir);
    }
  }
  std::vector<Dependency> result;
  result.reserve(found.size());
  for (const auto& [name, version] : found) {
    result.push_back({name, version});
  }
  return result;
}

}  // namespace joggle
