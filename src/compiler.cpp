#include "joggle/compiler.h"

#include "graph_internal.h"
#include "graph_member.h"
#include "joggle/behavior.h"
#include "module_internal.h"
#include "module_repository.h"
#include "type_internal.h"
#include "type_contract.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <limits>
#include <sstream>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace joggle {
namespace {

using detail::ParameterValue;

class DynamicLibrary {
public:
  DynamicLibrary() = default;
  ~DynamicLibrary() { close(); }
  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;
  DynamicLibrary(DynamicLibrary&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}
  DynamicLibrary& operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  static std::optional<DynamicLibrary> open(const std::filesystem::path& path,
                                            Diagnostics& diagnostics) {
    DynamicLibrary result;
#if defined(_WIN32)
    result.handle_ = LoadLibraryW(path.c_str());
    if (result.handle_ == nullptr) {
      diagnostics.report("cannot load behavior library '" + path.string() +
                         "'");
      return std::nullopt;
    }
#else
    result.handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (result.handle_ == nullptr) {
      const char* message = dlerror();
      diagnostics.report(
          "cannot load behavior library '" + path.string() +
          "': " + (message == nullptr ? "unknown error" : message));
      return std::nullopt;
    }
#endif
    return result;
  }

  void* symbol(const char* name) const {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(handle_, name));
#else
    return dlsym(handle_, name);
#endif
  }

private:
  void close() {
    if (handle_ == nullptr) {
      return;
    }
#if defined(_WIN32)
    FreeLibrary(handle_);
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
  }

#if defined(_WIN32)
  HMODULE handle_ = nullptr;
#else
  void* handle_ = nullptr;
#endif
};

std::string module_identity(const Module& module) {
  return std::string(module.name()) + "@" + to_string(module.version()) + "#" +
         std::string(module.digest());
}

std::string behavior_key(std::string_view module, std::string_view target) {
  return std::string(module) + "\n" + std::string(target);
}

template <typename Modules>
bool belongs_to(const Modules& modules, const ParameterValue& value) {
  const auto contains = [&](const Module::Symbol& symbol) {
    const auto owner = modules.find(symbol.module_name());
    return owner != modules.end() &&
           owner->second.version() == symbol.module_version() &&
           owner->second.digest() == symbol.module_digest();
  };
  if (const Type* type = value.as_type()) {
    return contains(type->schema().symbol());
  }
  if (const Attribute* attribute = value.as_attribute()) {
    return contains(attribute->schema().symbol());
  }
  if (value.kind() == ParameterValue::Kind::List) {
    return std::all_of(value.elements().begin(), value.elements().end(),
                       [&](const ParameterValue& element) {
                         return belongs_to(modules, element);
                       });
  }
  return true;
}

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

std::string_view resolve_prefix(const Module& module, std::string_view prefix) {
  if (prefix == module.name()) {
    return module.name();
  }
  const auto found = std::find_if(
      module.imports().begin(), module.imports().end(),
      [&](const Module::Import& import) { return import.prefix() == prefix; });
  return found == module.imports().end() ? prefix
                                         : std::string_view(found->name);
}

template <typename Modules>
bool conforms_to_interface(const Modules& modules,
                           const Module::Symbol& declaration,
                           std::span<const std::string> references,
                           const Module::InterfaceDecl& interface,
                           Module::SymbolKind subject) {
  const auto declaration_module = modules.find(declaration.module_name());
  const Module::Symbol interface_symbol = interface.symbol();
  const auto interface_module = modules.find(interface_symbol.module_name());
  if (declaration_module == modules.end() ||
      declaration_module->second.version() != declaration.module_version() ||
      declaration_module->second.digest() != declaration.module_digest() ||
      interface_module == modules.end() ||
      interface_module->second.version() != interface_symbol.module_version() ||
      interface_module->second.digest() != interface_symbol.module_digest() ||
      interface.subject() != subject) {
    return false;
  }
  return std::any_of(
      references.begin(), references.end(), [&](const std::string& reference) {
        const std::size_t dot = reference.find('.');
        const std::string_view owner =
            dot == std::string::npos
                ? declaration.module_name()
                : resolve_prefix(declaration_module->second,
                                 std::string_view(reference).substr(0, dot));
        const std::string_view local =
            dot == std::string::npos
                ? std::string_view(reference)
                : std::string_view(reference).substr(dot + 1U);
        return owner == interface_symbol.module_name() &&
               local == interface_symbol.local_name();
      });
}

std::string
method_binding_key(const Module::Symbol& declaration,
                   const Module::InterfaceDecl::MethodDecl& method) {
  return declaration.stable_name() + "/conforms/" + method.stable_name();
}

bool matches_method_result(const Module::InterfaceDecl::MethodDecl& method,
                           const ParameterValue& value) {
  return detail::matches_parameter(
      Module::ParameterDecl{"result", method.result_kind(),
                            method.result_is_list(), false, std::nullopt},
      value);
}

template <typename Declaration, typename Function, typename BindingMap,
          typename Conforms>
void bind_interface_method(bool linked, Declaration declaration,
                           Module::InterfaceDecl::MethodDecl method,
                           Function function, BindingMap& bindings,
                           Diagnostics& diagnostics, Conforms conforms,
                           std::string_view subject_kind) {
  if (!linked) {
    diagnostics.report(
        "cannot bind an interface method before the compiler is linked");
    return;
  }
  if (!conforms(declaration, method.owner())) {
    diagnostics.report(std::string(subject_kind) + " '" +
                       declaration.symbol().qualified_name() +
                       "' does not provide interface method '" +
                       method.qualified_name() + "'");
    return;
  }
  if (!function) {
    diagnostics.report("interface method binding is empty");
    return;
  }
  if (!bindings
           .emplace(method_binding_key(declaration.symbol(), method),
                    std::move(function))
           .second) {
    diagnostics.report("interface method '" + method.qualified_name() +
                       "' is already bound for " + std::string(subject_kind) +
                       " '" + declaration.symbol().qualified_name() + "'");
  }
}

template <typename Subject, typename BindingMap, typename Conforms,
          typename Modules>
std::optional<ParameterValue>
evaluate_interface_method(bool linked, const Subject& subject,
                          Module::InterfaceDecl::MethodDecl method,
                          std::span<const ParameterValue> parameters,
                          const BindingMap& bindings, const Modules& modules,
                          Diagnostics& diagnostics, Conforms conforms,
                          std::string_view subject_kind) {
  if constexpr (std::is_same_v<Subject, Operation>) {
    if (!subject.valid()) {
      diagnostics.report("invalid operation used as an interface subject");
      return std::nullopt;
    }
  }
  const auto declaration = subject.schema();
  if (!linked || !conforms(declaration, method.owner())) {
    diagnostics.report(std::string(subject_kind) + " '" +
                       declaration.symbol().qualified_name() +
                       "' does not provide interface method '" +
                       method.qualified_name() + "'");
    return std::nullopt;
  }
  auto arguments = detail::validate_parameters(
      method.qualified_name(), method.parameters(), parameters, diagnostics);
  if (!arguments) {
    return std::nullopt;
  }
  const auto binding =
      bindings.find(method_binding_key(declaration.symbol(), method));
  if (binding == bindings.end()) {
    diagnostics.report("interface method '" + method.qualified_name() +
                       "' has no binding for " + std::string(subject_kind) +
                       " '" + declaration.symbol().qualified_name() + "'");
    return std::nullopt;
  }
  Diagnostics reported;
  auto result = binding->second(subject, *arguments, reported);
  if (!result && reported.ok()) {
    reported.report("interface method '" + method.qualified_name() +
                    "' did not produce a value");
  }
  std::optional<SourceRange> location;
  if constexpr (std::is_same_v<Subject, Operation>) {
    location = detail::GraphAccess::location(subject);
  }
  for (const Diagnostic& entry : reported.entries()) {
    Diagnostic diagnostic = entry;
    if (!diagnostic.source) {
      diagnostic.source = location;
    }
    diagnostics.report(std::move(diagnostic));
  }
  if (!result || !reported.ok()) {
    return std::nullopt;
  }
  if (!matches_method_result(method, *result) ||
      !belongs_to(modules, *result)) {
    diagnostics.report("interface method '" + method.qualified_name() +
                       "' produced a value with the wrong kind or module "
                       "identity");
    return std::nullopt;
  }
  return result;
}

