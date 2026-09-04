#include "compile/compiler.h"

#include "ir/mod.h"
#include "joggle/detail/native.h"
#include "lang/fn.h"
#include "lang/prelude.h"
#include "pkg/repo.h"
#include "sema/call.h"
#include "sema/check.h"
#include "sema/domain.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace joggle {
namespace {

std::string_view trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

std::optional<Version> parse_exact_version(std::string_view text) {
  Version result;
  std::array<std::uint32_t*, 3> components{&result.major, &result.minor,
                                           &result.patch};
  for (std::size_t index = 0; index < components.size(); ++index) {
    const std::size_t separator = text.find('.');
    const std::string_view component =
        separator == std::string_view::npos ? text : text.substr(0, separator);
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(
        component.data(), component.data() + component.size(), value);
    if (component.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != component.data() + component.size() ||
        value > std::numeric_limits<std::uint32_t>::max()) {
      return std::nullopt;
    }
    *components[index] = static_cast<std::uint32_t>(value);
    if (index + 1U == components.size()) {
      if (separator != std::string_view::npos) {
        return std::nullopt;
      }
    } else {
      if (separator == std::string_view::npos) {
        return std::nullopt;
      }
      text.remove_prefix(separator + 1U);
    }
  }
  return result;
}

bool identifier(std::string_view text) {
  if (text.empty() ||
      (std::isalpha(static_cast<unsigned char>(text.front())) == 0 &&
       text.front() != '_')) {
    return false;
  }
  return std::all_of(text.begin() + 1, text.end(), [](const char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == '_';
  });
}

bool target_identifier(std::string_view text) {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(), [](const char character) {
           return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
                  character == '_' || character == '-';
         });
}

