#include "compile/compiler.h"

#include "base/diag.h"
#include "compile/eval.h"
#include "ir/fn.h"
#include "ir/mod.h"
#include "ir/type.h"
#include "lang/fn.h"
#include "lang/prelude.h"
#include "sema/infer.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace joggle {
namespace {

using detail::belongs_to;
using detail::ParamVal;

template <typename Subject, typename Verifier>
bool invoke_verifier(Verifier& verifier, const Subject& subject,
                     std::string description, Diag& diagnostics,
                     std::optional<Loc> location = std::nullopt) {
  Diag reported;
  bool accepted = false;
  try {
    accepted = verifier(subject, reported);
  } catch (const std::exception& exception) {
    reported.report("semantic verifier for " + description +
                    " threw: " + exception.what());
  } catch (...) {
    reported.report("semantic verifier for " + description +
                    " threw an unknown exception");
  }
  if (!accepted && reported.ok()) {
    reported.report("semantic verifier rejected " + description);
  }
  const bool valid = accepted && reported.ok();
  for (const Issue& entry : reported.issues()) {
    Issue diagnostic = entry;
    if (!diagnostic.source && location) {
      diagnostic.source = location;
    }
    diagnostics.report(std::move(diagnostic));
  }
  return valid;
}

bool accepts_known_value(const Type& type, const ParamVal& value) {
  const Mod::Symbol symbol = type.schema().symbol();
  if (symbol.mod_name() != detail::prelude_mod_name) {
    return false;
  }
  const std::string_view name = symbol.local_name();
  if (name == "int" || name == "i8" || name == "i16" || name == "i32" ||
      name == "i64" || name == "index") {
    return value.kind() == ParamVal::Kind::I64;
  }
  if (name == "u8" || name == "u16" || name == "u32" || name == "u64") {
    return value.kind() == ParamVal::Kind::I64 && *value.as_i64() >= 0;
  }
  if (name == "real" || name == "f16" || name == "bf16" || name == "f32" ||
      name == "f64") {
    return value.kind() == ParamVal::Kind::I64 ||
           value.kind() == ParamVal::Kind::F64;
  }
  if (name == "bool" || name == "i1") {
    return value.kind() == ParamVal::Kind::Boolean;
  }
  if (name == "string") {
    return value.kind() == ParamVal::Kind::String;
  }
  if (name == "type") {
    return value.kind() == ParamVal::Kind::Type;
  }
  if (name != "list" || value.kind() != ParamVal::Kind::List) {
    return false;
  }
  const auto parameters = detail::TypeAccess::parameters(type);
  if (parameters.size() != 1U || parameters.front().as_type() == nullptr) {
    return false;
  }
  return std::all_of(value.elements().begin(), value.elements().end(),
                     [&](const ParamVal& element) {
                       return accepts_known_value(*parameters.front().as_type(),
                                                  element);
                     });
}

}  // namespace

Compiler::Compiler() : Compiler(Limits{}) {}

Compiler::Compiler(Limits limits) : state_(std::make_unique<State>()) {
  state_->evaluation_limits = limits;
  auto prelude =
      parse_mod(detail::prelude_mod_source(), state_->diagnostics, "<prelude>");
  if (prelude) {
    add_mod(std::move(*prelude), false, std::nullopt);
    bind_prelude_mod();
    bind_prelude_primitives();
  }
}
Compiler::~Compiler() = default;
Compiler::Compiler(Compiler&&) noexcept = default;
Compiler& Compiler::operator=(Compiler&&) noexcept = default;

bool Compiler::ok() const { return state_->diagnostics.ok(); }

bool Compiler::linked() const { return state_->linked; }

Compiler::Limits Compiler::evaluation_limits() const {
  return state_->evaluation_limits;
}