template <typename Modules>
std::optional<Module::PassDecl> resolve_pass(const Modules& modules,
                                             std::string_view owner,
                                             std::string_view reference) {
  const std::size_t dot = reference.find('.');
  std::string_view module_name = owner;
  if (dot != std::string_view::npos) {
    const auto source = modules.find(owner);
    module_name =
        source == modules.end()
            ? reference.substr(0, dot)
            : resolve_prefix(source->second, reference.substr(0, dot));
  }
  const std::string_view local =
      dot == std::string_view::npos ? reference : reference.substr(dot + 1U);
  const auto module = modules.find(module_name);
  return module == modules.end() ? std::nullopt : module->second.pass(local);
}

template <typename Modules>
std::optional<Module::OperationDecl>
resolve_rule_operation(const Modules& modules, const Module::PassDecl& pass,
                       std::string_view reference) {
  const Module::Symbol owner = pass.symbol();
  const std::size_t dot = reference.find('.');
  std::string_view module_name = owner.module_name();
  if (dot != std::string_view::npos) {
    const auto source = modules.find(owner.module_name());
    module_name =
        source == modules.end()
            ? reference.substr(0, dot)
            : resolve_prefix(source->second, reference.substr(0, dot));
  }
  const std::string_view local =
      dot == std::string_view::npos ? reference : reference.substr(dot + 1U);
  const auto module = modules.find(module_name);
  return module == modules.end() ? std::nullopt
                                 : module->second.operation(local);
}

std::string term_key(const detail::RuleDefinition& rule, std::size_t index) {
  const detail::TermDefinition& term = rule.terms[index];
  if (term.kind == detail::TermDefinition::Kind::Variable) {
    return "%" + term.name;
  }
  std::string result(term.name);
  result += '(';
  for (std::size_t argument = 0; argument < term.arguments.size(); ++argument) {
    if (argument != 0U) {
      result += ',';
    }
    result += term_key(rule, term.arguments[argument]);
  }
  return result + ')';
}

struct RuleMatch {
  Operation root;
  Value replacement;
};

template <typename Modules>
bool match_term(const Value& value, const detail::RuleDefinition& rule,
                std::size_t term_index, const Module::PassDecl& pass,
                const Modules& modules,
                std::map<std::string, Value>& bindings) {
  const detail::TermDefinition& term = rule.terms[term_index];
  const std::string key = term_key(rule, term_index);
  const auto bound = bindings.find(key);
  if (bound != bindings.end()) {
    return bound->second == value;
  }
  if (term.kind == detail::TermDefinition::Kind::Variable) {
    bindings.emplace(key, value);
    return true;
  }
  const auto target = resolve_rule_operation(modules, pass, term.name);
  const auto defining = value.defining_operation();
  if (!target || !defining || defining->results().size() != 1U ||
      defining->result(0) != value || defining->schema() != *target) {
    return false;
  }
  const auto operands = defining->operands();
  if (operands.size() != term.arguments.size()) {
    return false;
  }
  for (std::size_t index = 0; index < operands.size(); ++index) {
    if (!match_term(operands[index], rule, term.arguments[index], pass, modules,
                    bindings)) {
      return false;
    }
  }
  bindings.emplace(key, value);
  return true;
}

template <typename Modules>
std::optional<RuleMatch>
find_rule_match(const Region& region, const Module::PassDecl& pass,
                const detail::RuleDefinition& rule, const Modules& modules) {
  const auto try_operation =
      [&](const Operation& operation) -> std::optional<RuleMatch> {
    const auto results = operation.results();
    if (results.size() != 1U) {
      return std::nullopt;
    }
    std::map<std::string, Value> bindings;
    if (!match_term(results.front(), rule, rule.match, pass, modules,
                    bindings)) {
      return std::nullopt;
    }
    const auto replacement = bindings.find(term_key(rule, rule.replacement));
    if (replacement == bindings.end()) {
      return std::nullopt;
    }
    return RuleMatch{operation, replacement->second};
  };

  for (const Operation& operation : region.operations()) {
    if (auto match = try_operation(operation)) {
      return match;
    }
    for (const Region& nested : operation.regions()) {
      if (auto match = find_rule_match(nested, pass, rule, modules)) {
        return match;
      }
    }
  }
  return std::nullopt;
}

template <typename Modules>
bool apply_contraction_rule(Graph& graph, const Module::PassDecl& pass,
                            const detail::RuleDefinition& rule,
                            const Modules& modules, Diagnostics& diagnostics) {
  while (auto match = find_rule_match(detail::GraphAccess::root(graph), pass,
                                      rule, modules)) {
    auto edit = graph.edit();
    const Value result = match->root.result(0);
    if (result.type() != match->replacement.type()) {
      diagnostics.report("pass '" + pass.symbol().qualified_name() +
                         "' matched a replacement with incompatible type");
      return false;
    }
    edit.replace(result, match->replacement);
    edit.erase(match->root);
    if (!edit.commit(diagnostics)) {
      return false;
    }
  }
  return true;
}

}  // namespace

struct Compiler::State {
  struct LockedIdentity {
    Version version;
    std::string digest;
    bool root = false;
  };
  struct LockedBehavior {
    Version module_version;
    std::string module_digest;
    std::string target;
    std::string digest;
  };
  Diagnostics diagnostics;
  std::map<std::string, Module, std::less<>> modules;
  std::map<std::string, std::filesystem::path, std::less<>> module_sources;
  std::set<std::string, std::less<>> explicit_modules;
  std::vector<std::filesystem::path> search_paths;
  std::map<std::string, LockedIdentity, std::less<>> locked_modules;
  std::map<std::string, LockedBehavior, std::less<>> locked_behaviors;
  bool has_lock = false;
  std::vector<DynamicLibrary> behavior_libraries;
  std::set<std::string, std::less<>> loaded_behaviors;
  std::map<std::string, VerifierFunction<Type>, std::less<>> type_verifiers;
  std::map<std::string, VerifierFunction<Attribute>, std::less<>>
      attribute_verifiers;
  std::map<std::string, VerifierFunction<Operation>, std::less<>>
      operation_verifiers;
  std::map<std::string, MethodFunction<Type>, std::less<>> type_methods;
  std::map<std::string, MethodFunction<Attribute>, std::less<>>
      attribute_methods;
  std::map<std::string, MethodFunction<Operation>, std::less<>>
      operation_methods;
  std::map<std::string, PassFunction, std::less<>> passes;
  bool linked = false;
};

Compiler::Compiler() : state_(std::make_unique<State>()) {}
Compiler::~Compiler() = default;
Compiler::Compiler(Compiler&&) noexcept = default;
Compiler& Compiler::operator=(Compiler&&) noexcept = default;

void Compiler::add(std::string_view text, std::string source) {
  if (state_->linked) {
    state_->diagnostics.report(
        "cannot add a module after the compiler has been linked");
    return;
  }
  auto parsed = parse_module(text, state_->diagnostics, std::move(source));
  if (!parsed) {
    return;
  }

  add_module(std::move(*parsed), true, std::nullopt);
}

void Compiler::add_module(Module module, bool explicit_module,
                          std::optional<std::filesystem::path> source) {
  const std::string name(module.name());
  if (explicit_module) {
    state_->explicit_modules.insert(name);
  }
  const auto found = state_->modules.find(name);
  if (found == state_->modules.end()) {
    state_->modules.emplace(name, std::move(module));
    if (source) {
      state_->module_sources.emplace(name, std::move(*source));
    }
    return;
  }
  if (found->second.version() == module.version() &&
      found->second.digest() == module.digest()) {
    if (source) {
      state_->module_sources.insert_or_assign(name, std::move(*source));
    }
    return;
  }
  state_->diagnostics.report("module '" + name +
                                 "' was loaded with conflicting identities",
                             std::nullopt);
}

void Compiler::load(const std::filesystem::path& path) {
  if (state_->linked) {
    state_->diagnostics.report(
        "cannot add a module after the compiler has been linked");
    return;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    state_->diagnostics.report("cannot open module '" + path.string() + "'");
    return;
  }
  std::ostringstream text;
  text << input.rdbuf();
  if (!input.eof() && input.fail()) {
    state_->diagnostics.report("cannot read module '" + path.string() + "'");
    return;
  }
  auto parsed = parse_module(text.str(), state_->diagnostics, path.string());
  if (parsed) {
    add_module(std::move(*parsed), true,
               std::filesystem::absolute(path).lexically_normal());
  }
}