bool digest(std::string_view text) {
  return text.size() == 64U &&
         std::all_of(text.begin(), text.end(), [](const char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

}  // namespace

using detail::native_key;

void Compiler::add(std::string_view text, std::string source) {
  if (state_->linked) {
    state_->diagnostics.report(
        "cannot add a mod after the compiler has been linked");
    return;
  }
  auto parsed = parse_mod(text, state_->diagnostics, std::move(source));
  if (!parsed) {
    return;
  }

  add_mod(std::move(*parsed), true, std::nullopt);
}

void Compiler::add(Mod mod) {
  if (state_->linked) {
    state_->diagnostics.report(
        "cannot add a mod after the compiler has been linked");
    return;
  }
  add_mod(std::move(mod), true, std::nullopt);
}

void Compiler::add_mod(Mod mod, bool explicit_mod,
                       std::optional<std::filesystem::path> source) {
  const std::string name(mod.name());
  if (explicit_mod) {
    state_->explicit_mods.insert(name);
  }
  const auto found = state_->mods.find(name);
  if (found == state_->mods.end()) {
    state_->mods.emplace(name, std::move(mod));
    if (source) {
      state_->mod_sources.emplace(name, std::move(*source));
    }
    return;
  }
  if (found->second.version() == mod.version() &&
      found->second.digest() == mod.digest()) {
    if (source) {
      state_->mod_sources.insert_or_assign(name, std::move(*source));
    }
    return;
  }
  state_->diagnostics.report("mod '" + name +
                                 "' was loaded with conflicting identities",
                             std::nullopt);
}

void Compiler::load(const std::filesystem::path& path) {
  if (state_->linked) {
    state_->diagnostics.report(
        "cannot add a mod after the compiler has been linked");
    return;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    state_->diagnostics.report("cannot open mod '" + path.string() + "'");
    return;
  }
  std::ostringstream text;
  text << input.rdbuf();
  if (!input.eof() && input.fail()) {
    state_->diagnostics.report("cannot read mod '" + path.string() + "'");
    return;
  }
  auto parsed = parse_mod(text.str(), state_->diagnostics, path.string());
  if (parsed) {
    add_mod(std::move(*parsed), true,
            std::filesystem::absolute(path).lexically_normal());
  }
}

void Compiler::search(std::filesystem::path root) {
  if (state_->linked) {
    state_->diagnostics.report(
        "cannot add a mod search path after the compiler is linked");
    return;
  }
  root = root.lexically_normal();
  if (std::find(state_->search_paths.begin(), state_->search_paths.end(),
                root) == state_->search_paths.end()) {
    state_->search_paths.push_back(std::move(root));
  }
}

void Compiler::lock(const std::filesystem::path& path) {
  if (state_->linked) {
    state_->diagnostics.report(
        "cannot load a lock file after the compiler is linked");
    return;
  }
  if (state_->has_lock) {
    state_->diagnostics.report("a lock file is already loaded");
    return;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    state_->diagnostics.report("cannot open lock file '" + path.string() + "'");
    return;
  }

  std::map<std::string, State::LockedIdentity, std::less<>> locked;
  std::map<std::string, State::LockedNative, std::less<>> locked_native;
  std::size_t roots = 0;
  std::size_t line_number = 0;
  bool header = false;
  std::string line;
  const std::size_t before = state_->diagnostics.size();
  while (std::getline(input, line)) {
    ++line_number;
    std::string_view text = trim(line);
    if (text.empty()) {
      continue;
    }
    const auto report = [&](std::string message) {
      state_->diagnostics.report(std::move(message),
                                 Loc{path.string(),
                                     {line_number, 1},
                                     {line_number, line.size() + 1U}});
    };
    if (!header) {
      if (text != "joggle-lock 1;") {
        report("expected 'joggle-lock 1;'");
      }
      header = true;
      continue;
    }
    if (!text.ends_with(';')) {
      report("lock entry must end with ';'");
      continue;
    }
    text.remove_suffix(1);
    const std::size_t space = text.find(' ');
    if (space == std::string_view::npos) {
      report("invalid lock entry");
      continue;
    }
    const std::string_view kind = text.substr(0, space);
    const bool is_root = kind == "root";
    const bool is_native = kind == "native";
    if (!is_root && kind != "mod" && !is_native) {
      report("expected root, mod, or native lock entry");
      continue;
    }
    text.remove_prefix(space + 1U);
    std::string_view mod_text = text;
    std::string_view native_text;
    if (is_native) {
      const std::size_t separator = text.find(' ');
      if (separator == std::string_view::npos) {
        report("native lock entry needs target#digest");
        continue;
      }
      mod_text = text.substr(0, separator);
      native_text = text.substr(separator + 1U);
    }
    const std::size_t at = mod_text.find('@');
    const std::size_t hash = mod_text.find('#');
    if (at == std::string_view::npos || hash == std::string_view::npos ||
        at >= hash) {
      report("expected name@version#digest");
      continue;
    }
    const std::string_view name = mod_text.substr(0, at);
    const auto version =
        parse_exact_version(mod_text.substr(at + 1U, hash - at - 1U));
    const std::string_view mod_digest = mod_text.substr(hash + 1U);
    if (!identifier(name) || !version || !digest(mod_digest)) {
      report("invalid locked mod identity");
      continue;
    }
    if (is_native) {
      const std::size_t native_hash = native_text.find('#');
      if (native_hash == std::string_view::npos) {
        report("expected target#digest for native");
        continue;
      }
      const std::string_view target = native_text.substr(0, native_hash);
      const std::string_view native_digest =
          native_text.substr(native_hash + 1U);
      if (!target_identifier(target) || !digest(native_digest)) {
        report("invalid locked native identity");
        continue;
      }
      if (!locked_native
               .emplace(native_key(name, target),
                        State::LockedNative{*version, std::string(mod_digest),
                                            std::string(target),
                                            std::string(native_digest)})
               .second) {
        report("duplicate locked native for '" + std::string(name) +
               "' and target '" + std::string(target) + "'");
      }
      continue;
    }
    if (!locked
             .emplace(std::string(name),
                      State::LockedIdentity{*version, std::string(mod_digest),
                                            is_root})
             .second) {
      report("duplicate locked mod '" + std::string(name) + "'");
      continue;
    }
    if (is_root) {
      ++roots;
    }
  }
  if (!input.eof() && input.fail()) {
    state_->diagnostics.report("cannot read lock file '" + path.string() + "'");
  }
  if (!header) {
    state_->diagnostics.report("lock file is empty");
  }
  if (roots != 1U) {
    state_->diagnostics.report("lock file must contain exactly one root mod");
  }
  if (state_->diagnostics.size() != before) {
    return;
  }
  state_->locked_mods = std::move(locked);
  state_->locked_natives = std::move(locked_native);
  state_->has_lock = true;
}

bool Compiler::link() {
  if (state_->linked) {
    return state_->diagnostics.ok();
  }
  if (!state_->diagnostics.ok()) {
    return false;
  }

  bool loaded = true;
  while (loaded) {
    loaded = false;
    struct MissingImport {
      Mod::Import import;
      std::optional<Loc> source;
    };
    std::vector<MissingImport> missing;
    for (const auto& [name, mod] : state_->mods) {
      static_cast<void>(name);
      for (std::size_t index = 0; index < mod.imports().size(); ++index) {
        const Mod::Import& import = mod.imports()[index];
        if (state_->mods.find(import.name) == state_->mods.end()) {
          missing.push_back(
              {import, detail::ModAccess::import_source(mod, index)});
        }
      }
    }
    for (const MissingImport& pending : missing) {
      const Mod::Import& import = pending.import;
      if (state_->mods.find(import.name) != state_->mods.end() ||
          state_->search_paths.empty()) {
        continue;
      }
      std::optional<detail::InstalledMod> resolved;
      if (state_->has_lock) {
        const auto locked = state_->locked_mods.find(import.name);
        if (locked == state_->locked_mods.end()) {
          const std::string message =
              "mod '" + import.name + "' is missing from the lock file";
          state_->diagnostics.report(message, pending.source);
          continue;
        }
        if (!import.version.contains(locked->second.version)) {
          const std::string message = "locked mod '" + import.name + "@" +
                                      to_string(locked->second.version) +
                                      "' does not satisfy import " +
                                      to_string(import.version);
          state_->diagnostics.report(message, pending.source);
          continue;
        }
        resolved = detail::resolve_mod(
            state_->search_paths, import.name, locked->second.version,
            locked->second.digest, state_->diagnostics);
        if (!resolved) {
          const std::string message = "locked mod '" + import.name + "@" +
                                      to_string(locked->second.version) + "#" +
                                      locked->second.digest +
                                      "' is not installed";
          state_->diagnostics.report(message, pending.source);
        }
      } else {
        resolved = detail::resolve_mod(state_->search_paths, import.name,
                                       import.version, state_->diagnostics);
      }
      if (!resolved) {
        continue;
      }
      add_mod(std::move(resolved->mod), false,
              std::filesystem::absolute(resolved->source).lexically_normal());
      loaded = true;
    }
    if (!state_->diagnostics.ok()) {
      return false;
    }
  }

  if (state_->has_lock) {
    for (const auto& [name, mod] : state_->mods) {
      if (name == detail::prelude_mod_name) {
        continue;
      }
      const auto locked = state_->locked_mods.find(name);
      if (locked == state_->locked_mods.end()) {
        state_->diagnostics.report("loaded mod '" + name +
                                   "' is absent from the lock file");
        continue;
      }
      if (locked->second.version != mod.version() ||
          locked->second.digest != mod.digest()) {
        state_->diagnostics.report("loaded mod '" + name +
                                   "' does not match its locked identity");
      }
      if (locked->second.root &&
          state_->explicit_mods.find(name) == state_->explicit_mods.end()) {
        state_->diagnostics.report("locked root mod '" + name +
                                   "' was not loaded explicitly");
      }
    }
    for (const auto& [name, identity] : state_->locked_mods) {
      static_cast<void>(identity);
      if (state_->mods.find(name) == state_->mods.end()) {
        state_->diagnostics.report("locked mod '" + name +
                                   "' is not part of the resolved closure");
      }
    }
    for (const auto& [key, native] : state_->locked_natives) {
      const std::size_t separator = key.find('\n');
      const std::string name = key.substr(0, separator);
      const auto mod = state_->mods.find(name);
      if (mod == state_->mods.end() ||
          mod->second.version() != native.mod_version ||
          mod->second.digest() != native.mod_digest) {
        state_->diagnostics.report(
            "locked native references a different mod identity for '" + name +
            "'");
        continue;
      }
      if (native.target != detail::native_target) {
        continue;
      }

      std::vector<std::filesystem::path> candidates;
      const auto source = state_->mod_sources.find(name);
      if (source != state_->mod_sources.end()) {
        candidates =
            detail::native_candidates(source->second, state_->diagnostics);
      }
      if (candidates.empty() && !state_->search_paths.empty()) {
        auto installed =
            detail::resolve_mod(state_->search_paths, name, native.mod_version,
                                native.mod_digest, state_->diagnostics);
        if (installed) {
          candidates =
              detail::native_candidates(installed->source, state_->diagnostics);
          if (!candidates.empty()) {
            state_->mod_sources.insert_or_assign(
                name, std::filesystem::absolute(installed->source)
                          .lexically_normal());
          }
        }
      }
      bool found = false;
      for (const auto& candidate : candidates) {
        const auto candidate_digest =
            detail::native_digest(candidate, state_->diagnostics);
        if (candidate_digest && *candidate_digest == native.digest) {
          found = true;
          break;
        }
      }
      if (!found) {
        state_->diagnostics.report(
            "locked native for '" + name + "' and target '" + native.target +
            "' is not installed with digest " + native.digest);
      }
    }
    if (!state_->diagnostics.ok()) {
      return false;
    }
  }

  for (const auto& [name, mod] : state_->mods) {
    for (std::size_t index = 0; index < mod.imports().size(); ++index) {
      const Mod::Import& import = mod.imports()[index];
      const auto source = detail::ModAccess::import_source(mod, index);
      const auto dependency = state_->mods.find(import.name);
      if (dependency == state_->mods.end()) {
        state_->diagnostics.report("mod '" + name + "' imports missing mod '" +
                                       import.name + "'",
                                   source);
        continue;
      }
      if (!import.version.contains(dependency->second.version())) {
        state_->diagnostics.report(
            "mod '" + name + "' imports '" + import.name + "@" +
                to_string(import.version) + "', but version " +
                to_string(dependency->second.version()) + " was loaded",
            source);
      }
    }

    for (const Mod::TypeDecl& type : mod.types()) {
      const auto location = detail::ModAccess::declaration_source(
          mod, Mod::Symbol::Kind::Type, type.name());
      for (const auto& derived : type.derived_parameters()) {
        detail::check_declaration_expression(
            *this, mod, derived.value, derived.domain, {}, type.parameters(),
            state_->diagnostics, location,
            "derived field '" + name + "." + std::string(type.name()) + "." +
                derived.name + "'");
      }
    }

    for (const Mod::FnDecl& fn : mod.fns()) {
      const auto location = detail::ModAccess::declaration_source(
          mod, Mod::Symbol::Kind::Fn, fn.name());
      const auto body = detail::ModAccess::body(mod, fn);
      if (body) {
        detail::verify_body_calls(*this, fn, *body, state_->diagnostics);
      }
    }

    for (const Mod::FnDecl& fn : mod.fns()) {
      if (detail::ModAccess::expression(fn) == nullptr ||
          !detail::value_inputs(fn).empty() ||
          !detail::value_results(fn).empty() ||
          detail::compiler_results(fn).size() != 1U) {
        continue;
      }
      const auto location = detail::ModAccess::declaration_source(
          mod, Mod::Symbol::Kind::Fn, fn.name());
      const auto inputs = detail::compiler_inputs(fn);
      const auto results = detail::compiler_results(fn);
      detail::check_declaration_expression(
          *this, mod, *detail::ModAccess::expression(fn),
          results.front().domain, fn.generics(), inputs, state_->diagnostics,
          location, "fn '" + name + "." + std::string(fn.name()) + "'");
    }
    for (const Mod::FnDecl& declaration : mod.fns()) {
      if (detail::value_inputs(declaration).empty() &&
          detail::value_results(declaration).empty()) {
        continue;
      }
      const auto fn_source = detail::ModAccess::declaration_source(
          mod, Mod::Symbol::Kind::Fn, declaration.name());
      const auto report_fn = [&](std::string message) {
        state_->diagnostics.report(std::move(message), fn_source);
      };
      const auto& contract = detail::FnTypeAccess::get(declaration);
      if (!contract.bindings.empty() &&
          contract.bindings.size() != declaration.inputs().size()) {
        report_fn("fn '" + name + "." + std::string(declaration.name()) +
                  "' has an invalid type contract");
        continue;
      }
      const auto type_domain = detail::domain_expression(detail::ValKind::Type);
      const std::string subject =
          "fn '" + name + "." + std::string(declaration.name()) + "'";
      for (const auto& input : detail::value_inputs(declaration)) {
        detail::check_declaration_expression(
            *this, mod, input.domain, type_domain, contract.generics, {},
            state_->diagnostics, fn_source, subject);
      }
      for (std::size_t index = 0; index < declaration.inputs().size();
           ++index) {
        if (!contract.bindings.empty() && contract.bindings[index]) {
          detail::check_declaration_expression(
              *this, mod, *contract.bindings[index],
              declaration.inputs()[index].domain, contract.generics, {},
              state_->diagnostics, fn_source, subject);
        }
      }
      for (const auto& result : detail::value_results(declaration)) {
        detail::check_declaration_expression(
            *this, mod, result.domain, type_domain, contract.generics, {},
            state_->diagnostics, fn_source, subject);
      }
    }
  }
  if (!state_->diagnostics.ok()) {
    return false;
  }

  enum class Visit { Unseen, Active, Complete };
  std::unordered_map<std::string, Visit> visits;
  std::vector<std::string> stack;
  const auto visit = [&](const auto& self, const Mod& mod,
                         std::optional<Loc> incoming) -> bool {
    const std::string name(mod.name());
    Visit& state = visits[name];
    if (state == Visit::Complete) {
      return true;
    }
    if (state == Visit::Active) {
      std::string cycle;
      const auto first = std::find(stack.begin(), stack.end(), name);
      for (auto current = first; current != stack.end(); ++current) {
        if (!cycle.empty()) {
          cycle += " -> ";
        }
        cycle += *current;
      }
      cycle += " -> " + name;
      state_->diagnostics.report("mod import cycle: " + cycle, incoming);
      return false;
    }
    state = Visit::Active;
    stack.push_back(name);
    for (std::size_t index = 0; index < mod.imports().size(); ++index) {
      const Mod::Import& import = mod.imports()[index];
      const auto dependency = state_->mods.find(import.name);
      if (dependency != state_->mods.end() &&
          !self(self, dependency->second,
                detail::ModAccess::import_source(mod, index))) {
        return false;
      }
    }
    stack.pop_back();
    state = Visit::Complete;
    return true;
  };

  for (const auto& [name, mod] : state_->mods) {
    static_cast<void>(name);
    if (!visit(visit, mod, std::nullopt)) {
      return false;
    }
  }

  visits.clear();
  stack.clear();
  const auto visit_fn = [&](const auto& self, const Mod::FnDecl& fn,
                            std::optional<Loc> incoming) -> bool {
    const std::string identity(fn.symbol().qualified_name());
    Visit& state = visits[identity];
    if (state == Visit::Complete) {
      return true;
    }
    if (state == Visit::Active) {
      std::string cycle;
      const auto first = std::find(stack.begin(), stack.end(), identity);
      for (auto current = first; current != stack.end(); ++current) {
        if (!cycle.empty()) {
          cycle += " -> ";
        }
        cycle += *current;
      }
      cycle += " -> " + identity;
      state_->diagnostics.report("pure fn cycle: " + cycle, incoming);
      return false;
    }
    state = Visit::Active;
    stack.push_back(identity);
    const auto owner = state_->mods.find(fn.symbol().mod_name());
    const auto location =
        owner == state_->mods.end()
            ? std::optional<Loc>{}
            : detail::ModAccess::declaration_source(
                  owner->second, Mod::Symbol::Kind::Fn, fn.name());
    bool valid = true;
    const auto walk = [&](const auto& walk_self,
                          const Mod::Expr& expression) -> void {
      std::vector<Mod::FnDecl> targets;
      if (expression.kind == Mod::Expr::Kind::Call &&
          owner != state_->mods.end()) {
        targets =
            detail::visible_fns(*this, owner->second.name(), expression.text);
      } else if (owner != state_->mods.end() &&
                 (expression.kind == Mod::Expr::Kind::Prefix ||
                  expression.kind == Mod::Expr::Kind::Infix ||
                  expression.kind == Mod::Expr::Kind::Postfix)) {
        const auto fixity = expression.kind == Mod::Expr::Kind::Prefix
                                ? Mod::FnDecl::Fixity::Prefix
                            : expression.kind == Mod::Expr::Kind::Postfix
                                ? Mod::FnDecl::Fixity::Postfix
                                : Mod::FnDecl::Fixity::Infix;
        targets = detail::visible_operators(*this, owner->second.name(),
                                            expression.text, fixity);
      }
      if (targets.size() == 1U &&
          targets.front().form() == Mod::FnDecl::Form::Body &&
          !self(self, targets.front(), location)) {
        valid = false;
      }
      for (const auto& argument : expression.arguments) {
        walk_self(walk_self, argument);
      }
    };
    if (const auto* expression = detail::ModAccess::expression(fn)) {
      walk(walk, *expression);
    }
    stack.pop_back();
    state = Visit::Complete;
    return valid;
  };

  for (const auto& [name, mod] : state_->mods) {
    static_cast<void>(name);
    for (const Mod::FnDecl& fn : mod.fns()) {
      if (!visit_fn(visit_fn, fn, std::nullopt)) {
        return false;
      }
    }
  }

  state_->linked = true;
  return true;
}

}  // namespace joggle

