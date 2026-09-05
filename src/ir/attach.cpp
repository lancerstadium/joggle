#include "joggle/mod.h"

#include "ir/fn.h"
#include "ir/mod.h"
#include "lang/prelude.h"
#include "joggle/digest.h"
#include "ir/type.h"

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

void collect(DependencyMap& dependencies, const Mod::Symbol& symbol) {
  const std::string name(symbol.mod_name());
  const auto [found, inserted] =
      dependencies.emplace(name, symbol.mod_version());
  if (!inserted && found->second != symbol.mod_version()) {
    throw std::invalid_argument("Mod references multiple versions of '" + name +
                                "'");
  }
}

void collect(DependencyMap& dependencies, const detail::ParamVal& value);
void collect(DependencyMap& dependencies, const Fn& fn);

void collect(DependencyMap& dependencies, const Type& type) {
  collect(dependencies, type.schema().symbol());
  for (const detail::ParamVal& parameter :
       detail::TypeAccess::parameters(type)) {
    collect(dependencies, parameter);
  }
}

void collect(DependencyMap& dependencies, const detail::ParamVal& value) {
  if (const Type* type = value.as_type()) {
    collect(dependencies, *type);
  } else if (value.kind() == detail::ParamVal::Kind::List) {
    for (const detail::ParamVal& element : value.elements()) {
      collect(dependencies, element);
    }
  }
}

void collect(DependencyMap& dependencies, const Val& value) {
  collect(dependencies, value.type());
  if (const auto fn = value.referenced_fn()) {
    collect(dependencies, fn->symbol());
  }
  if (const auto fn = value.inline_fn()) {
    collect(dependencies, *fn);
  }
  for (const auto& [name, binding] : value.bindings()) {
    static_cast<void>(name);
    collect(dependencies, binding);
  }
  if (const auto known = detail::FnAccess::known_value(value)) {
    collect(dependencies, *known);
  }
}

void collect(DependencyMap& dependencies, const Fn& fn) {
  for (const Val& argument : fn.arguments()) {
    collect(dependencies, argument);
  }
  for (const Type& result : fn.result_types()) {
    collect(dependencies, result);
  }
  for (const Blk& block : fn.blks()) {
    for (const Val& argument : block.arguments()) {
      collect(dependencies, argument);
    }
    for (const Op& op : block.ops()) {
      collect(dependencies, op.callee());
      for (const Val& argument : op.arguments()) {
        collect(dependencies, argument);
      }
      for (const Val& result : op.results()) {
        collect(dependencies, result);
      }
    }
    const Term terminator = block.terminator();
    if (const auto condition = terminator.condition()) {
      collect(dependencies, *condition);
    }
    for (const Val& returned : terminator.returned()) {
      collect(dependencies, returned);
    }
    for (std::size_t index = 0; index < terminator.successor_count(); ++index) {
      for (const Val& argument : terminator.arguments(index)) {
        collect(dependencies, argument);
      }
    }
  }
}

std::vector<Mod::Dependency> fn_dependencies(const Fn& fn) {
  DependencyMap found;
  collect(found, fn);
  std::vector<Mod::Dependency> result;
  result.reserve(found.size());
  for (const auto& [name, version] : found) {
    result.push_back({name, version});
  }
  return result;
}

Mod::Expr expression(const detail::ValSyntax& value) {
  Mod::Expr result;
  switch (value.kind) {
  case detail::ValSyntax::Kind::Number:
    result.kind = Mod::Expr::Kind::Number;
    break;
  case detail::ValSyntax::Kind::Boolean:
    result.kind = Mod::Expr::Kind::Boolean;
    break;
  case detail::ValSyntax::Kind::String:
    result.kind = Mod::Expr::Kind::String;
    break;
  case detail::ValSyntax::Kind::List:
    result.kind = Mod::Expr::Kind::List;
    break;
  case detail::ValSyntax::Kind::Reference:
    result.kind = Mod::Expr::Kind::Reference;
    break;
  }
  result.text = value.text;
  result.arguments.reserve(value.elements.size());
  for (const detail::ValSyntax& element : value.elements) {
    result.arguments.push_back(expression(element));
  }
  return result;
}

detail::FnDef definition(const Fn& fn, std::string_view name) {
  const detail::FnSyntax syntax =
      detail::materialized_fn_syntax(fn, name, true);
  detail::FnDef result;
  result.name = syntax.name;
  result.inputs.reserve(syntax.arguments.size());
  for (const detail::FnArgSyntax& argument : syntax.arguments) {
    result.inputs.push_back(
        {argument.name, expression(argument.type), false, std::nullopt});
  }
  result.results.reserve(syntax.result_types.size());
  for (const detail::ValSyntax& type : syntax.result_types) {
    result.results.push_back({"", expression(type), false, std::nullopt});
  }
  result.types.bindings.resize(result.inputs.size());
  return result;
}

}  // namespace