void Compiler::search(std::filesystem::path root) {
  if (state_->linked) {
    state_->diagnostics.report(
        "cannot add a module search path after the compiler is linked");
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
  std::map<std::string, State::LockedBehavior, std::less<>> locked_behavior;
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
                                 SourceRange{path.string(),
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
    const bool is_behavior = kind == "behavior";
    if (!is_root && kind != "module" && !is_behavior) {
      report("expected root, module, or behavior lock entry");
      continue;
    }
    text.remove_prefix(space + 1U);
    std::string_view module_text = text;
    std::string_view behavior_text;
    if (is_behavior) {
      const std::size_t separator = text.find(' ');
      if (separator == std::string_view::npos) {
        report("behavior lock entry needs target#digest");
        continue;
      }
      module_text = text.substr(0, separator);
      behavior_text = text.substr(separator + 1U);
    }
    const std::size_t at = module_text.find('@');
    const std::size_t hash = module_text.find('#');
    if (at == std::string_view::npos || hash == std::string_view::npos ||
        at >= hash) {
      report("expected name@version#digest");
      continue;
    }
    const std::string_view name = module_text.substr(0, at);
    const auto version =
        parse_exact_version(module_text.substr(at + 1U, hash - at - 1U));
    const std::string_view module_digest = module_text.substr(hash + 1U);
    if (!identifier(name) || !version || !digest(module_digest)) {
      report("invalid locked module identity");
      continue;
    }
    if (is_behavior) {
      const std::size_t behavior_hash = behavior_text.find('#');
      if (behavior_hash == std::string_view::npos) {
        report("expected target#digest for behavior");
        continue;
      }
      const std::string_view target = behavior_text.substr(0, behavior_hash);
      const std::string_view behavior_digest =
          behavior_text.substr(behavior_hash + 1U);
      if (!target_identifier(target) || !digest(behavior_digest)) {
        report("invalid locked behavior identity");
        continue;
      }
      if (!locked_behavior
               .emplace(behavior_key(name, target),
                        State::LockedBehavior{
                            *version, std::string(module_digest),
                            std::string(target), std::string(behavior_digest)})
               .second) {
        report("duplicate locked behavior for '" + std::string(name) +
               "' and target '" + std::string(target) + "'");
      }
      continue;
    }
    if (!locked
             .emplace(std::string(name),
                      State::LockedIdentity{
                          *version, std::string(module_digest), is_root})
             .second) {
      report("duplicate locked module '" + std::string(name) + "'");
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
    state_->diagnostics.report(
        "lock file must contain exactly one root module");
  }
  if (state_->diagnostics.size() != before) {
    return;
  }
  state_->locked_modules = std::move(locked);
  state_->locked_behaviors = std::move(locked_behavior);
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
      Module::Import import;
      std::optional<SourceRange> source;
    };
    std::vector<MissingImport> missing;
    for (const auto& [name, module] : state_->modules) {
      static_cast<void>(name);
      for (std::size_t index = 0; index < module.imports().size(); ++index) {
        const Module::Import& import = module.imports()[index];
        if (state_->modules.find(import.name) == state_->modules.end()) {
          missing.push_back(
              {import, detail::ModuleAccess::import_source(module, index)});
        }
      }
    }
    for (const MissingImport& pending : missing) {
      const Module::Import& import = pending.import;
      if (state_->modules.find(import.name) != state_->modules.end() ||
          state_->search_paths.empty()) {
        continue;
      }
      std::optional<detail::InstalledModule> resolved;
      if (state_->has_lock) {
        const auto locked = state_->locked_modules.find(import.name);
        if (locked == state_->locked_modules.end()) {
          const std::string message =
              "module '" + import.name + "' is missing from the lock file";
          state_->diagnostics.report(message, pending.source);
          continue;
        }
        if (!import.version.contains(locked->second.version)) {
          const std::string message = "locked module '" + import.name + "@" +
                                      to_string(locked->second.version) +
                                      "' does not satisfy import " +
                                      to_string(import.version);
          state_->diagnostics.report(message, pending.source);
          continue;
        }
        resolved = detail::resolve_module(
            state_->search_paths, import.name, locked->second.version,
            locked->second.digest, state_->diagnostics);
        if (!resolved) {
          const std::string message = "locked module '" + import.name + "@" +
                                      to_string(locked->second.version) + "#" +
                                      locked->second.digest +
                                      "' is not installed";
          state_->diagnostics.report(message, pending.source);
        }
      } else {
        resolved = detail::resolve_module(state_->search_paths, import.name,
                                          import.version, state_->diagnostics);
      }
      if (!resolved) {
        continue;
      }
      add_module(
          std::move(resolved->module), false,
          std::filesystem::absolute(resolved->source).lexically_normal());
      loaded = true;
    }
    if (!state_->diagnostics.ok()) {
      return false;
    }
  }

  if (state_->has_lock) {
    for (const auto& [name, module] : state_->modules) {
      const auto locked = state_->locked_modules.find(name);
      if (locked == state_->locked_modules.end()) {
        state_->diagnostics.report("loaded module '" + name +
                                   "' is absent from the lock file");
        continue;
      }
      if (locked->second.version != module.version() ||
          locked->second.digest != module.digest()) {
        state_->diagnostics.report("loaded module '" + name +
                                   "' does not match its locked identity");
      }
      if (locked->second.root && state_->explicit_modules.find(name) ==
                                     state_->explicit_modules.end()) {
        state_->diagnostics.report("locked root module '" + name +
                                   "' was not loaded explicitly");
      }
    }
    for (const auto& [name, identity] : state_->locked_modules) {
      static_cast<void>(identity);
      if (state_->modules.find(name) == state_->modules.end()) {
        state_->diagnostics.report("locked module '" + name +
                                   "' is not part of the resolved closure");
      }
    }
    for (const auto& [key, behavior] : state_->locked_behaviors) {
      const std::size_t separator = key.find('\n');
      const std::string name = key.substr(0, separator);
      const auto module = state_->modules.find(name);
      if (module == state_->modules.end() ||
          module->second.version() != behavior.module_version ||
          module->second.digest() != behavior.module_digest) {
        state_->diagnostics.report(
            "locked behavior references a different module identity for '" +
            name + "'");
        continue;
      }
      if (behavior.target != behavior_target) {
        continue;
      }

      std::vector<std::filesystem::path> candidates;
      const auto source = state_->module_sources.find(name);
      if (source != state_->module_sources.end()) {
        candidates =
            detail::behavior_candidates(source->second, state_->diagnostics);
      }
      if (candidates.empty() && !state_->search_paths.empty()) {
        auto installed = detail::resolve_module(
            state_->search_paths, name, behavior.module_version,
            behavior.module_digest, state_->diagnostics);
        if (installed) {
          candidates = detail::behavior_candidates(installed->source,
                                                   state_->diagnostics);
          if (!candidates.empty()) {
            state_->module_sources.insert_or_assign(
                name, std::filesystem::absolute(installed->source)
                          .lexically_normal());
          }
        }
      }
      bool found = false;
      for (const auto& candidate : candidates) {
        const auto candidate_digest =
            detail::behavior_digest(candidate, state_->diagnostics);
        if (candidate_digest && *candidate_digest == behavior.digest) {
          found = true;
          break;
        }
      }
      if (!found) {
        state_->diagnostics.report("locked behavior for '" + name +
                                   "' and target '" + behavior.target +
                                   "' is not installed with digest " +
                                   behavior.digest);
      }
    }
    if (!state_->diagnostics.ok()) {
      return false;
    }
  }

  for (const auto& [name, module] : state_->modules) {
    for (std::size_t index = 0; index < module.imports().size(); ++index) {
      const Module::Import& import = module.imports()[index];
      const auto source = detail::ModuleAccess::import_source(module, index);
      const auto dependency = state_->modules.find(import.name);
      if (dependency == state_->modules.end()) {
        state_->diagnostics.report("module '" + name +
                                       "' imports missing module '" +
                                       import.name + "'",
                                   source);
        continue;
      }
      if (!import.version.contains(dependency->second.version())) {
        state_->diagnostics.report(
            "module '" + name + "' imports '" + import.name + "@" +
                to_string(import.version) + "', but version " +
                to_string(dependency->second.version()) + " was loaded",
            source);
      }
    }

    const auto validate_interfaces =
        [&](std::span<const std::string> references, Module::SymbolKind subject,
            std::string_view declaration, std::optional<SourceRange> source) {
          for (const std::string& reference : references) {
            const std::size_t dot = reference.find('.');
            const std::string owner =
                dot == std::string::npos
                    ? name
                    : std::string(
                          resolve_prefix(module, reference.substr(0, dot)));
            const std::string local = dot == std::string::npos
                                          ? reference
                                          : reference.substr(dot + 1U);
            const auto interface_module = state_->modules.find(owner);
            if (interface_module == state_->modules.end()) {
              state_->diagnostics.report(
                  "declaration '" + name + "." + std::string(declaration) +
                      "' conforms to interface from missing module '" + owner +
                      "'",
                  source);
              continue;
            }
            const auto interface = interface_module->second.interface(local);
            if (!interface) {
              state_->diagnostics.report(
                  "declaration '" + name + "." + std::string(declaration) +
                      "' conforms to unknown interface '" + reference + "'",
                  source);
              continue;
            }
            if (interface->subject() != subject) {
              state_->diagnostics.report(
                  "declaration '" + name + "." + std::string(declaration) +
                      "' conforms to interface '" + reference +
                      "' for the wrong subject kind",
                  source);
            }
          }
        };
    for (const Module::TypeDecl& declaration : module.types()) {
      validate_interfaces(
          declaration.interfaces(), Module::SymbolKind::Type,
          declaration.name(),
          detail::ModuleAccess::declaration_source(
              module, Module::SymbolKind::Type, declaration.name()));
    }
    for (const Module::AttributeDecl& declaration : module.attributes()) {
      validate_interfaces(
          declaration.interfaces(), Module::SymbolKind::Attribute,
          declaration.name(),
          detail::ModuleAccess::declaration_source(
              module, Module::SymbolKind::Attribute, declaration.name()));
    }
    for (const Module::OperationDecl& declaration : module.operations()) {
      const auto operation_source = detail::ModuleAccess::declaration_source(
          module, Module::SymbolKind::Operation, declaration.name());
      const auto report_operation = [&](std::string message) {
        state_->diagnostics.report(std::move(message), operation_source);
      };
      validate_interfaces(declaration.interfaces(),
                          Module::SymbolKind::Operation, declaration.name(),
                          operation_source);
      const auto& contract = detail::OperationTypeAccess::get(declaration);
      const auto validate_expression =
          [&](const auto& self, const detail::TypeExpression& expression,
              Module::ParameterKind expected, bool expected_list) -> void {
        using Kind = detail::TypeExpression::Kind;
        if (expression.kind == Kind::Variable) {
          const auto variable =
              std::find_if(contract.generics.begin(), contract.generics.end(),
                           [&](const auto& generic) {
                             return generic.name == expression.text;
                           });
          if (variable == contract.generics.end() ||
              variable->kind != expected || variable->list != expected_list) {
            report_operation("type variable '" + expression.text +
                             "' in operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' has the wrong kind");
          }
          return;
        }
        if (expected_list) {
          if (expression.kind != Kind::List) {
            report_operation("operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' expects a list-valued type expression");
            return;
          }
          for (const auto& element : expression.arguments) {
            self(self, element, expected, false);
          }
          return;
        }
        if (expression.kind == Kind::List) {
          report_operation("operation '" + name + "." +
                           std::string(declaration.name()) +
                           "' contains an unexpected list type expression");
          return;
        }
        if (expression.kind == Kind::Number ||
            expression.kind == Kind::Boolean ||
            expression.kind == Kind::String) {
          const bool matches = (expression.kind == Kind::Number &&
                                (expected == Module::ParameterKind::I64 ||
                                 expected == Module::ParameterKind::F64)) ||
                               (expression.kind == Kind::Boolean &&
                                expected == Module::ParameterKind::Boolean) ||
                               (expression.kind == Kind::String &&
                                expected == Module::ParameterKind::String);
          if (!matches) {
            report_operation("literal in operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' has the wrong kind");
          }
          return;
        }
        if (expected != Module::ParameterKind::Type &&
            expected != Module::ParameterKind::Attribute) {
          report_operation("type expression '" + expression.text +
                           "' has the wrong kind in operation '" + name + "." +
                           std::string(declaration.name()) + "'");
          return;
        }
        const std::size_t dot = expression.text.find('.');
        const std::string owner =
            dot == std::string::npos
                ? name
                : std::string(resolve_prefix(
                      module,
                      std::string_view(expression.text).substr(0, dot)));
        const std::string local = dot == std::string::npos
                                      ? expression.text
                                      : expression.text.substr(dot + 1U);
        const auto source = state_->modules.find(owner);
        if (source == state_->modules.end()) {
          report_operation("operation '" + name + "." +
                           std::string(declaration.name()) +
                           "' references missing module '" + owner + "'");
          return;
        }
        std::span<const Module::ParameterDecl> parameters;
        if (expected == Module::ParameterKind::Type) {
          const auto target = source->second.type(local);
          if (!target) {
            report_operation(
                "operation '" + name + "." + std::string(declaration.name()) +
                "' references unknown type '" + expression.text + "'");
            return;
          }
          parameters = target->parameters();
        } else {
          const auto target = source->second.attribute(local);
          if (!target) {
            report_operation(
                "operation '" + name + "." + std::string(declaration.name()) +
                "' references unknown attribute '" + expression.text + "'");
            return;
          }
          parameters = target->parameters();
        }
        if (expression.arguments.size() > parameters.size()) {
          report_operation("type expression '" + expression.text +
                           "' has too many arguments in operation '" + name +
                           "." + std::string(declaration.name()) + "'");
          return;
        }
        for (std::size_t index = 0; index < expression.arguments.size();
             ++index) {
          self(self, expression.arguments[index], parameters[index].kind,
               parameters[index].list);
        }
        for (std::size_t index = expression.arguments.size();
             index < parameters.size(); ++index) {
          if (!parameters[index].default_value) {
            report_operation("type expression '" + expression.text +
                             "' omits argument '" +
                             parameters[index].name + "' in operation '" +
                             name + "." + std::string(declaration.name()) +
                             "'");
          }
        }
      };
      if (contract.inputs.size() != declaration.inputs().size() ||
          contract.results.size() != declaration.results().size()) {
        report_operation("operation '" + name + "." +
                         std::string(declaration.name()) +
                         "' has an invalid type contract");
        continue;
      }
      for (std::size_t index = 0; index < declaration.inputs().size();
           ++index) {
        if (declaration.inputs()[index].kind == Module::ParameterKind::Value) {
          if (!contract.inputs[index]) {
            report_operation("operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' has an untyped operand");
          } else {
            validate_expression(validate_expression, *contract.inputs[index],
                                Module::ParameterKind::Type, false);
          }
        } else if (contract.inputs[index]) {
          validate_expression(validate_expression, *contract.inputs[index],
                              declaration.inputs()[index].kind,
                              declaration.inputs()[index].list);
        }
      }
      for (const auto& result : contract.results) {
        validate_expression(validate_expression, result,
                            Module::ParameterKind::Type, false);
      }
    }

    const auto split_reference = [&](std::string_view reference) {
      const std::size_t dot = reference.find('.');
      return std::pair<std::string, std::string>{
          dot == std::string_view::npos
              ? name
              : std::string(resolve_prefix(module, reference.substr(0, dot))),
          dot == std::string_view::npos
              ? std::string(reference)
              : std::string(reference.substr(dot + 1U))};
    };
    for (const Module::PassDecl& pass : module.passes()) {
      const auto pass_source = detail::ModuleAccess::declaration_source(
          module, Module::SymbolKind::Pass, pass.name());
      const auto validate_rule_term = [&](const auto& self,
                                          const detail::RuleDefinition& rule,
                                          std::size_t index) -> void {
        const auto source = rule.source ? rule.source : pass_source;
        const detail::TermDefinition& term = rule.terms[index];
        if (term.kind == detail::TermDefinition::Kind::Variable) {
          return;
        }
        const auto target =
            resolve_rule_operation(state_->modules, pass, term.name);
        if (!target) {
          state_->diagnostics.report(
              "pass '" + name + "." + std::string(pass.name()) +
                  "' matches unknown operation '" + term.name + "'",
              source);
          return;
        }
        const auto count_kind = [](auto parameters,
                                   Module::ParameterKind kind) {
          return static_cast<std::size_t>(std::count_if(
              parameters.begin(), parameters.end(),
              [&](const auto& parameter) { return parameter.kind == kind; }));
        };
        if (count_kind(target->inputs(), Module::ParameterKind::Value) !=
                term.arguments.size() ||
            count_kind(target->results(), Module::ParameterKind::Value) != 1U ||
            count_kind(target->inputs(), Module::ParameterKind::Region) != 0U) {
          state_->diagnostics.report(
              "pass '" + name + "." + std::string(pass.name()) +
                  "' has a term incompatible with operation '" + term.name +
                  "'",
              source);
        }
        for (std::size_t argument : term.arguments) {
          self(self, rule, argument);
        }
      };
      const auto rules = detail::ModuleAccess::rules(module, pass);
      for (const detail::RuleDefinition& rule : rules) {
        validate_rule_term(validate_rule_term, rule, rule.match);
      }
      for (const std::string& step : pass.steps()) {
        const auto [owner, local] = split_reference(step);
        const auto target_module = state_->modules.find(owner);
        if (target_module == state_->modules.end() ||
            !target_module->second.pass(local)) {
          state_->diagnostics.report(
              "pass '" + name + "." + std::string(pass.name()) +
                  "' contains unknown pass '" + step + "'",
              pass_source);
        }
      }
    }
  }
  if (!state_->diagnostics.ok()) {
    return false;
  }

  enum class Visit { Unseen, Active, Complete };
  std::unordered_map<std::string, Visit> visits;
  std::vector<std::string> stack;
  const auto visit = [&](const auto& self, const Module& module,
                         std::optional<SourceRange> incoming) -> bool {
    const std::string name(module.name());
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
      state_->diagnostics.report("module import cycle: " + cycle, incoming);
      return false;
    }
    state = Visit::Active;
    stack.push_back(name);
    for (std::size_t index = 0; index < module.imports().size(); ++index) {
      const Module::Import& import = module.imports()[index];
      const auto dependency = state_->modules.find(import.name);
      if (dependency != state_->modules.end() &&
          !self(self, dependency->second,
                detail::ModuleAccess::import_source(module, index))) {
        return false;
      }
    }
    stack.pop_back();
    state = Visit::Complete;
    return true;
  };

  for (const auto& [name, module] : state_->modules) {
    static_cast<void>(name);
    if (!visit(visit, module, std::nullopt)) {
      return false;
    }
  }

  visits.clear();
  stack.clear();
  const auto visit_pass = [&](const auto& self, const Module::PassDecl& pass,
                              std::optional<SourceRange> incoming) -> bool {
    const std::string identity(pass.symbol().qualified_name());
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
      state_->diagnostics.report("pass cycle: " + cycle, incoming);
      return false;
    }
    state = Visit::Active;
    stack.push_back(identity);
    const auto owner = state_->modules.find(pass.symbol().module_name());
    const auto source =
        owner == state_->modules.end()
            ? std::optional<SourceRange>{}
            : detail::ModuleAccess::declaration_source(
                  owner->second, Module::SymbolKind::Pass, pass.name());
    for (const std::string& step : pass.steps()) {
      const auto dependency =
          resolve_pass(state_->modules, pass.symbol().module_name(), step);
      if (dependency && !self(self, *dependency, source)) {
        return false;
      }
    }
    stack.pop_back();
    state = Visit::Complete;
    return true;
  };
  for (const auto& [name, module] : state_->modules) {
    static_cast<void>(name);
    for (const Module::PassDecl& pass : module.passes()) {
      if (!visit_pass(visit_pass, pass, std::nullopt)) {
        return false;
      }
    }
  }
  state_->linked = true;
  return true;
}

