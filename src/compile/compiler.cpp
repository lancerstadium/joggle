#include "compile/compiler.h"

#include "sema/call.h"
#include "sema/check.h"
#include "base/diag.h"
#include "sema/domain.h"
#include "compile/eval.h"
#include "lang/expr.h"
#include "lang/fn.h"
#include "ir/fn.h"
#include "joggle/detail/native.h"
#include "joggle/mod.h"
#include "ir/mod.h"
#include "pkg/repo.h"
#include "lang/prelude.h"
#include "sema/infer.h"
#include "ir/type.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <exception>
#include <fstream>
#include <map>
#include <limits>
#include <sstream>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace joggle {
namespace {

using detail::ParamVal;
using detail::mod_identity;

template <typename Mods>
bool belongs_to(const Mods& mods, const ParamVal& value) {
  const auto contains = [&](const Mod::Symbol& symbol) {
    const auto owner = mods.find(symbol.mod_name());
    return owner != mods.end() &&
           owner->second.version() == symbol.mod_version() &&
           owner->second.declaration_digest() == symbol.declaration_digest();
  };
  if (const Type* type = value.as_type()) {
    return contains(type->schema().symbol());
  }
  if (value.kind() == ParamVal::Kind::List) {
    return std::all_of(
        value.elements().begin(), value.elements().end(),
        [&](const ParamVal& element) { return belongs_to(mods, element); });
  }
  return true;
}

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

std::optional<std::pair<std::string_view, std::string_view>>
qualified_member(std::string_view name) {
  const std::size_t separator = name.find('.');
  if (separator == std::string_view::npos || separator == 0U ||
      separator + 1U == name.size() ||
      name.find('.', separator + 1U) != std::string_view::npos) {
    return std::nullopt;
  }
  return std::pair{name.substr(0U, separator), name.substr(separator + 1U)};
}

std::optional<detail::Domain> parameter_domain(const Mod::ParamDecl& field) {
  return detail::kernel_domain(field.domain);
}

std::string_view resolve_prefix(const Mod& mod, std::string_view prefix);

template <typename Mods>
std::optional<Mod::TypeDecl>
field_type_declaration(const Mods& mods, const Mod::FnDecl& fn,
                       const Mod::ParamDecl& field) {
  if (field.domain.kind != Mod::Expr::Kind::Reference ||
      detail::kernel_domain(field.domain)) {
    return std::nullopt;
  }
  const std::size_t dot = field.domain.text.find('.');
  const Mod::Symbol symbol = fn.symbol();
  std::string_view mod_name = symbol.mod_name();
  const auto owner = mods.find(mod_name);
  if (dot != std::string::npos) {
    mod_name = owner == mods.end()
                   ? std::string_view(field.domain.text).substr(0, dot)
                   : resolve_prefix(
                         owner->second,
                         std::string_view(field.domain.text).substr(0, dot));
  }
  const std::string_view local =
      dot == std::string::npos
          ? std::string_view(field.domain.text)
          : std::string_view(field.domain.text).substr(dot + 1U);
  const auto mod = mods.find(mod_name);
  return mod == mods.end() ? std::nullopt : mod->second.type(local);
}

std::string_view resolve_prefix(const Mod& mod, std::string_view prefix) {
  if (prefix == mod.name()) {
    return mod.name();
  }
  const auto found = std::find_if(
      mod.imports().begin(), mod.imports().end(),
      [&](const Mod::Import& import) { return import.prefix() == prefix; });
  return found == mod.imports().end() ? prefix : std::string_view(found->name);
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

  const Mod::FnDecl callee = call.callee();
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
      continue;
    }
    const auto value = detail::FnAccess::known_value(argument);
    if (!value) {
      diagnostics.report("call property '" + parameters[parameter].name +
                         "' is not Known");
      return std::nullopt;
    }
    known_values.push_back(argument);
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

void Compiler::bind_verifier(Mod::TypeDecl schema, VerifierFn<Type> verifier) {
  const Mod::Symbol symbol = schema.symbol();
  const auto owner = state_->mods.find(symbol.mod_name());
  if (owner == state_->mods.end() ||
      owner->second.version() != symbol.mod_version() ||
      owner->second.declaration_digest() != symbol.declaration_digest()) {
    state_->diagnostics.report("cannot bind type '" + symbol.qualified_name() +
                               "' outside this compiler");
    return;
  }
  if (!verifier) {
    state_->diagnostics.report("type verifier binding is empty");
    return;
  }
  if (!state_->type_verifiers.emplace(symbol.stable_name(), std::move(verifier))
           .second) {
    state_->diagnostics.report("type '" + symbol.qualified_name() +
                               "' already has a verifier binding");
  }
}

void Compiler::bind_verifier(Mod::FnDecl schema, VerifierFn<Op> verifier) {
  const Mod::Symbol symbol = schema.symbol();
  const auto owner = state_->mods.find(symbol.mod_name());
  if (owner == state_->mods.end() ||
      owner->second.version() != symbol.mod_version() ||
      owner->second.declaration_digest() != symbol.declaration_digest()) {
    state_->diagnostics.report("cannot bind an Op verifier for fn '" +
                               symbol.qualified_name() +
                               "' outside this compiler");
    return;
  }
  if (!verifier) {
    state_->diagnostics.report("Op verifier binding is empty");
    return;
  }
  if (!state_->op_verifiers.emplace(symbol.stable_name(), std::move(verifier))
           .second) {
    state_->diagnostics.report("fn '" + symbol.qualified_name() +
                               "' already has an Op verifier");
  }
}

bool Compiler::bind_representation(Mod::TypeDecl schema,
                                   std::string_view type) {
  if (!schema.parameters().empty()) {
    state_->diagnostics.report(
        "a parameterized host representation needs a projection returning "
        "its ordered type parameters");
    return false;
  }
  RepresentationProjector projector =
      [](Compiler& compiler, const Mod::TypeDecl& declaration, const void*) {
        return compiler.make(declaration);
      };
  return bind_representation(std::move(schema), type, std::move(projector));
}

bool Compiler::bind_representation(Mod::TypeDecl schema, std::string_view type,
                                   RepresentationProjector projector) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot register a host representation before the compiler is linked");
    return false;
  }
  const Mod::Symbol symbol = schema.symbol();
  const auto owner = state_->mods.find(symbol.mod_name());
  if (owner == state_->mods.end() ||
      owner->second.version() != symbol.mod_version() ||
      owner->second.declaration_digest() != symbol.declaration_digest()) {
    state_->diagnostics.report("cannot represent type '" +
                               symbol.qualified_name() +
                               "' outside this compiler");
    return false;
  }
  if (detail::cpp_value_domain(type)) {
    state_->diagnostics.report(
        "built-in C++ representations belong to Prelude and cannot be "
        "registered again");
    return false;
  }
  if (!projector) {
    state_->diagnostics.report("a host representation projection is empty");
    return false;
  }
  const std::string identity = symbol.stable_name();
  const auto by_type = state_->host_types.find(type);
  const auto by_schema = state_->host_representations.find(identity);
  if (by_type != state_->host_types.end() ||
      by_schema != state_->host_representations.end()) {
    if (by_type != state_->host_types.end() &&
        by_schema != state_->host_representations.end() &&
        by_type->second.schema == schema && by_schema->second == type) {
      return true;
    }
    state_->diagnostics.report(
        "a C++ type and a Mod type must have a one-to-one host "
        "representation");
    return false;
  }
  state_->host_types.emplace(
      std::string(type),
      State::HostRepresentation{schema, std::move(projector)});
  state_->host_representations.emplace(identity, std::string(type));
  return true;
}