std::optional<Mod> Compiler::mod(std::string_view name) const {
  const auto found = state_->mods.find(name);
  if (found == state_->mods.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::vector<Mod> Compiler::mods() const {
  std::vector<Mod> result;
  result.reserve(state_->mods.size());
  for (const auto& [name, mod] : state_->mods) {
    if (name == detail::prelude_mod_name) {
      continue;
    }
    result.push_back(mod);
  }
  return result;
}

std::optional<Type> Compiler::make(const Mod::TypeDecl& schema,
                                   std::span<const ParamVal> parameters) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot construct a type before the compiler is linked");
    return std::nullopt;
  }
  const Mod::Symbol symbol = schema.symbol();
  const auto owner = state_->mods.find(symbol.mod_name());
  if (owner == state_->mods.end() ||
      owner->second.version() != symbol.mod_version() ||
      owner->second.declaration_digest() != symbol.declaration_digest()) {
    state_->diagnostics.report("type schema '" + symbol.qualified_name() +
                               "' is not part of this compiler");
    return std::nullopt;
  }
  auto values =
      detail::validate_parameters(symbol.qualified_name(), schema.parameters(),
                                  parameters, state_->diagnostics);
  if (!values) {
    return std::nullopt;
  }
  if (!std::all_of(values->begin(), values->end(), [&](const ParamVal& value) {
        return belongs_to(state_->mods, value);
      })) {
    state_->diagnostics.report("type '" + symbol.qualified_name() +
                               "' references a value outside this compiler's "
                               "mod closure");
    return std::nullopt;
  }
  std::string construction = symbol.stable_name();
  for (const ParamVal& value : *values) {
    const std::string canonical = value.canonical();
    construction += "/" + std::to_string(canonical.size()) + ":" + canonical;
  }
  if (!state_->constructing_types.insert(construction).second) {
    state_->diagnostics.report(
        "recursive derived parameters while constructing type '" +
        symbol.qualified_name() + "'");
    return std::nullopt;
  }
  auto derived = detail::resolve_derived_parameters(*this, schema, *values,
                                                    state_->diagnostics);
  state_->constructing_types.erase(construction);
  if (!derived) {
    return std::nullopt;
  }
  Type type =
      detail::TypeAccess::make(schema, std::move(*values), std::move(*derived));
  const auto verifier = state_->type_verifiers.find(symbol.stable_name());
  if (verifier != state_->type_verifiers.end() &&
      !invoke_verifier(verifier->second, type,
                       "type '" + symbol.qualified_name() + "'",
                       state_->diagnostics)) {
    return std::nullopt;
  }
  return type;
}

std::optional<Type> Compiler::make(std::string_view prelude_type) {
  if (!detail::is_prelude_type(prelude_type)) {
    state_->diagnostics.report("unknown Prelude type '" +
                               std::string(prelude_type) + "'");
    return std::nullopt;
  }
  const auto owner = state_->mods.find(detail::prelude_mod_name);
  const auto declaration = owner == state_->mods.end()
                               ? std::optional<Mod::TypeDecl>{}
                               : owner->second.type(prelude_type);
  if (!declaration) {
    state_->diagnostics.report("Prelude type '" + std::string(prelude_type) +
                               "' is unavailable");
    return std::nullopt;
  }
  return make(*declaration, std::span<const ParamVal>{});
}

std::optional<Val> Compiler::make_known(Type type, ParamVal value) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot create a Known value before the compiler is linked");
    return std::nullopt;
  }
  if (!belongs_to(state_->mods, ParamVal(type)) ||
      !belongs_to(state_->mods, value)) {
    state_->diagnostics.report(
        "Known value references a declaration outside this compiler");
    return std::nullopt;
  }
  if (!accepts_known_value(type, value)) {
    state_->diagnostics.report("Known value payload does not match type '" +
                               type.schema().symbol().qualified_name() + "'");
    return std::nullopt;
  }
  return Val(std::move(type), std::move(value));
}

std::optional<Fn> Compiler::create_fn() {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot create a fn before the compiler is linked");
    return std::nullopt;
  }
  std::vector<Mod> mods;
  mods.reserve(state_->mods.size());
  for (const auto& [name, mod] : state_->mods) {
    static_cast<void>(name);
    mods.push_back(mod);
  }
  return Fn(std::move(mods));
}

std::optional<Mod> Compiler::materialize(const Mod& mod) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot materialize a Mod before the compiler is linked");
    return std::nullopt;
  }
  const auto owner = state_->mods.find(mod.name());
  if (owner == state_->mods.end() || owner->second.version() != mod.version() ||
      owner->second.digest() != mod.digest()) {
    state_->diagnostics.report("Mod '" + std::string(mod.name()) +
                               "' is not in this compilation");
    return std::nullopt;
  }

  const Mod& linked = owner->second;
  auto storage = std::make_shared<Mod::Storage>(*linked.storage_);
  const auto fns = linked.fns();
  for (std::size_t index = 0; index < fns.size(); ++index) {
    const Mod::FnDecl& declaration = fns[index];
    if (declaration.body() != nullptr ||
        !detail::ModAccess::body(linked, declaration) ||
        !detail::compiler_results(declaration).empty() ||
        !detail::has_default_specialization(declaration)) {
      continue;
    }
    auto fn = materialize(declaration);
    if (!fn) {
      return std::nullopt;
    }
    storage->fns[index].ir = std::make_shared<Fn>(std::move(*fn));
  }
  storage->digest_revisions.clear();
  return Mod(std::move(storage));
}