bool Compiler::ok() const { return state_->diagnostics.ok(); }

bool Compiler::linked() const { return state_->linked; }

std::optional<Module> Compiler::module(std::string_view name) const {
  const auto found = state_->modules.find(name);
  if (found == state_->modules.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::vector<Module> Compiler::modules() const {
  std::vector<Module> result;
  result.reserve(state_->modules.size());
  for (const auto& [name, module] : state_->modules) {
    static_cast<void>(name);
    result.push_back(module);
  }
  return result;
}

bool Compiler::load_behavior(std::string_view module,
                             const std::filesystem::path& library) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot load behavior before the compiler is linked");
    return false;
  }
  const auto found = state_->modules.find(module);
  if (found == state_->modules.end()) {
    state_->diagnostics.report("behavior target module '" +
                               std::string(module) + "' is not linked");
    return false;
  }
  return load_behavior(found->second, library);
}

bool Compiler::load_behavior(std::string_view module) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot load behavior before the compiler is linked");
    return false;
  }
  const auto found = state_->modules.find(module);
  if (found == state_->modules.end()) {
    state_->diagnostics.report("behavior target module '" +
                               std::string(module) + "' is not linked");
    return false;
  }
  return load_behavior(found->second);
}

bool Compiler::load_behavior(const Module& module,
                             const std::filesystem::path& library) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot load behavior before the compiler is linked");
    return false;
  }
  const auto loaded = state_->modules.find(module.name());
  if (loaded == state_->modules.end() ||
      loaded->second.version() != module.version() ||
      loaded->second.digest() != module.digest()) {
    state_->diagnostics.report("behavior target '" + module_identity(module) +
                               "' is not part of this compiler");
    return false;
  }
  const std::string identity = module_identity(module);
  if (state_->loaded_behaviors.contains(identity)) {
    return true;
  }
  if (state_->has_lock) {
    const auto locked = state_->locked_behaviors.find(
        behavior_key(module.name(), behavior_target));
    if (locked == state_->locked_behaviors.end()) {
      state_->diagnostics.report("behavior for '" + identity +
                                 "' is absent from the lock file");
      return false;
    }
    const auto actual = detail::behavior_digest(library, state_->diagnostics);
    if (!actual || *actual != locked->second.digest) {
      state_->diagnostics.report("behavior library '" + library.string() +
                                 "' does not match locked digest " +
                                 locked->second.digest);
      return false;
    }
  }

  auto opened = DynamicLibrary::open(library, state_->diagnostics);
  if (!opened) {
    return false;
  }
  void* address = opened->symbol(behavior_entry);
  if (address == nullptr) {
    state_->diagnostics.report("behavior library '" + library.string() +
                               "' does not export " + behavior_entry);
    return false;
  }
  static_assert(sizeof(BehaviorEntry) == sizeof(address));
  BehaviorEntry entry = nullptr;
  std::memcpy(&entry, &address, sizeof(entry));
  const Behavior* behavior = nullptr;
  try {
    behavior = entry();
  } catch (const std::exception& exception) {
    state_->diagnostics.report("behavior entry in '" + library.string() +
                               "' threw: " + exception.what());
    return false;
  } catch (...) {
    state_->diagnostics.report("behavior entry in '" + library.string() +
                               "' threw an unknown exception");
    return false;
  }
  if (behavior == nullptr || behavior->abi != behavior_abi ||
      behavior->size < sizeof(Behavior) ||
      behavior->module_identity == nullptr || behavior->target == nullptr ||
      behavior->bind == nullptr) {
    state_->diagnostics.report("behavior library '" + library.string() +
                               "' has an incompatible descriptor");
    return false;
  }
  if (behavior->module_identity != identity) {
    state_->diagnostics.report("behavior library '" + library.string() +
                               "' targets '" + behavior->module_identity +
                               "', not '" + identity + "'");
    return false;
  }
  if (behavior->target != std::string_view(behavior_target)) {
    state_->diagnostics.report("behavior library '" + library.string() +
                               "' targets '" + behavior->target +
                               "', not host target '" + behavior_target + "'");
    return false;
  }

  auto type_verifiers = state_->type_verifiers;
  auto attribute_verifiers = state_->attribute_verifiers;
  auto operation_verifiers = state_->operation_verifiers;
  auto type_methods = state_->type_methods;
  auto attribute_methods = state_->attribute_methods;
  auto operation_methods = state_->operation_methods;
  auto passes = state_->passes;
  const std::size_t before = state_->diagnostics.size();
  bool bound = false;
  try {
    bound = behavior->bind(*this, loaded->second, state_->diagnostics);
  } catch (const std::exception& exception) {
    state_->diagnostics.report("behavior binding for '" + identity +
                               "' threw: " + exception.what());
  } catch (...) {
    state_->diagnostics.report("behavior binding for '" + identity +
                               "' threw an unknown exception");
  }
  if (!bound || state_->diagnostics.size() != before) {
    state_->type_verifiers = std::move(type_verifiers);
    state_->attribute_verifiers = std::move(attribute_verifiers);
    state_->operation_verifiers = std::move(operation_verifiers);
    state_->type_methods = std::move(type_methods);
    state_->attribute_methods = std::move(attribute_methods);
    state_->operation_methods = std::move(operation_methods);
    state_->passes = std::move(passes);
    if (!bound && state_->diagnostics.size() == before) {
      state_->diagnostics.report("behavior binding for '" + identity +
                                 "' failed");
    }
    return false;
  }

  state_->behavior_libraries.push_back(std::move(*opened));
  state_->loaded_behaviors.insert(identity);
  return true;
}