bool Compiler::accepts_host_type(const Mod::FnDecl& fn,
                                 const Mod::ParamDecl& field,
                                 std::string_view type) const {
  if (const auto domain = detail::cpp_value_domain(type)) {
    return parameter_domain(field) == domain;
  }
  const auto declaration = field_type_declaration(state_->mods, fn, field);
  const auto representation = declaration
                                  ? state_->host_representations.find(
                                        declaration->symbol().stable_name())
                                  : state_->host_representations.end();
  return representation != state_->host_representations.end() &&
         representation->second == type;
}

bool Compiler::project_host_value(detail::ExecVal& value) {
  auto* host = std::get_if<detail::HostVal>(&value);
  if (host == nullptr || host->concrete_type) {
    return true;
  }
  const auto representation = state_->host_types.find(host->cpp_type);
  if (representation == state_->host_types.end()) {
    state_->diagnostics.report(
        "a C++ value has no registered Mod type representation");
    return false;
  }
  std::optional<Type> projected;
  try {
    projected = representation->second.project(
        *this, representation->second.schema, host->storage.get());
  } catch (const std::exception& exception) {
    state_->diagnostics.report("host type projection threw: " +
                               std::string(exception.what()));
    return false;
  } catch (...) {
    state_->diagnostics.report(
        "host type projection threw an unknown exception");
    return false;
  }
  if (!projected || projected->schema() != representation->second.schema ||
      !belongs_to(state_->mods, ParamVal(*projected))) {
    state_->diagnostics.report(
        "host type projection did not produce an instance of its registered "
        "Mod type");
    return false;
  }
  host->concrete_type = std::move(*projected);
  return true;
}