bool Mod::insert(std::string name, Fn fn, Diag& diagnostics) {
  if (!valid_name(name)) {
    diagnostics.report("Mod fn name '" + name + "' is invalid");
    return false;
  }
  std::vector<Dependency> referenced;
  try {
    referenced = fn_dependencies(fn);
  } catch (const std::exception& error) {
    diagnostics.report(error.what());
    return false;
  }

  auto next = std::make_shared<Storage>(*storage_);
  for (const Dependency& dependency : referenced) {
    if (dependency.name == detail::prelude_mod_name ||
        dependency.name == next->name) {
      continue;
    }
    const auto found = std::find_if(
        next->imports.begin(), next->imports.end(),
        [&](const Import& import) { return import.name == dependency.name; });
    if (found != next->imports.end()) {
      if (!found->alias.empty()) {
        diagnostics.report("materialized Fn references Mod '" +
                           dependency.name +
                           "' without its source import alias");
        return false;
      }
      if (!found->version.contains(dependency.version)) {
        diagnostics.report("Mod import for '" + dependency.name +
                           "' excludes referenced version " +
                           to_string(dependency.version));
        return false;
      }
    }
  }
  detail::FnDef declaration;
  try {
    declaration = definition(fn, name);
  } catch (const std::exception& error) {
    diagnostics.report(error.what());
    return false;
  }
  next->fns.push_back({std::move(name), std::move(declaration),
                       std::make_shared<Fn>(std::move(fn))});
  const std::size_t inserted_index = next->fns.size() - 1U;
  const FnDecl inserted(next, inserted_index);
  for (std::size_t index = 0; index < inserted_index; ++index) {
    if (next->fns[index].declaration &&
        next->fns[index].name == inserted.name() &&
        FnDecl(next, index).signature() == inserted.signature()) {
      diagnostics.report("Mod already contains fn '" + inserted.signature() +
                         "'");
      return false;
    }
  }
  next->declaration_digest = Mod::compute_declaration_digest(next);
  Mod candidate(next);
  Mod owner = detail::ModAccess::declaration_view(candidate);
  const auto declarations = owner.overloads(inserted.name());
  const auto attached = std::find_if(
      declarations.begin(), declarations.end(), [&](const FnDecl& value) {
        return value.signature() == inserted.signature();
      });
  if (attached == declarations.end()) {
    diagnostics.report("Mod lost inserted fn '" + inserted.signature() + "'");
    return false;
  }
  if (!detail::FnAccess::attach(*next->fns[inserted_index].ir, *attached,
                                std::move(owner), diagnostics)) {
    return false;
  }
  next->digest = Mod::compute_digest(next);
  next->digest_revisions.clear();
  next->digest_revisions.reserve(next->fns.size());
  for (const detail::FnMember& member : next->fns) {
    if (member.ir) {
      next->digest_revisions.emplace_back(member.name, member.ir->revision());
    }
  }
  storage_ = std::move(next);
  return true;
}

Fn* Mod::body(FnDecl declaration) {
  if (declaration.storage_->name != storage_->name ||
      declaration.storage_->version != storage_->version) {
    return nullptr;
  }
  const auto matches = [&](const detail::FnMember& fn, std::size_t index) {
    if (!fn.declaration || !fn.ir || fn.name != declaration.name()) {
      return false;
    }
    return FnDecl(storage_, index).signature() == declaration.signature();
  };
  auto found = storage_->fns.end();
  for (auto current = storage_->fns.begin(); current != storage_->fns.end();
       ++current) {
    const auto index =
        static_cast<std::size_t>(current - storage_->fns.begin());
    if (matches(*current, index)) {
      found = current;
      break;
    }
  }
  if (found == storage_->fns.end()) {
    return nullptr;
  }
  if (storage_.use_count() == 1 && found->ir.use_count() == 1) {
    return found->ir.get();
  }
  auto next = std::make_shared<Storage>(*storage_);
  const auto index = static_cast<std::size_t>(found - storage_->fns.begin());
  auto selected = next->fns.begin() + static_cast<std::ptrdiff_t>(index);
  selected->ir = std::make_shared<Fn>(*selected->ir);
  Fn* result = selected->ir.get();
  storage_ = std::move(next);
  return result;
}

std::vector<Mod::Dependency> Mod::dependencies() const {
  DependencyMap found;
  for (const detail::FnMember& fn : storage_->fns) {
    if (fn.ir) {
      collect(found, *fn.ir);
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