bool Compiler::load_behavior(const Module& module) {
  const auto source = state_->module_sources.find(module.name());
  if (source == state_->module_sources.end()) {
    state_->diagnostics.report("module '" + module_identity(module) +
                               "' has no filesystem package location");
    return false;
  }
  auto candidates =
      detail::behavior_candidates(source->second, state_->diagnostics);
  if (candidates.empty()) {
    state_->diagnostics.report("module '" + module_identity(module) +
                               "' has no behavior for target '" +
                               behavior_target + "'");
    return false;
  }
  if (candidates.size() != 1U) {
    state_->diagnostics.report("module '" + module_identity(module) +
                               "' has ambiguous behavior for target '" +
                               behavior_target + "'");
    return false;
  }
  return load_behavior(module, candidates.front());
}

std::optional<Type> Compiler::make(const Module::TypeDecl& schema,
                                   std::span<const ParameterValue> parameters) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot construct a type before the compiler is linked");
    return std::nullopt;
  }
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
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
  if (!std::all_of(values->begin(), values->end(),
                   [&](const ParameterValue& value) {
                     return belongs_to(state_->modules, value);
                   })) {
    state_->diagnostics.report("type '" + symbol.qualified_name() +
                               "' references a value outside this compiler's "
                               "module closure");
    return std::nullopt;
  }
  Type type = detail::TypeAccess::make(schema, std::move(*values));
  const auto verifier = state_->type_verifiers.find(symbol.stable_name());
  if (verifier != state_->type_verifiers.end()) {
    Diagnostics reported;
    const bool accepted = verifier->second(type, reported);
    if (!accepted && reported.ok()) {
      reported.report("type verifier rejected '" + symbol.qualified_name() +
                      "'");
    }
    for (const Diagnostic& entry : reported.entries()) {
      state_->diagnostics.report(entry);
    }
    if (!accepted || !reported.ok()) {
      return std::nullopt;
    }
  }
  return type;
}