bool Compiler::check_host_values(const Mod::FnDecl& fn,
                                 std::span<const detail::ExecVal> arguments,
                                 std::span<const detail::ExecVal> results) {
  const bool has_host_input =
      std::any_of(arguments.begin(), arguments.end(), [](const auto& value) {
        return std::holds_alternative<detail::HostVal>(value);
      });
  const bool has_host_result =
      std::any_of(results.begin(), results.end(), [](const auto& value) {
        return std::holds_alternative<detail::HostVal>(value);
      });
  if (!has_host_input && !has_host_result) {
    return true;
  }

  if (arguments.size() != fn.inputs().size()) {
    return false;
  }
  std::vector<Type> value_arguments;
  std::vector<std::optional<ParamVal>> known_arguments;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (detail::is_value_port(fn.inputs()[index])) {
      const auto* host = std::get_if<detail::HostVal>(&arguments[index]);
      if (host == nullptr || !host->concrete_type) {
        state_->diagnostics.report(
            "compiler fn IR input has no concrete Joggle type");
        return false;
      }
      value_arguments.push_back(*host->concrete_type);
      continue;
    }
    known_arguments.push_back(detail::parameter_value(arguments[index]));
  }

  std::vector<std::optional<Type>> expected_results;
  expected_results.reserve(detail::value_results(fn).size());
  for (std::size_t index = 0; index < fn.results().size(); ++index) {
    if (!detail::is_value_port(fn.results()[index])) {
      continue;
    }
    const auto* host = results.empty()
                           ? nullptr
                           : std::get_if<detail::HostVal>(&results[index]);
    expected_results.push_back(host == nullptr ? std::optional<Type>{}
                                               : host->concrete_type);
  }
  return detail::resolve_call_types(*this, fn, value_arguments, known_arguments,
                                    expected_results, state_->diagnostics)
      .has_value();
}

bool Compiler::check_binding_signature(
    const Mod::FnDecl& schema, std::span<const std::string_view> inputs,
    std::span<const std::string_view> results) {
  const bool input_match =
      schema.inputs().size() == inputs.size() &&
      std::equal(schema.inputs().begin(), schema.inputs().end(), inputs.begin(),
                 [&](const auto& field, auto type) {
                   return accepts_host_type(schema, field, type);
                 });
  const bool result_match =
      schema.results().size() == results.size() &&
      std::equal(schema.results().begin(), schema.results().end(),
                 results.begin(), [&](const auto& field, auto type) {
                   return accepts_host_type(schema, field, type);
                 });
  if (!input_match || !result_match) {
    state_->diagnostics.report("C++ binding for fn '" +
                               schema.symbol().qualified_name() +
                               "' does not match its declared type");
    return false;
  }
  return true;
}

