#include "compile/compiler.h"

#include "compile/eval.h"
#include "ir/mod.h"
#include "ir/type.h"
#include "lang/fn.h"
#include "lang/prelude.h"
#include "sema/call.h"
#include "sema/domain.h"
#include "sema/infer.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

namespace joggle {
namespace {

using detail::ParamVal;
using detail::belongs_to;

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

using detail::mod_identity;

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

}  // namespace joggle