std::optional<Attribute>
Compiler::make(const Module::AttributeDecl& schema,
               std::span<const ParameterValue> parameters) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot construct an attribute before the compiler is linked");
    return std::nullopt;
  }
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("attribute schema '" + symbol.qualified_name() +
                               "' is not part of this compiler");
    return std::nullopt;
  }
  auto values =
      detail::validate_parameters(symbol.qualified_name(), schema.parameters(),
                                  parameters, state_->diagnostics);
  if (!values) {
    return std::nullopt;
  }
  if (!std::all_of(values->begin(), values->end(),
                   [&](const ParameterValue& value) {
                     return belongs_to(state_->modules, value);
                   })) {
    state_->diagnostics.report(
        "attribute '" + symbol.qualified_name() +
        "' references a value outside this compiler's module closure");
    return std::nullopt;
  }
  Attribute attribute = detail::TypeAccess::make(schema, std::move(*values));
  const auto verifier = state_->attribute_verifiers.find(symbol.stable_name());
  if (verifier != state_->attribute_verifiers.end()) {
    Diagnostics reported;
    const bool accepted = verifier->second(attribute, reported);
    if (!accepted && reported.ok()) {
      reported.report("attribute verifier rejected '" +
                      symbol.qualified_name() + "'");
    }
    for (const Diagnostic& entry : reported.entries()) {
      state_->diagnostics.report(entry);
    }
    if (!accepted || !reported.ok()) {
      return std::nullopt;
    }
  }
  return attribute;
}

std::optional<Graph> Compiler::graph() {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot create a graph before the compiler is linked");
    return std::nullopt;
  }
  std::vector<Module> modules;
  modules.reserve(state_->modules.size());
  for (const auto& [name, module] : state_->modules) {
    static_cast<void>(name);
    modules.push_back(module);
  }
  return Graph(std::move(modules));
}

std::optional<Graph> Compiler::graph(std::string_view name) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot construct a graph before the compiler is linked");
    return std::nullopt;
  }
  const auto member = qualified_member(name);
  if (!member) {
    state_->diagnostics.report("graph name '" + std::string(name) +
                               "' must be qualified as module.member");
    return std::nullopt;
  }
  const auto [module_name, member_name] = *member;
  const auto owner = state_->modules.find(module_name);
  if (owner == state_->modules.end()) {
    state_->diagnostics.report("graph '" + std::string(name) +
                               "' names an unlinked module");
    return std::nullopt;
  }
  const auto symbol =
      owner->second.symbol(Module::SymbolKind::Graph, member_name);
  if (!symbol) {
    state_->diagnostics.report("unknown graph '" + std::string(name) + "'");
    return std::nullopt;
  }
  return graph(*symbol);
}

std::optional<Graph> Compiler::graph(Module::Symbol symbol) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot construct a graph before the compiler is linked");
    return std::nullopt;
  }
  if (symbol.kind() != Module::SymbolKind::Graph) {
    state_->diagnostics.report("symbol '" + symbol.qualified_name() +
                               "' is not a graph");
    return std::nullopt;
  }
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("graph '" + symbol.qualified_name() +
                               "' is not in this compilation");
    return std::nullopt;
  }
  const auto definition =
      detail::ModuleAccess::graph(owner->second, symbol.local_name());
  if (!definition) {
    state_->diagnostics.report("unknown graph '" + symbol.qualified_name() +
                               "'");
    return std::nullopt;
  }
  return detail::instantiate_graph(*this, *definition, owner->second.name(),
                                   state_->diagnostics);
}

bool Compiler::conforms(const Module::TypeDecl& declaration,
                        const Module::InterfaceDecl& interface) const {
  return state_->linked &&
         conforms_to_interface(state_->modules, declaration.symbol(),
                               declaration.interfaces(), interface,
                               Module::SymbolKind::Type);
}

bool Compiler::conforms(const Module::AttributeDecl& declaration,
                        const Module::InterfaceDecl& interface) const {
  return state_->linked &&
         conforms_to_interface(state_->modules, declaration.symbol(),
                               declaration.interfaces(), interface,
                               Module::SymbolKind::Attribute);
}

bool Compiler::conforms(const Module::OperationDecl& declaration,
                        const Module::InterfaceDecl& interface) const {
  return state_->linked &&
         conforms_to_interface(state_->modules, declaration.symbol(),
                               declaration.interfaces(), interface,
                               Module::SymbolKind::Operation);
}

bool Compiler::check_method_result(Module::InterfaceDecl::MethodDecl method,
                                   const Module::ParameterDecl& result) {
  if (method.result_kind() == result.kind &&
      method.result_is_list() == result.list) {
    return true;
  }
  state_->diagnostics.report("typed result for interface method '" +
                             method.qualified_name() +
                             "' does not match its declaration");
  return false;
}