std::optional<Mod::FnDecl>
Compiler::lookup_binding(const Mod& mod, std::string_view name,
                         std::span<const std::string_view> inputs,
                         std::span<const std::string_view> results) {
  const auto scope = lookup_mod(mod);
  if (!scope) {
    return std::nullopt;
  }
  return resolve_host_overload(*scope, name, inputs, results, "C++ binding");
}

std::optional<Mod::FnDecl>
Compiler::resolve_host_overload(const Mod& mod, std::string_view name,
                                std::span<const std::string_view> inputs,
                                std::span<const std::string_view> results,
                                std::string_view purpose) {
  const auto overloads = mod.overloads(name);
  if (overloads.empty()) {
    state_->diagnostics.report("mod '" + std::string(mod.name()) +
                               "' has no fn named '" + std::string(name) + "'");
    return std::nullopt;
  }
  std::optional<Mod::FnDecl> match;
  for (const Mod::FnDecl& candidate : overloads) {
    if (!matches_run_signature(candidate, inputs, results)) {
      continue;
    }
    if (match) {
      state_->diagnostics.report(
          std::string(purpose) + " is ambiguous for overloaded fn '" +
          std::string(mod.name()) + "." + std::string(name) + "'");
      return std::nullopt;
    }
    match = candidate;
  }
  if (!match) {
    state_->diagnostics.report("no overload of fn '" + std::string(mod.name()) +
                               "." + std::string(name) + "' matches the " +
                               std::string(purpose));
  }
  return match;
}

std::optional<Mod::FnDecl>
Compiler::lookup_run(std::string_view name,
                     std::span<const std::string_view> inputs,
                     std::span<const std::string_view> results) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot run a compiler fn before the compiler is linked");
    return std::nullopt;
  }
  const auto member = qualified_member(name);
  if (!member) {
    state_->diagnostics.report("fn name '" + std::string(name) +
                               "' must be qualified as mod.member");
    return std::nullopt;
  }
  const auto owner = state_->mods.find(member->first);
  if (owner == state_->mods.end()) {
    state_->diagnostics.report("fn '" + std::string(name) +
                               "' names an unlinked mod");
    return std::nullopt;
  }
  return resolve_host_overload(owner->second, member->second, inputs, results,
                               "C++ invocation");
}

void Compiler::bind_native(Mod::FnDecl schema, NativeFn fn,
                           HostEval evaluation) {
  const Mod::Symbol symbol = schema.symbol();
  const auto owner = state_->mods.find(symbol.mod_name());
  if (owner == state_->mods.end() ||
      owner->second.version() != symbol.mod_version() ||
      owner->second.declaration_digest() != symbol.declaration_digest()) {
    state_->diagnostics.report("cannot bind compiler fn '" +
                               symbol.qualified_name() +
                               "' outside this compiler");
    return;
  }
  if (schema.form() != Mod::FnDecl::Form::External) {
    state_->diagnostics.report("text-defined compiler fn '" +
                               symbol.qualified_name() +
                               "' cannot receive a C++ binding");
    return;
  }
  if (!fn) {
    state_->diagnostics.report("compiler-fn binding is empty");
    return;
  }
  if (!state_->bindings
           .emplace(symbol.stable_name(),
                    State::BoundFn{std::move(fn), evaluation})
           .second) {
    state_->diagnostics.report("compiler fn '" + symbol.qualified_name() +
                               "' already has a binding");
  }
}

void Compiler::bind_prelude_mod() {
  const auto found = state_->mods.find(detail::prelude_mod_name);
  const auto mod = found == state_->mods.end() ? std::optional<Mod::TypeDecl>{}
                                               : found->second.type("mod");
  if (!mod) {
    state_->diagnostics.report("Prelude does not declare type 'mod'");
    return;
  }
  const std::string cpp_type(detail::host_type_name<Mod>());
  const auto projector = [](Compiler& compiler,
                            const Mod::TypeDecl& declaration,
                            const void*) { return compiler.make(declaration); };
  state_->host_types.emplace(cpp_type,
                             State::HostRepresentation{*mod, projector});
  state_->host_representations.emplace(mod->symbol().stable_name(), cpp_type);
}