std::optional<Fn> Compiler::materialize(Mod::FnDecl declaration) {
  return materialize(std::move(declaration), {});
}

std::optional<Fn> Compiler::materialize(Mod::FnDecl declaration,
                                        std::vector<Val> known_arguments) {
  return materialize(declaration.symbol(), std::move(known_arguments));
}

std::optional<Fn> Compiler::materialize(std::string_view name) {
  return materialize(name, {});
}

std::optional<Fn> Compiler::materialize(std::string_view name,
                                        std::vector<Val> known_arguments) {
  const auto declaration = lookup(name);
  if (!declaration) {
    return std::nullopt;
  }
  return materialize(*declaration, std::move(known_arguments));
}

std::optional<Fn> Compiler::materialize(Mod::Symbol symbol) {
  return materialize(std::move(symbol), {});
}

std::optional<Fn> Compiler::materialize(Mod::Symbol symbol,
                                        std::vector<Val> known_arguments) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot construct a fn before the compiler is linked");
    return std::nullopt;
  }
  if (symbol.kind() != Mod::Symbol::Kind::Fn) {
    state_->diagnostics.report("symbol '" + symbol.qualified_name() +
                               "' is not a fn");
    return std::nullopt;
  }
  const auto owner = state_->mods.find(symbol.mod_name());
  if (owner == state_->mods.end() ||
      owner->second.version() != symbol.mod_version() ||
      owner->second.declaration_digest() != symbol.declaration_digest()) {
    state_->diagnostics.report("fn '" + symbol.qualified_name() +
                               "' is not in this compilation");
    return std::nullopt;
  }
  const auto overloads = owner->second.overloads(symbol.local_name());
  const auto fn = std::find_if(
      overloads.begin(), overloads.end(), [&](const Mod::FnDecl& candidate) {
        return candidate.symbol() == symbol &&
               candidate.form() == Mod::FnDecl::Form::Body;
      });
  const auto definition = fn == overloads.end()
                              ? std::shared_ptr<const detail::FnBody>{}
                              : detail::ModAccess::body(owner->second, *fn);
  const bool compile_time_only = fn != overloads.end() &&
                                 detail::value_inputs(*fn).empty() &&
                                 detail::value_results(*fn).empty() &&
                                 detail::ModAccess::expression(*fn) != nullptr;
  if (!definition || compile_time_only) {
    state_->diagnostics.report("unknown fn '" + symbol.qualified_name() + "'");
    return std::nullopt;
  }
  return detail::instantiate_fn(*this, *fn, *definition, state_->diagnostics,
                                std::move(known_arguments));
}

std::optional<Fn> Compiler::materialize(const Op& call) {
  return materialize(call, state_->diagnostics);
}