bool Compiler::check_method_signature(
    Module::InterfaceDecl::MethodDecl method,
    std::span<const Module::ParameterDecl> parameters,
    const Module::ParameterDecl& result) {
  if (!check_method_result(method, result)) {
    return false;
  }
  const auto declared = method.parameters();
  if (declared.size() != parameters.size()) {
    state_->diagnostics.report("typed binding for interface method '" +
                               method.qualified_name() +
                               "' has the wrong argument count");
    return false;
  }
  for (std::size_t index = 0; index < declared.size(); ++index) {
    if (declared[index].kind != parameters[index].kind ||
        declared[index].list != parameters[index].list) {
      state_->diagnostics.report(
          "typed binding for interface method '" + method.qualified_name() +
          "' disagrees with argument " + std::to_string(index + 1U));
      return false;
    }
  }
  return true;
}

void Compiler::bind_method(Module::TypeDecl declaration,
                           Module::InterfaceDecl::MethodDecl method,
                           MethodFunction<Type> function) {
  bind_interface_method(
      state_->linked, std::move(declaration), std::move(method),
      std::move(function), state_->type_methods, state_->diagnostics,
      [this](const auto& subject, const auto& interface) {
        return conforms(subject, interface);
      },
      "type");
}

void Compiler::bind_method(Module::AttributeDecl declaration,
                           Module::InterfaceDecl::MethodDecl method,
                           MethodFunction<Attribute> function) {
  bind_interface_method(
      state_->linked, std::move(declaration), std::move(method),
      std::move(function), state_->attribute_methods, state_->diagnostics,
      [this](const auto& subject, const auto& interface) {
        return conforms(subject, interface);
      },
      "attribute");
}

void Compiler::bind_method(Module::OperationDecl declaration,
                           Module::InterfaceDecl::MethodDecl method,
                           MethodFunction<Operation> function) {
  bind_interface_method(
      state_->linked, std::move(declaration), std::move(method),
      std::move(function), state_->operation_methods, state_->diagnostics,
      [this](const auto& subject, const auto& interface) {
        return conforms(subject, interface);
      },
      "operation");
}

std::optional<ParameterValue>
Compiler::call(const Type& subject, Module::InterfaceDecl::MethodDecl method,
               std::span<const ParameterValue> parameters) {
  return evaluate_interface_method(
      state_->linked, subject, std::move(method), parameters,
      state_->type_methods, state_->modules, state_->diagnostics,
      [this](const auto& declaration, const auto& interface) {
        return conforms(declaration, interface);
      },
      "type");
}

std::optional<ParameterValue>
Compiler::call(const Attribute& subject,
               Module::InterfaceDecl::MethodDecl method,
               std::span<const ParameterValue> parameters) {
  return evaluate_interface_method(
      state_->linked, subject, std::move(method), parameters,
      state_->attribute_methods, state_->modules, state_->diagnostics,
      [this](const auto& declaration, const auto& interface) {
        return conforms(declaration, interface);
      },
      "attribute");
}

std::optional<ParameterValue>
Compiler::call(const Operation& subject,
               Module::InterfaceDecl::MethodDecl method,
               std::span<const ParameterValue> parameters) {
  return evaluate_interface_method(
      state_->linked, subject, std::move(method), parameters,
      state_->operation_methods, state_->modules, state_->diagnostics,
      [this](const auto& declaration, const auto& interface) {
        return conforms(declaration, interface);
      },
      "operation");
}

void Compiler::bind_verifier(Module::TypeDecl schema,
                             VerifierFunction<Type> verifier) {
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
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

void Compiler::bind_verifier(Module::AttributeDecl schema,
                             VerifierFunction<Attribute> verifier) {
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("cannot bind attribute '" +
                               symbol.qualified_name() +
                               "' outside this compiler");
    return;
  }
  if (!verifier) {
    state_->diagnostics.report("attribute verifier binding is empty");
    return;
  }
  if (!state_->attribute_verifiers
           .emplace(symbol.stable_name(), std::move(verifier))
           .second) {
    state_->diagnostics.report("attribute '" + symbol.qualified_name() +
                               "' already has a verifier binding");
  }
}

void Compiler::bind_verifier(Module::OperationDecl schema,
                             VerifierFunction<Operation> verifier) {
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("cannot bind operation '" +
                               symbol.qualified_name() +
                               "' outside this compiler");
    return;
  }
  if (!verifier) {
    state_->diagnostics.report("operation verifier binding is empty");
    return;
  }
  if (!state_->operation_verifiers
           .emplace(symbol.stable_name(), std::move(verifier))
           .second) {
    state_->diagnostics.report("operation '" + symbol.qualified_name() +
                               "' already has a verifier binding");
  }
}

void Compiler::bind_pass(Module::PassDecl schema, PassFunction function) {
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("cannot bind pass '" + symbol.qualified_name() +
                               "' outside this compiler");
    return;
  }
  if (schema.form() != Module::PassDecl::Form::External) {
    state_->diagnostics.report("text-defined pass '" + symbol.qualified_name() +
                               "' cannot receive a C++ binding");
    return;
  }
  if (!function) {
    state_->diagnostics.report("pass binding is empty");
    return;
  }
  if (!state_->passes.emplace(symbol.stable_name(), std::move(function))
           .second) {
    state_->diagnostics.report("pass '" + symbol.qualified_name() +
                               "' already has a binding");
  }
}