void Compiler::bind_prelude_primitives() {
  const auto found = state_->mods.find(detail::prelude_mod_name);
  if (found == state_->mods.end()) {
    return;
  }
  for (const Mod::FnDecl& fn : found->second.fns()) {
    if (!detail::is_prelude_primitive(fn)) {
      continue;
    }
    NativeFn implementation =
        [fn](Compiler& compiler, std::span<detail::ExecVal> arguments,
             Diag& diagnostics) -> std::optional<detail::ExecVals> {
      std::vector<detail::ParamVal> values;
      values.reserve(arguments.size());
      for (const auto& argument : arguments) {
        auto value = detail::parameter_value(argument);
        if (!value) {
          diagnostics.report("Prelude primitive '" +
                             fn.symbol().qualified_name() +
                             "' received an unsupported value");
          return std::nullopt;
        }
        values.push_back(std::move(*value));
      }
      auto result = detail::evaluate_prelude_primitive(
          fn, values, diagnostics, compiler.evaluation_limits().steps);
      if (!result || fn.results().size() != 1U) {
        return std::nullopt;
      }
      auto encoded = detail::exec_val(*result, fn.results().front());
      if (!encoded) {
        diagnostics.report("Prelude primitive '" +
                           fn.symbol().qualified_name() +
                           "' produced an unsupported value");
        return std::nullopt;
      }
      detail::ExecVals results;
      results.push_back(std::move(*encoded));
      return results;
    };
    bind_native(fn, std::move(implementation), HostEval::Hermetic);
  }
}

bool Compiler::can_evaluate_binding(const Mod::FnDecl& fn,
                                    bool under_residual_control) const {
  if (fn.form() == Mod::FnDecl::Form::Body) {
    return true;
  }
  const auto binding = state_->bindings.find(fn.symbol().stable_name());
  return binding != state_->bindings.end() &&
         (!under_residual_control ||
          binding->second.evaluation == HostEval::Hermetic);
}

std::optional<detail::ParamVal>
Compiler::evaluate_binding(Mod::FnDecl fn,
                           std::span<const detail::ParamVal> arguments,
                           bool under_residual_control) {
  if (!detail::value_inputs(fn).empty() || !detail::value_results(fn).empty() ||
      detail::compiler_inputs(fn).size() != arguments.size() ||
      detail::compiler_results(fn).size() != 1U) {
    state_->diagnostics.report("fn '" + fn.symbol().qualified_name() +
                               "' cannot be evaluated from Known values");
    return std::nullopt;
  }
  std::optional<std::string> cache_key;
  if (fn.form() == Mod::FnDecl::Form::External) {
    const auto binding = state_->bindings.find(fn.symbol().stable_name());
    if (binding != state_->bindings.end() &&
        binding->second.evaluation == HostEval::Hermetic) {
      cache_key = fn.symbol().stable_name();
      for (const auto& argument : arguments) {
        const std::string value = argument.canonical();
        *cache_key += "/" + std::to_string(value.size()) + ":" + value;
      }
      const auto cached = state_->hermetic_evaluations.find(*cache_key);
      if (cached != state_->hermetic_evaluations.end()) {
        return cached->second;
      }
    }
  }
  std::vector<detail::ExecVal> values;
  values.reserve(arguments.size());
  const auto parameters = detail::compiler_inputs(fn);
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    auto converted = detail::exec_val(arguments[index], parameters[index]);
    if (!converted) {
      state_->diagnostics.report(
          "compiler execution cannot represent argument '" +
          parameters[index].name + "'");
      return std::nullopt;
    }
    values.push_back(std::move(*converted));
  }
  auto produced = execute(fn, std::move(values), under_residual_control);
  if (!produced || produced->size() != 1U) {
    return std::nullopt;
  }
  auto result = detail::parameter_value(produced->front());
  if (!result || !detail::matches_parameter(
                     detail::compiler_results(fn).front(), *result)) {
    state_->diagnostics.report("compiler execution of fn '" +
                               fn.symbol().qualified_name() +
                               "' produced a value with the wrong type");
    return std::nullopt;
  }
  if (cache_key) {
    state_->hermetic_evaluations.emplace(std::move(*cache_key), *result);
  }
  return result;
}

