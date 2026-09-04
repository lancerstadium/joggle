#include "compile/compiler.h"

#include "base/diag.h"
#include "compile/eval.h"
#include "ir/fn.h"
#include "ir/mod.h"

#include <algorithm>
#include <exception>
#include <memory>
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

}  // namespace

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

}  // namespace joggle