std::optional<Module> Compiler::lookup_module(const Module& module) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot bind behavior before the compiler is linked");
    return std::nullopt;
  }
  const auto found = state_->modules.find(module.name());
  if (found == state_->modules.end() || found->second != module) {
    state_->diagnostics.report("module '" + module_identity(module) +
                               "' is not part of this compiler");
    return std::nullopt;
  }
  return found->second;
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(const Module& module, std::string_view reference,
                        Module::SymbolKind subject) {
  const auto scope = lookup_module(module);
  if (!scope) {
    return std::nullopt;
  }
  const std::size_t method_separator = reference.rfind('.');
  if (method_separator == std::string_view::npos || method_separator == 0U ||
      method_separator + 1U == reference.size()) {
    state_->diagnostics.report("method '" + std::string(reference) +
                               "' must be written as interface.method");
    return std::nullopt;
  }
  const std::string_view interface_reference =
      reference.substr(0U, method_separator);
  const std::string_view method_name = reference.substr(method_separator + 1U);
  const std::size_t interface_separator = interface_reference.find('.');
  std::string_view owner_name = scope->name();
  std::string_view interface_name = interface_reference;
  if (interface_separator != std::string_view::npos) {
    const std::string_view prefix =
        interface_reference.substr(0U, interface_separator);
    interface_name = interface_reference.substr(interface_separator + 1U);
    const auto imported =
        std::find_if(scope->imports().begin(), scope->imports().end(),
                     [&](const Module::Import& import) {
                       return import.prefix() == prefix;
                     });
    if (imported == scope->imports().end() ||
        interface_name.find('.') != std::string_view::npos) {
      state_->diagnostics.report("interface '" +
                                 std::string(interface_reference) +
                                 "' is not local or directly imported by '" +
                                 std::string(scope->name()) + "'");
      return std::nullopt;
    }
    owner_name = imported->name;
  }
  const auto owner = state_->modules.find(owner_name);
  const auto interface = owner == state_->modules.end()
                             ? std::optional<Module::InterfaceDecl>{}
                             : owner->second.interface(interface_name);
  if (!interface) {
    state_->diagnostics.report("unknown interface '" +
                               std::string(interface_reference) + "'");
    return std::nullopt;
  }
  if (interface->subject() != subject) {
    state_->diagnostics.report("interface '" +
                               std::string(interface_reference) +
                               "' has the wrong subject kind");
    return std::nullopt;
  }
  const auto method = interface->method(method_name);
  if (!method) {
    state_->diagnostics.report(
        "interface '" + std::string(interface_reference) +
        "' has no method named '" + std::string(method_name) + "'");
  }
  return method;
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(const Module& module, std::string_view declaration,
                        std::span<const std::string> interfaces,
                        std::string_view reference,
                        Module::SymbolKind subject) {
  if (reference.find('.') != std::string_view::npos) {
    return lookup_method(module, reference, subject);
  }
  const auto scope = lookup_module(module);
  if (!scope) {
    return std::nullopt;
  }

  std::vector<Module::InterfaceDecl::MethodDecl> matches;
  std::vector<std::string> owners;
  for (const std::string& interface_reference : interfaces) {
    const std::size_t separator = interface_reference.find('.');
    std::string_view owner_name = scope->name();
    std::string_view interface_name = interface_reference;
    if (separator != std::string::npos) {
      const std::string_view prefix =
          std::string_view(interface_reference).substr(0U, separator);
      interface_name =
          std::string_view(interface_reference).substr(separator + 1U);
      const auto imported =
          std::find_if(scope->imports().begin(), scope->imports().end(),
                       [&](const Module::Import& import) {
                         return import.prefix() == prefix;
                       });
      if (imported == scope->imports().end()) {
        continue;
      }
      owner_name = imported->name;
    }
    const auto owner = state_->modules.find(owner_name);
    const auto interface = owner == state_->modules.end()
                               ? std::optional<Module::InterfaceDecl>{}
                               : owner->second.interface(interface_name);
    if (!interface || interface->subject() != subject) {
      continue;
    }
    if (const auto method = interface->method(reference)) {
      matches.push_back(*method);
      owners.push_back(interface_reference);
    }
  }

  const std::string binding =
      std::string(declaration) + "." + std::string(reference);
  if (matches.empty()) {
    state_->diagnostics.report("declaration '" + std::string(declaration) +
                               "' has no interface method named '" +
                               std::string(reference) + "'");
    return std::nullopt;
  }
  if (matches.size() > 1U) {
    std::string message =
        "method binding '" + binding + "' is ambiguous; qualify it as one of";
    for (const std::string& owner : owners) {
      message += " '" + std::string(declaration) + "." + owner + "." +
                 std::string(reference) + "'";
    }
    state_->diagnostics.report(std::move(message));
    return std::nullopt;
  }
  return matches.front();
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(Module::TypeDecl declaration,
                        std::string_view reference) {
  const auto symbol = declaration.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (!state_->linked || owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("type '" + symbol.qualified_name() +
                               "' is not in this compilation");
    return std::nullopt;
  }
  return lookup_method(owner->second, declaration.name(),
                       declaration.interfaces(), reference,
                       Module::SymbolKind::Type);
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(Module::AttributeDecl declaration,
                        std::string_view reference) {
  const auto symbol = declaration.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (!state_->linked || owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("attribute '" + symbol.qualified_name() +
                               "' is not in this compilation");
    return std::nullopt;
  }
  return lookup_method(owner->second, declaration.name(),
                       declaration.interfaces(), reference,
                       Module::SymbolKind::Attribute);
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(Module::OperationDecl declaration,
                        std::string_view reference) {
  const auto symbol = declaration.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (!state_->linked || owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("operation '" + symbol.qualified_name() +
                               "' is not in this compilation");
    return std::nullopt;
  }
  return lookup_method(owner->second, declaration.name(),
                       declaration.interfaces(), reference,
                       Module::SymbolKind::Operation);
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(const Type& subject, std::string_view reference) {
  return lookup_method(subject.schema(), reference);
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(const Attribute& subject, std::string_view reference) {
  return lookup_method(subject.schema(), reference);
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(const Operation& subject, std::string_view reference) {
  return lookup_method(subject.schema(), reference);
}

bool Compiler::prepare_query(const Graph& graph) { return verify(graph); }

Diagnostics& Compiler::query_diagnostics() { return state_->diagnostics; }

bool Compiler::verify(const Graph& graph) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot verify a graph before the compiler is linked");
    return false;
  }
  if (!detail::GraphAccess::verify_structure(graph, state_->diagnostics)) {
    return false;
  }
  bool valid =
      detail::GraphAccess::verify_contracts(graph, state_->diagnostics);
  const auto verify_region = [&](const auto& self,
                                 const Region& region) -> void {
    for (const Operation& operation : region.operations()) {
      const Module::OperationDecl schema = operation.schema();
      const Module::Symbol symbol = schema.symbol();
      const auto location = detail::GraphAccess::location(operation);
      const auto verifier =
          state_->operation_verifiers.find(symbol.stable_name());
      if (verifier != state_->operation_verifiers.end()) {
        Diagnostics reported;
        const bool accepted = verifier->second(operation, reported);
        if (!accepted && reported.ok()) {
          reported.report("operation verifier rejected '" +
                          symbol.qualified_name() + "'");
        }
        for (const Diagnostic& entry : reported.entries()) {
          Diagnostic diagnostic = entry;
          if (!diagnostic.source) {
            diagnostic.source = location;
          }
          state_->diagnostics.report(std::move(diagnostic));
        }
        if (!accepted || !reported.ok()) {
          valid = false;
        }
      }
      for (const Region& nested : operation.regions()) {
        self(self, nested);
      }
    }
  };
  verify_region(verify_region, detail::GraphAccess::root(graph));
  return valid;
}

bool Compiler::run(Graph& graph, Module::PassDecl pass) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot run a pass before the compiler is linked");
    return false;
  }
  const Module::Symbol symbol = pass.symbol();
  if (!graph.accepts(symbol)) {
    state_->diagnostics.report("pass '" + symbol.qualified_name() +
                               "' is outside the graph's module closure");
    return false;
  }
  if (!verify(graph)) {
    return false;
  }

  const auto snapshot = graph.snapshot();
  const std::size_t before = state_->diagnostics.size();
  bool succeeded = false;
  try {
    const auto execute = [&](const auto& self,
                             const Module::PassDecl& current) -> bool {
      const Module::Symbol current_symbol = current.symbol();
      if (!graph.accepts(current_symbol)) {
        state_->diagnostics.report("pass '" + current_symbol.qualified_name() +
                                   "' is outside the graph's module closure");
        return false;
      }
      switch (current.form()) {
      case Module::PassDecl::Form::External: {
        const auto binding = state_->passes.find(current_symbol.stable_name());
        if (binding == state_->passes.end()) {
          state_->diagnostics.report("pass '" +
                                     current_symbol.qualified_name() +
                                     "' has no C++ binding");
          return false;
        }
        return binding->second(*this, graph, state_->diagnostics);
      }
      case Module::PassDecl::Form::Rules: {
        const auto current_module =
            state_->modules.find(current_symbol.module_name());
        if (current_module == state_->modules.end()) {
          state_->diagnostics.report("pass owner '" +
                                     std::string(current_symbol.module_name()) +
                                     "' is not linked");
          return false;
        }
        const auto rules =
            detail::ModuleAccess::rules(current_module->second, current);
        for (const detail::RuleDefinition& rule : rules) {
          if (!apply_contraction_rule(graph, current, rule, state_->modules,
                                      state_->diagnostics)) {
            return false;
          }
        }
        return true;
      }
      case Module::PassDecl::Form::Sequence:
        for (const std::string& step : current.steps()) {
          const auto next =
              resolve_pass(state_->modules, current_symbol.module_name(), step);
          if (!next) {
            state_->diagnostics.report("pass '" +
                                       current_symbol.qualified_name() +
                                       "' cannot resolve step '" + step + "'");
            return false;
          }
          if (!self(self, *next)) {
            return false;
          }
        }
        return true;
      }
      return false;
    };
    succeeded = execute(execute, pass);
  } catch (...) {
    graph.restore(snapshot);
    throw;
  }
  if (!succeeded || state_->diagnostics.size() != before) {
    graph.restore(snapshot);
    if (!succeeded && state_->diagnostics.size() == before) {
      state_->diagnostics.report("pass '" + symbol.qualified_name() +
                                 "' failed");
    }
    return false;
  }
  if (!verify(graph)) {
    graph.restore(snapshot);
    return false;
  }
  return true;
}

bool Compiler::run(Graph& graph, std::string_view pass) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot run a pass before the compiler is linked");
    return false;
  }
  const auto member = qualified_member(pass);
  if (!member) {
    state_->diagnostics.report("pass name '" + std::string(pass) +
                               "' must be qualified as module.member");
    return false;
  }
  const auto [module_name, member_name] = *member;
  const auto owner = state_->modules.find(module_name);
  if (owner == state_->modules.end()) {
    state_->diagnostics.report("pass '" + std::string(pass) +
                               "' names an unlinked module");
    return false;
  }
  const auto declaration = owner->second.pass(member_name);
  if (!declaration) {
    state_->diagnostics.report("unknown pass '" + std::string(pass) + "'");
    return false;
  }
  return run(graph, *declaration);
}

const Diagnostics& Compiler::diagnostics() const { return state_->diagnostics; }

}  // namespace joggle