std::optional<Mod> Compiler::lookup_mod(const Mod& mod) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot bind native before the compiler is linked");
    return std::nullopt;
  }
  const auto found = state_->mods.find(mod.name());
  if (found == state_->mods.end() || found->second != mod) {
    state_->diagnostics.report("mod '" + mod_identity(mod) +
                               "' is not part of this compiler");
    return std::nullopt;
  }
  return found->second;
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
    const Mod::FnDecl schema = op.callee();
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

bool Compiler::check_run_signature(const Mod::FnDecl& schema,
                                   std::span<const std::string_view> inputs,
                                   std::span<const std::string_view> results) {
  if (matches_run_signature(schema, inputs, results)) {
    return true;
  }
  state_->diagnostics.report("invocation of fn '" +
                             schema.symbol().qualified_name() +
                             "' does not match its declared type");
  return false;
}

bool Compiler::matches_run_signature(
    const Mod::FnDecl& schema, std::span<const std::string_view> inputs,
    std::span<const std::string_view> results) const {
  if (!state_->linked) {
    return false;
  }
  const Mod::Symbol symbol = schema.symbol();
  const auto owner = state_->mods.find(symbol.mod_name());
  if (owner == state_->mods.end() ||
      owner->second.version() != symbol.mod_version() ||
      owner->second.declaration_digest() != symbol.declaration_digest()) {
    return false;
  }
  const bool input_match =
      schema.inputs().size() == inputs.size() &&
      std::equal(schema.inputs().begin(), schema.inputs().end(), inputs.begin(),
                 [&](const auto& field, auto type) {
                   return accepts_host_type(schema, field, type);
                 });
  const bool result_match =
      schema.results().size() == results.size() &&
      std::equal(schema.results().begin(), schema.results().end(),
                 results.begin(), [&](const auto& field, auto type) {
                   return accepts_host_type(schema, field, type);
                 });
  return input_match && result_match;
}

std::optional<Mod::FnDecl> Compiler::lookup(std::string_view name) {
  if (!state_->linked) {
    state_->diagnostics.report("cannot look up a fn before the compiler "
                               "is linked");
    return std::nullopt;
  }
  const auto member = qualified_member(name);
  if (!member) {
    state_->diagnostics.report("fn name '" + std::string(name) +
                               "' must be qualified as mod.member");
    return std::nullopt;
  }
  const auto owner = state_->mods.find(member->first);
  if (owner == state_->mods.end()) {
    state_->diagnostics.report("fn '" + std::string(name) +
                               "' names an unlinked mod");
    return std::nullopt;
  }
  const auto declaration = owner->second.fn(member->second);
  if (!declaration) {
    state_->diagnostics.report("unknown or overloaded fn '" +
                               std::string(name) + "'");
  }
  return declaration;
}