std::optional<Fn> Compiler::materialize(const Op& call, Diag& diagnostics) {
  if (!state_->linked) {
    diagnostics.report("cannot construct a fn before the compiler is "
                       "linked");
    return std::nullopt;
  }
  if (!call.valid()) {
    diagnostics.report("cannot materialize an invalid call");
    return std::nullopt;
  }

  const Val callee_value = call.callee();
  if (const auto body = callee_value.inline_fn()) {
    return *body;
  }
  const auto referenced = callee_value.referenced_fn();
  if (!referenced) {
    diagnostics.report("cannot materialize a dynamic callee");
    return std::nullopt;
  }
  const Mod::FnDecl callee = *referenced;
  const auto owner = state_->mods.find(callee.symbol().mod_name());
  if (owner == state_->mods.end() ||
      owner->second.version() != callee.symbol().mod_version() ||
      owner->second.declaration_digest() !=
          callee.symbol().declaration_digest()) {
    diagnostics.report("fn '" + callee.symbol().qualified_name() +
                       "' is not in this compilation");
    return std::nullopt;
  }
  const auto overloads = owner->second.overloads(callee.name());
  const auto declaration = std::find_if(
      overloads.begin(), overloads.end(), [&](const Mod::FnDecl& candidate) {
        return candidate.symbol() == callee.symbol() &&
               candidate.form() == Mod::FnDecl::Form::Body;
      });
  const auto definition =
      declaration == overloads.end()
          ? std::shared_ptr<const detail::FnBody>{}
          : detail::ModAccess::body(owner->second, *declaration);
  if (!definition) {
    diagnostics.report("fn '" + callee.symbol().qualified_name() +
                       "' has no source body");
    return std::nullopt;
  }

  std::vector<Type> argument_types;
  std::vector<Val> known_values;
  std::vector<std::optional<detail::ParamVal>> known_arguments;
  known_arguments.reserve(detail::compiler_inputs(*declaration).size());
  const auto parameters = declaration->inputs();
  const auto arguments = call.arguments();
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const Val& argument = arguments[index];
    const std::size_t parameter =
        detail::FnAccess::argument_parameter(call, index);
    if (parameter >= parameters.size()) {
      diagnostics.report("call to '" + callee.symbol().qualified_name() +
                         "' has an invalid argument map");
      return std::nullopt;
    }
    if (detail::is_value_port(parameters[parameter])) {
      argument_types.push_back(argument.type());
    }
  }
  for (const auto& [name, binding] : callee_value.bindings()) {
    const auto value = detail::FnAccess::known_value(binding);
    if (!value) {
      diagnostics.report("callee binding '" + name + "' is not Known");
      return std::nullopt;
    }
    known_values.push_back(binding);
    known_arguments.push_back(*value);
  }
  std::vector<std::optional<Type>> expected_results;
  for (const Val& result : call.results()) {
    expected_results.push_back(result.type());
  }
  const auto specialization = detail::resolve_call_types(
      *this, *declaration, argument_types, known_arguments, expected_results,
      diagnostics, detail::FnAccess::location(call));
  if (!specialization) {
    return std::nullopt;
  }
  detail::KnownBindings generic_bindings;
  for (const auto& generic : declaration->generics()) {
    const auto binding = specialization->bindings.find(generic.name);
    if (binding != specialization->bindings.end()) {
      generic_bindings.emplace(binding->first, binding->second);
    }
  }
  return detail::instantiate_fn(*this, *declaration, *definition, diagnostics,
                                std::move(known_values),
                                std::move(generic_bindings));
}

bool Compiler::verify(const Fn& fn) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot verify a fn before the compiler is linked");
    return false;
  }
  if (!detail::FnAccess::verify_structure(fn, state_->diagnostics)) {
    return false;
  }
  bool valid =
      detail::FnAccess::verify_contracts(fn, *this, state_->diagnostics);
  for (const Op& op : fn.ops()) {
    const auto declaration = op.callee().referenced_fn();
    if (!declaration) {
      continue;
    }
    const Mod::FnDecl schema = *declaration;
    const Mod::Symbol symbol = schema.symbol();
    const auto location = detail::FnAccess::location(op);
    const auto verifier = state_->op_verifiers.find(symbol.stable_name());
    if (verifier != state_->op_verifiers.end() &&
        !invoke_verifier(verifier->second, op,
                         "call to '" + symbol.qualified_name() + "'",
                         state_->diagnostics, location)) {
      valid = false;
    }
  }
  return valid;
}

bool Compiler::verify(const Mod& mod) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot verify a Mod before the compiler is linked");
    return false;
  }
  bool valid = true;
  std::vector<Fn::Revision> verified;
  for (const Mod::FnDecl& member : mod.fns()) {
    const Fn* body = member.body();
    if (body == nullptr) {
      continue;
    }
    const auto declaration = body->declaration();
    if (!declaration || *declaration != member) {
      state_->diagnostics.report(
          "Mod fn '" + std::string(member.name()) +
          "' has a body attached to a different declaration");
      valid = false;
      continue;
    }
    const auto revision = body->revision();
    if (std::find(verified.begin(), verified.end(), revision) !=
        verified.end()) {
      continue;
    }
    if (!verify(*body)) {
      state_->diagnostics.report("Mod fn '" + std::string(member.name()) +
                                 "' is invalid");
      valid = false;
    } else {
      verified.push_back(revision);
    }
  }
  return valid;
}

const Diag& Compiler::diag() const { return state_->diagnostics; }

}  // namespace joggle