std::optional<detail::ExecVals>
Compiler::execute(Mod::FnDecl declaration,
                  std::vector<detail::ExecVal> arguments,
                  bool under_residual_control) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot run a compiler fn before the compiler is linked");
    return std::nullopt;
  }
  const std::size_t before = state_->diagnostics.size();
  std::vector<Fn::Revision> verified_fns;
  const auto verify_fn = [&](const Fn& fn) {
    const auto revision = fn.revision();
    if (std::find(verified_fns.begin(), verified_fns.end(), revision) !=
        verified_fns.end()) {
      return true;
    }
    if (!verify(fn)) {
      return false;
    }
    verified_fns.push_back(revision);
    return true;
  };
  const auto verify_values = [&](std::span<const detail::ExecVal> values) {
    for (const detail::ExecVal& value : values) {
      if (const auto* fn = std::get_if<std::shared_ptr<Fn>>(&value)) {
        if (!*fn || !verify_fn(**fn)) {
          return false;
        }
        continue;
      }
      const auto* host = std::get_if<detail::HostVal>(&value);
      if (host == nullptr || host->cpp_type != detail::host_type_name<Mod>()) {
        continue;
      }
      if (!host->storage) {
        state_->diagnostics.report("Mod value has no storage");
        return false;
      }
      const auto& mod = *static_cast<const Mod*>(host->storage.get());
      for (const Mod::FnDecl& member : mod.fns()) {
        const Fn* body = member.body();
        if (body != nullptr && !verify_fn(*body)) {
          state_->diagnostics.report("Mod fn '" + std::string(member.name()) +
                                     "' is invalid");
          return false;
        }
      }
    }
    return true;
  };
  std::size_t steps = 0;
  std::size_t depth = 0;
  const auto execute = [&](const auto& self, const Mod::FnDecl& current,
                           std::vector<detail::ExecVal> values)
      -> std::optional<detail::ExecVals> {
    if (depth >= state_->evaluation_limits.depth) {
      state_->diagnostics.report(
          "compiler execution nesting limit exceeded in '" +
          current.symbol().qualified_name() + "'");
      return std::nullopt;
    }
    ++depth;
    struct DepthGuard {
      std::size_t& value;
      ~DepthGuard() { --value; }
    } depth_guard{depth};
    if (values.size() != current.inputs().size()) {
      state_->diagnostics.report("compiler fn '" +
                                 current.symbol().qualified_name() +
                                 "' received the wrong argument count");
      return std::nullopt;
    }
    for (auto& value : values) {
      if (!project_host_value(value)) {
        return std::nullopt;
      }
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (!accepts_host_type(current, current.inputs()[index],
                             detail::exec_val_type(values[index]))) {
        state_->diagnostics.report(
            "compiler fn '" + current.symbol().qualified_name() +
            "' received an argument with the wrong type");
        return std::nullopt;
      }
      if (detail::exec_val_type(values[index]) == typeid(Fn).name()) {
        const auto fn = std::get<std::shared_ptr<Fn>>(values[index]);
        if (!fn->accepts(current.symbol())) {
          state_->diagnostics.report("compiler fn '" +
                                     current.symbol().qualified_name() +
                                     "' is outside the fn's mod closure");
          return std::nullopt;
        }
      }
    }
    if (!check_host_values(current, values)) {
      return std::nullopt;
    }
    if (!verify_values(values)) {
      return std::nullopt;
    }
    switch (current.form()) {
    case Mod::FnDecl::Form::External: {
      const auto binding =
          state_->bindings.find(current.symbol().stable_name());
      if (binding == state_->bindings.end()) {
        state_->diagnostics.report("compiler fn '" +
                                   current.symbol().qualified_name() +
                                   "' has no C++ binding");
        return std::nullopt;
      }
      if (under_residual_control &&
          binding->second.evaluation != HostEval::Hermetic) {
        state_->diagnostics.report(
            "host implementation of fn '" + current.symbol().qualified_name() +
            "' is guarded and cannot execute under Residual control");
        return std::nullopt;
      }
      const std::size_t call_diagnostics = state_->diagnostics.size();
      std::optional<detail::ExecVals> execution;
      try {
        execution =
            binding->second.callable(*this, values, state_->diagnostics);
      } catch (const std::bad_variant_access&) {
        state_->diagnostics.report(
            "C++ binding for compiler fn '" +
            current.symbol().qualified_name() +
            "' disagrees with its registered host representation");
        return std::nullopt;
      } catch (const std::exception& exception) {
        state_->diagnostics.report("C++ binding for compiler fn '" +
                                   current.symbol().qualified_name() +
                                   "' threw: " + exception.what());
        return std::nullopt;
      } catch (...) {
        state_->diagnostics.report("C++ binding for compiler fn '" +
                                   current.symbol().qualified_name() +
                                   "' threw an unknown exception");
        return std::nullopt;
      }
      if (!execution) {
        if (state_->diagnostics.size() == call_diagnostics) {
          state_->diagnostics.report(
              "compiler fn '" + current.symbol().qualified_name() + "' failed");
        }
        return std::nullopt;
      }
      if (state_->diagnostics.size() != call_diagnostics) {
        return std::nullopt;
      }
      if (execution->size() != current.results().size()) {
        state_->diagnostics.report("compiler fn '" +
                                   current.symbol().qualified_name() +
                                   "' produced the wrong number of values");
        return std::nullopt;
      }
      for (std::size_t index = 0; index < execution->size(); ++index) {
        if (!project_host_value((*execution)[index]) ||
            !accepts_host_type(current, current.results()[index],
                               detail::exec_val_type((*execution)[index]))) {
          state_->diagnostics.report("compiler fn '" +
                                     current.symbol().qualified_name() +
                                     "' produced a value with the wrong type");
          return std::nullopt;
        }
      }
      if (!check_host_values(current, values, *execution)) {
        return std::nullopt;
      }
      if (!verify_values(*execution)) {
        return std::nullopt;
      }
      return execution;
    }
    case Mod::FnDecl::Form::Body: {
      const auto owner = state_->mods.find(current.symbol().mod_name());
      const auto body = owner == state_->mods.end()
                            ? std::shared_ptr<const detail::FnBody>{}
                            : detail::ModAccess::body(owner->second, current);
      if (!body) {
        state_->diagnostics.report("compiler fn '" +
                                   current.symbol().qualified_name() +
                                   "' has no executable body");
        return std::nullopt;
      }
      const detail::ExecuteFn invoke =
          [&](Mod::FnDecl fn, std::vector<detail::ExecVal> arguments,
              Loc call_site) {
            const std::size_t call_diagnostics = state_->diagnostics.size();
            auto result = self(self, fn, std::move(arguments));
            if (!result && state_->diagnostics.size() > call_diagnostics) {
              std::string note = "while calling '" +
                                 fn.symbol().qualified_name() + "' from '" +
                                 current.symbol().qualified_name() + "'";
              if (!call_site.source.empty()) {
                note += " at " + call_site.source + ":" +
                        std::to_string(call_site.begin.line) + ":" +
                        std::to_string(call_site.begin.column);
              }
              detail::DiagAccess::note_since(state_->diagnostics,
                                             call_diagnostics, std::move(note));
            }
            return result;
          };
      auto evaluated = detail::execute_body(
          *this, current, *body, values, state_->evaluation_limits, steps,
          under_residual_control, state_->diagnostics, invoke);
      if (!evaluated) {
        return std::nullopt;
      }
      if (evaluated->size() != current.results().size()) {
        state_->diagnostics.report("compiler fn '" +
                                   current.symbol().qualified_name() +
                                   "' returned the wrong number of values");
        return std::nullopt;
      }
      for (std::size_t index = 0; index < evaluated->size(); ++index) {
        if (!project_host_value((*evaluated)[index]) ||
            !accepts_host_type(current, current.results()[index],
                               detail::exec_val_type((*evaluated)[index]))) {
          state_->diagnostics.report("compiler fn '" +
                                     current.symbol().qualified_name() +
                                     "' returned a value with the wrong type");
          return std::nullopt;
        }
      }
      if (!check_host_values(current, values, *evaluated)) {
        return std::nullopt;
      }
      if (!verify_values(*evaluated)) {
        return std::nullopt;
      }
      return evaluated;
    }
    }
    return std::nullopt;
  };

  auto result = execute(execute, declaration, std::move(arguments));
  bool valid = result.has_value() && state_->diagnostics.size() == before;
  if (!valid) {
    return std::nullopt;
  }
  return result;
}

const Diag& Compiler::diag() const { return state_->diagnostics; }

}  // namespace joggle
