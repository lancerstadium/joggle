#include "joggle/compiler.h"

#include "call_resolution.h"
#include "declaration_check.h"
#include "domain.h"
#include "execution.h"
#include "expression_syntax.h"
#include "function_body.h"
#include "ir_internal.h"
#include "joggle/behavior.h"
#include "joggle/module.h"
#include "module_internal.h"
#include "module_repository.h"
#include "prelude.h"
#include "prelude_runtime.h"
#include "type_contract.h"
#include "type_internal.h"

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

bool accepts_known_value(const Type& type, const ParameterValue& value) {
  const Module::Symbol symbol = type.schema().symbol();
  if (symbol.module_name() != detail::prelude_module_name) {
    return false;
  }
  const std::string_view name = symbol.local_name();
  if (name == "int" || name == "i8" || name == "i16" || name == "i32" ||
      name == "i64" || name == "index") {
    return value.kind() == ParameterValue::Kind::I64;
  }
  if (name == "u8" || name == "u16" || name == "u32" || name == "u64") {
    return value.kind() == ParameterValue::Kind::I64 && *value.as_i64() >= 0;
  }
  if (name == "real" || name == "f16" || name == "bf16" || name == "f32" ||
      name == "f64") {
    return value.kind() == ParameterValue::Kind::I64 ||
           value.kind() == ParameterValue::Kind::F64;
  }
  if (name == "bool" || name == "i1") {
    return value.kind() == ParameterValue::Kind::Boolean;
  }
  if (name == "string") {
    return value.kind() == ParameterValue::Kind::String;
  }
  if (name == "type") {
    return value.kind() == ParameterValue::Kind::Type;
  }
  if (name == "attr") {
    return value.kind() == ParameterValue::Kind::Attribute;
  }
  if (name != "list" || value.kind() != ParameterValue::Kind::List) {
    return false;
  }
  const auto parameters = detail::TypeAccess::parameters(type);
  if (parameters.size() != 1U || parameters.front().as_type() == nullptr) {
    return false;
  }
  return std::all_of(value.elements().begin(), value.elements().end(),
                     [&](const ParameterValue& element) {
                       return accepts_known_value(*parameters.front().as_type(),
                                                  element);
                     });
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

std::optional<detail::Domain>
parameter_domain(const Module::ParameterDecl& field) {
  return detail::kernel_domain(field.domain);
}

std::string_view resolve_prefix(const Module& module, std::string_view prefix);

template <typename Modules>
std::optional<Module::TypeDecl>
field_type_declaration(const Modules& modules,
                       const Module::FunctionDecl& function,
                       const Module::ParameterDecl& field) {
  if (field.domain.kind != Module::Expression::Kind::Reference ||
      detail::kernel_domain(field.domain)) {
    return std::nullopt;
  }
  const std::size_t dot = field.domain.text.find('.');
  std::string_view module_name = function.symbol().module_name();
  const auto owner = modules.find(module_name);
  if (dot != std::string::npos) {
    module_name = owner == modules.end()
                      ? std::string_view(field.domain.text).substr(0, dot)
                      : resolve_prefix(
                            owner->second,
                            std::string_view(field.domain.text).substr(0, dot));
  }
  const std::string_view local =
      dot == std::string::npos
          ? std::string_view(field.domain.text)
          : std::string_view(field.domain.text).substr(dot + 1U);
  const auto module = modules.find(module_name);
  return module == modules.end() ? std::nullopt : module->second.type(local);
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
  return method.results().size() == 1U &&
         detail::matches_parameter(method.results().front(), value);
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
  if constexpr (std::is_same_v<Subject, ir::Instruction>) {
    if (!subject.valid()) {
      diagnostics.report("invalid Instruction used as an interface subject");
      return std::nullopt;
    }
  }
  const auto declaration = [&] {
    if constexpr (std::is_same_v<Subject, ir::Instruction>) {
      return subject.callee();
    } else {
      return subject.schema();
    }
  }();
  if (!linked || !conforms(declaration, method.owner())) {
    diagnostics.report(std::string(subject_kind) + " '" +
                       declaration.symbol().qualified_name() +
                       "' does not provide interface method '" +
                       method.qualified_name() + "'");
    return std::nullopt;
  }
  auto arguments = detail::validate_parameters(
      method.qualified_name(), method.inputs(), parameters, diagnostics);
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
  if constexpr (std::is_same_v<Subject, ir::Instruction>) {
    location = detail::FunctionAccess::location(subject);
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
  struct HostRepresentation {
    Module::TypeDecl schema;
    RepresentationProjector project;
  };
  struct BoundFunction {
    NativeFunction callable;
    HostEvaluation evaluation = HostEvaluation::Guarded;
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
  std::map<std::string, VerifierFunction<ir::Instruction>, std::less<>>
      instruction_verifiers;
  std::map<std::string, MethodFunction<Attribute>, std::less<>>
      attribute_methods;
  std::map<std::string, MethodFunction<ir::Instruction>, std::less<>>
      instruction_methods;
  std::map<std::string, BoundFunction, std::less<>> bindings;
  std::map<std::string, detail::ParameterValue, std::less<>>
      hermetic_evaluations;
  std::map<std::string, HostRepresentation, std::less<>> host_types;
  std::map<std::string, std::string, std::less<>> host_representations;
  std::set<std::string, std::less<>> constructing_types;
  EvaluationLimits evaluation_limits;
  bool linked = false;
};

Compiler::Compiler() : Compiler(EvaluationLimits{}) {}

Compiler::Compiler(EvaluationLimits limits)
    : state_(std::make_unique<State>()) {
  state_->evaluation_limits = limits;
  auto prelude = parse_module(detail::prelude_module_source(),
                              state_->diagnostics, "<prelude>");
  if (prelude) {
    add_module(std::move(*prelude), false, std::nullopt);
    bind_prelude_module();
    bind_prelude_primitives();
  }
}
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
      if (name == detail::prelude_module_name) {
        continue;
      }
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

    for (const Module::TypeDecl& type : module.types()) {
      const auto location = detail::ModuleAccess::declaration_source(
          module, Module::SymbolKind::Type, type.name());
      std::vector<Module::ParameterDecl> required_fields;
      for (const std::string& reference : type.interfaces()) {
        const std::size_t dot = reference.find('.');
        const std::string interface_owner =
            dot == std::string::npos
                ? name
                : std::string(resolve_prefix(
                      module, std::string_view(reference).substr(0, dot)));
        const std::string local =
            dot == std::string::npos ? reference : reference.substr(dot + 1U);
        const auto source = state_->modules.find(interface_owner);
        const auto interface = source == state_->modules.end()
                                   ? std::optional<Module::InterfaceDecl>{}
                                   : source->second.interface(local);
        if (interface) {
          required_fields.insert(required_fields.end(),
                                 interface->fields().begin(),
                                 interface->fields().end());
        }
      }
      for (const auto& field : required_fields) {
        const auto parameter =
            std::find_if(type.parameters().begin(), type.parameters().end(),
                         [&](const auto& candidate) {
                           return candidate.name == field.name &&
                                  candidate.domain == field.domain;
                         });
        const auto supplied = std::find_if(
            type.derived_parameters().begin(), type.derived_parameters().end(),
            [&](const auto& candidate) {
              return candidate.name == field.name;
            });
        if (parameter == type.parameters().end() &&
            supplied == type.derived_parameters().end()) {
          state_->diagnostics.report(
              "type '" + name + "." + std::string(type.name()) +
                  "' does not define required parameter '" + field.name + "'",
              location);
        }
      }
      for (const auto& derived : type.derived_parameters()) {
        std::vector<Module::ParameterDecl> fields;
        for (const std::string& reference : type.interfaces()) {
          const std::size_t dot = reference.find('.');
          const std::string interface_owner =
              dot == std::string::npos
                  ? name
                  : std::string(resolve_prefix(
                        module, std::string_view(reference).substr(0, dot)));
          const std::string local =
              dot == std::string::npos ? reference : reference.substr(dot + 1U);
          const auto source = state_->modules.find(interface_owner);
          const auto interface = source == state_->modules.end()
                                     ? std::optional<Module::InterfaceDecl>{}
                                     : source->second.interface(local);
          if (interface) {
            const auto field = std::find_if(
                interface->fields().begin(), interface->fields().end(),
                [&](const auto& candidate) {
                  return candidate.name == derived.name;
                });
            if (field != interface->fields().end()) {
              fields.push_back(*field);
            }
          }
        }
        if (fields.size() != 1U) {
          state_->diagnostics.report(
              fields.empty()
                  ? "derived parameter '" + derived.name + "' on type '" +
                        name + "." + std::string(type.name()) +
                        "' is not declared by one of its interfaces"
                  : "derived parameter '" + derived.name + "' on type '" +
                        name + "." + std::string(type.name()) +
                        "' is ambiguous across its interfaces",
              location);
          continue;
        }
        detail::check_declaration_expression(
            *this, module, derived.value, fields.front().domain, {},
            type.parameters(), state_->diagnostics, location,
            "derived field '" + name + "." + std::string(type.name()) + "." +
                derived.name + "'");
      }
    }

    for (const Module::FunctionDecl& function : module.declarations()) {
      const auto location = detail::ModuleAccess::declaration_source(
          module, Module::SymbolKind::Function, function.name());
      validate_interfaces(function.interfaces(), Module::SymbolKind::Function,
                          function.name(), location);
      detail::check_generic_constraints(
          *this, module, function.generics(), state_->diagnostics, location,
          "function '" + name + "." + std::string(function.name()) + "'");
      const auto body = detail::ModuleAccess::body(module, function);
      if (body) {
        detail::verify_body_calls(*this, function, *body, state_->diagnostics);
      }
    }

    for (const Module::FunctionDecl& function : module.declarations()) {
      if (detail::ModuleAccess::expression(function) == nullptr ||
          !detail::ir_inputs(function).empty() ||
          !detail::ir_results(function).empty() ||
          detail::parameter_results(function).size() != 1U) {
        continue;
      }
      const auto location = detail::ModuleAccess::declaration_source(
          module, Module::SymbolKind::Function, function.name());
      const auto inputs = detail::parameter_inputs(function);
      const auto results = detail::parameter_results(function);
      detail::check_declaration_expression(
          *this, module, *detail::ModuleAccess::expression(function),
          results.front().domain, function.generics(), inputs,
          state_->diagnostics, location,
          "function '" + name + "." + std::string(function.name()) + "'");
    }
    for (const Module::FunctionDecl& declaration : module.declarations()) {
      if (detail::ir_inputs(declaration).empty() &&
          detail::ir_results(declaration).empty()) {
        continue;
      }
      const auto function_source = detail::ModuleAccess::declaration_source(
          module, Module::SymbolKind::Function, declaration.name());
      const auto report_function = [&](std::string message) {
        state_->diagnostics.report(std::move(message), function_source);
      };
      const auto& contract = detail::FunctionTypeAccess::get(declaration);
      if (!contract.bindings.empty() &&
          contract.bindings.size() != declaration.inputs().size()) {
        report_function("function '" + name + "." +
                        std::string(declaration.name()) +
                        "' has an invalid type contract");
        continue;
      }
      const auto type_domain =
          detail::domain_expression(detail::ValueKind::Type);
      const std::string subject =
          "function '" + name + "." + std::string(declaration.name()) + "'";
      for (const auto& input : detail::ir_inputs(declaration)) {
        detail::check_declaration_expression(
            *this, module, input.domain, type_domain, contract.generics, {},
            state_->diagnostics, function_source, subject);
      }
      for (std::size_t index = 0; index < declaration.inputs().size();
           ++index) {
        if (!contract.bindings.empty() && contract.bindings[index]) {
          detail::check_declaration_expression(
              *this, module, *contract.bindings[index],
              declaration.inputs()[index].domain, contract.generics, {},
              state_->diagnostics, function_source, subject);
        }
      }
      for (const auto& result : detail::ir_results(declaration)) {
        detail::check_declaration_expression(
            *this, module, result.domain, type_domain, contract.generics, {},
            state_->diagnostics, function_source, subject);
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
  const auto visit_function = [&](const auto& self,
                                  const Module::FunctionDecl& function,
                                  std::optional<SourceRange> incoming) -> bool {
    const std::string identity(function.symbol().qualified_name());
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
      state_->diagnostics.report("pure function cycle: " + cycle, incoming);
      return false;
    }
    state = Visit::Active;
    stack.push_back(identity);
    const auto owner = state_->modules.find(function.symbol().module_name());
    const auto location =
        owner == state_->modules.end()
            ? std::optional<SourceRange>{}
            : detail::ModuleAccess::declaration_source(
                  owner->second, Module::SymbolKind::Function, function.name());
    bool valid = true;
    const auto walk = [&](const auto& walk_self,
                          const Module::Expression& expression) -> void {
      std::vector<Module::FunctionDecl> targets;
      if (expression.kind == Module::Expression::Kind::Call &&
          owner != state_->modules.end()) {
        targets = detail::visible_functions(*this, owner->second.name(),
                                            expression.text);
      } else if (owner != state_->modules.end() &&
                 (expression.kind == Module::Expression::Kind::Prefix ||
                  expression.kind == Module::Expression::Kind::Infix ||
                  expression.kind == Module::Expression::Kind::Postfix)) {
        const auto fixity =
            expression.kind == Module::Expression::Kind::Prefix
                ? Module::FunctionDecl::Fixity::Prefix
            : expression.kind == Module::Expression::Kind::Postfix
                ? Module::FunctionDecl::Fixity::Postfix
                : Module::FunctionDecl::Fixity::Infix;
        targets = detail::visible_operators(*this, owner->second.name(),
                                            expression.text, fixity);
      }
      if (targets.size() == 1U &&
          targets.front().form() == Module::FunctionDecl::Form::Body &&
          !self(self, targets.front(), location)) {
        valid = false;
      }
      for (const auto& argument : expression.arguments) {
        walk_self(walk_self, argument);
      }
    };
    if (const auto* expression = detail::ModuleAccess::expression(function)) {
      walk(walk, *expression);
    }
    stack.pop_back();
    state = Visit::Complete;
    return valid;
  };

  for (const auto& [name, module] : state_->modules) {
    static_cast<void>(name);
    for (const Module::FunctionDecl& function : module.declarations()) {
      if (!visit_function(visit_function, function, std::nullopt)) {
        return false;
      }
    }
  }

  state_->linked = true;
  return true;
}

bool Compiler::ok() const { return state_->diagnostics.ok(); }

bool Compiler::linked() const { return state_->linked; }

Compiler::EvaluationLimits Compiler::evaluation_limits() const {
  return state_->evaluation_limits;
}

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
    if (name == detail::prelude_module_name) {
      continue;
    }
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
  auto instruction_verifiers = state_->instruction_verifiers;
  auto attribute_methods = state_->attribute_methods;
  auto instruction_methods = state_->instruction_methods;
  auto bindings = state_->bindings;
  auto host_types = state_->host_types;
  auto host_representations = state_->host_representations;
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
    state_->instruction_verifiers = std::move(instruction_verifiers);
    state_->attribute_methods = std::move(attribute_methods);
    state_->instruction_methods = std::move(instruction_methods);
    state_->bindings = std::move(bindings);
    state_->host_types = std::move(host_types);
    state_->host_representations = std::move(host_representations);
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
  std::string construction = symbol.stable_name();
  for (const ParameterValue& value : *values) {
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

std::optional<Type> Compiler::make(std::string_view prelude_type) {
  if (!detail::is_prelude_type(prelude_type)) {
    state_->diagnostics.report("unknown Prelude type '" +
                               std::string(prelude_type) + "'");
    return std::nullopt;
  }
  const auto owner = state_->modules.find(detail::prelude_module_name);
  const auto declaration = owner == state_->modules.end()
                               ? std::optional<Module::TypeDecl>{}
                               : owner->second.type(prelude_type);
  if (!declaration) {
    state_->diagnostics.report("Prelude type '" + std::string(prelude_type) +
                               "' is unavailable");
    return std::nullopt;
  }
  return make(*declaration, std::span<const ParameterValue>{});
}

std::optional<ir::Value> Compiler::make_known(Type type, ParameterValue value) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot create a Known value before the compiler is linked");
    return std::nullopt;
  }
  if (!belongs_to(state_->modules, ParameterValue(type)) ||
      !belongs_to(state_->modules, value)) {
    state_->diagnostics.report(
        "Known value references a declaration outside this compiler");
    return std::nullopt;
  }
  if (!accepts_known_value(type, value)) {
    state_->diagnostics.report("Known value payload does not match type '" +
                               type.schema().symbol().qualified_name() + "'");
    return std::nullopt;
  }
  return ir::Value(std::move(type), std::move(value));
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

std::optional<ir::Function> Compiler::function() {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot create a function before the compiler is linked");
    return std::nullopt;
  }
  std::vector<Module> modules;
  modules.reserve(state_->modules.size());
  for (const auto& [name, module] : state_->modules) {
    static_cast<void>(name);
    modules.push_back(module);
  }
  return ir::Function(std::move(modules));
}

std::optional<ir::Function>
Compiler::function(Module::FunctionDecl declaration) {
  return function(std::move(declaration), {});
}

std::optional<ir::Function>
Compiler::function(Module::FunctionDecl declaration,
                   std::vector<ir::Value> known_arguments) {
  return function(declaration.symbol(), std::move(known_arguments));
}

std::optional<ir::Function> Compiler::function(std::string_view name) {
  return function(name, {});
}

std::optional<ir::Function>
Compiler::function(std::string_view name,
                   std::vector<ir::Value> known_arguments) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot construct a function before the compiler is linked");
    return std::nullopt;
  }
  const auto member = qualified_member(name);
  if (!member) {
    state_->diagnostics.report("function name '" + std::string(name) +
                               "' must be qualified as module.member");
    return std::nullopt;
  }
  const auto [module_name, member_name] = *member;
  const auto owner = state_->modules.find(module_name);
  if (owner == state_->modules.end()) {
    state_->diagnostics.report("function '" + std::string(name) +
                               "' names an unlinked module");
    return std::nullopt;
  }
  const auto symbol =
      owner->second.symbol(Module::SymbolKind::Function, member_name);
  if (!symbol) {
    state_->diagnostics.report("unknown function '" + std::string(name) + "'");
    return std::nullopt;
  }
  return function(*symbol, std::move(known_arguments));
}

std::optional<ir::Function> Compiler::function(Module::Symbol symbol) {
  return function(std::move(symbol), {});
}

std::optional<ir::Function>
Compiler::function(Module::Symbol symbol,
                   std::vector<ir::Value> known_arguments) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot construct a function before the compiler is linked");
    return std::nullopt;
  }
  if (symbol.kind() != Module::SymbolKind::Function) {
    state_->diagnostics.report("symbol '" + symbol.qualified_name() +
                               "' is not a function");
    return std::nullopt;
  }
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("function '" + symbol.qualified_name() +
                               "' is not in this compilation");
    return std::nullopt;
  }
  const auto overloads = owner->second.overloads(symbol.local_name());
  const auto function = std::find_if(
      overloads.begin(), overloads.end(),
      [&](const Module::FunctionDecl& candidate) {
        return candidate.symbol() == symbol &&
               candidate.form() == Module::FunctionDecl::Form::Body;
      });
  const auto definition =
      function == overloads.end()
          ? std::shared_ptr<const detail::FunctionBody>{}
          : detail::ModuleAccess::body(owner->second, *function);
  const bool compile_time_only =
      function != overloads.end() && detail::ir_inputs(*function).empty() &&
      detail::ir_results(*function).empty() &&
      detail::ModuleAccess::expression(*function) != nullptr;
  if (!definition || compile_time_only) {
    state_->diagnostics.report("unknown function '" + symbol.qualified_name() +
                               "'");
    return std::nullopt;
  }
  return detail::instantiate_function(*this, *function, *definition,
                                      state_->diagnostics,
                                      std::move(known_arguments));
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

bool Compiler::conforms(const Module::FunctionDecl& declaration,
                        const Module::InterfaceDecl& interface) const {
  return state_->linked &&
         conforms_to_interface(state_->modules, declaration.symbol(),
                               declaration.interfaces(), interface,
                               Module::SymbolKind::Function);
}

bool Compiler::check_method_result(Module::InterfaceDecl::MethodDecl method,
                                   const Module::ParameterDecl& result) {
  if (method.results().size() == 1U &&
      method.results().front().domain == result.domain) {
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
  const auto declared = method.inputs();
  if (declared.size() != parameters.size()) {
    state_->diagnostics.report("typed binding for interface method '" +
                               method.qualified_name() +
                               "' has the wrong argument count");
    return false;
  }
  for (std::size_t index = 0; index < declared.size(); ++index) {
    if (declared[index].domain != parameters[index].domain) {
      state_->diagnostics.report(
          "typed binding for interface method '" + method.qualified_name() +
          "' disagrees with argument " + std::to_string(index + 1U));
      return false;
    }
  }
  return true;
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

void Compiler::bind_method(Module::FunctionDecl declaration,
                           Module::InterfaceDecl::MethodDecl method,
                           MethodFunction<ir::Instruction> function) {
  bind_interface_method(
      state_->linked, std::move(declaration), std::move(method),
      std::move(function), state_->instruction_methods, state_->diagnostics,
      [this](const auto& subject, const auto& interface) {
        return conforms(subject, interface);
      },
      "function call");
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
Compiler::call(const ir::Instruction& subject,
               Module::InterfaceDecl::MethodDecl method,
               std::span<const ParameterValue> parameters) {
  return evaluate_interface_method(
      state_->linked, subject, std::move(method), parameters,
      state_->instruction_methods, state_->modules, state_->diagnostics,
      [this](const auto& declaration, const auto& interface) {
        return conforms(declaration, interface);
      },
      "function call");
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

void Compiler::bind_verifier(Module::FunctionDecl schema,
                             VerifierFunction<ir::Instruction> verifier) {
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report(
        "cannot bind an Instruction verifier for function '" +
        symbol.qualified_name() + "' outside this compiler");
    return;
  }
  if (!verifier) {
    state_->diagnostics.report("Instruction verifier binding is empty");
    return;
  }
  if (!state_->instruction_verifiers
           .emplace(symbol.stable_name(), std::move(verifier))
           .second) {
    state_->diagnostics.report("function '" + symbol.qualified_name() +
                               "' already has an Instruction verifier");
  }
}

bool Compiler::bind_representation(Module::TypeDecl schema,
                                   std::string_view type) {
  if (!schema.parameters().empty()) {
    state_->diagnostics.report(
        "a parameterized host representation needs a projection returning "
        "its ordered type parameters");
    return false;
  }
  RepresentationProjector projector =
      [](Compiler& compiler, const Module::TypeDecl& declaration, const void*) {
        return compiler.make(declaration);
      };
  return bind_representation(std::move(schema), type, std::move(projector));
}

bool Compiler::bind_representation(Module::TypeDecl schema,
                                   std::string_view type,
                                   RepresentationProjector projector) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot register a host representation before the compiler is linked");
    return false;
  }
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
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
        "a C++ type and a Module type must have a one-to-one host "
        "representation");
    return false;
  }
  state_->host_types.emplace(
      std::string(type),
      State::HostRepresentation{schema, std::move(projector)});
  state_->host_representations.emplace(identity, std::string(type));
  return true;
}

bool Compiler::accepts_host_type(const Module::FunctionDecl& function,
                                 const Module::ParameterDecl& field,
                                 std::string_view type) const {
  if (const auto domain = detail::cpp_value_domain(type)) {
    return parameter_domain(field) == domain;
  }
  const auto declaration =
      field_type_declaration(state_->modules, function, field);
  const auto representation = declaration
                                  ? state_->host_representations.find(
                                        declaration->symbol().stable_name())
                                  : state_->host_representations.end();
  return representation != state_->host_representations.end() &&
         representation->second == type;
}

bool Compiler::project_host_value(detail::ExecutionValue& value) {
  auto* host = std::get_if<detail::HostValue>(&value);
  if (host == nullptr || host->concrete_type) {
    return true;
  }
  const auto representation = state_->host_types.find(host->cpp_type);
  if (representation == state_->host_types.end()) {
    state_->diagnostics.report(
        "a C++ value has no registered Module type representation");
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
      !belongs_to(state_->modules, ParameterValue(*projected))) {
    state_->diagnostics.report(
        "host type projection did not produce an instance of its registered "
        "Module type");
    return false;
  }
  host->concrete_type = std::move(*projected);
  return true;
}

bool Compiler::check_host_values(
    const Module::FunctionDecl& function,
    std::span<const detail::ExecutionValue> arguments,
    std::span<const detail::ExecutionValue> results) {
  const bool has_host_input =
      std::any_of(arguments.begin(), arguments.end(), [](const auto& value) {
        return std::holds_alternative<detail::HostValue>(value);
      });
  const bool has_host_result =
      std::any_of(results.begin(), results.end(), [](const auto& value) {
        return std::holds_alternative<detail::HostValue>(value);
      });
  if (!has_host_input && !has_host_result) {
    return true;
  }

  const auto& contract = detail::FunctionTypeAccess::get(function);
  if (arguments.size() != function.inputs().size()) {
    return false;
  }
  std::vector<Type> value_arguments;
  std::vector<std::optional<ParameterValue>> known_arguments;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (contract.ir_inputs[index]) {
      const auto* host = std::get_if<detail::HostValue>(&arguments[index]);
      if (host == nullptr || !host->concrete_type) {
        state_->diagnostics.report(
            "compiler function IR input has no concrete Joggle type");
        return false;
      }
      value_arguments.push_back(*host->concrete_type);
      continue;
    }
    known_arguments.push_back(detail::parameter_value(arguments[index]));
  }

  std::vector<std::optional<Type>> expected_results;
  expected_results.reserve(detail::ir_results(function).size());
  for (std::size_t index = 0; index < function.results().size(); ++index) {
    if (!contract.ir_results[index]) {
      continue;
    }
    const auto* host = results.empty()
                           ? nullptr
                           : std::get_if<detail::HostValue>(&results[index]);
    expected_results.push_back(host == nullptr ? std::optional<Type>{}
                                               : host->concrete_type);
  }
  return detail::resolve_call_types(*this, function, value_arguments,
                                    known_arguments, expected_results,
                                    state_->diagnostics)
      .has_value();
}

bool Compiler::check_binding_signature(
    const Module::FunctionDecl& schema,
    std::span<const std::string_view> inputs,
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
    state_->diagnostics.report("C++ binding for function '" +
                               schema.symbol().qualified_name() +
                               "' does not match its declared type");
    return false;
  }
  return true;
}

void Compiler::bind_native(Module::FunctionDecl schema, NativeFunction function,
                           HostEvaluation evaluation) {
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("cannot bind compiler function '" +
                               symbol.qualified_name() +
                               "' outside this compiler");
    return;
  }
  if (schema.form() != Module::FunctionDecl::Form::External) {
    state_->diagnostics.report("text-defined compiler function '" +
                               symbol.qualified_name() +
                               "' cannot receive a C++ binding");
    return;
  }
  if (!function) {
    state_->diagnostics.report("compiler-function binding is empty");
    return;
  }
  if (!state_->bindings
           .emplace(symbol.stable_name(),
                    State::BoundFunction{std::move(function), evaluation})
           .second) {
    state_->diagnostics.report("compiler function '" + symbol.qualified_name() +
                               "' already has a binding");
  }
}

void Compiler::bind_prelude_module() {
  const auto found = state_->modules.find(detail::prelude_module_name);
  const auto module = found == state_->modules.end()
                          ? std::optional<Module::TypeDecl>{}
                          : found->second.type("module");
  if (!module) {
    state_->diagnostics.report("Prelude does not declare type 'module'");
    return;
  }
  const std::string cpp_type(detail::host_type_name<Module>());
  const auto projector = [](Compiler& compiler,
                            const Module::TypeDecl& declaration,
                            const void*) { return compiler.make(declaration); };
  state_->host_types.emplace(cpp_type,
                             State::HostRepresentation{*module, projector});
  state_->host_representations.emplace(module->symbol().stable_name(),
                                       cpp_type);
}

void Compiler::bind_prelude_primitives() {
  const auto found = state_->modules.find(detail::prelude_module_name);
  if (found == state_->modules.end()) {
    return;
  }
  for (const Module::FunctionDecl& function : found->second.declarations()) {
    if (!detail::is_prelude_primitive(function)) {
      continue;
    }
    NativeFunction implementation =
        [function](Compiler& compiler,
                   std::span<detail::ExecutionValue> arguments,
                   Diagnostics& diagnostics)
        -> std::optional<detail::ExecutionValues> {
      std::vector<detail::ParameterValue> values;
      values.reserve(arguments.size());
      for (const auto& argument : arguments) {
        auto value = detail::parameter_value(argument);
        if (!value) {
          diagnostics.report("Prelude primitive '" +
                             function.symbol().qualified_name() +
                             "' received an unsupported value");
          return std::nullopt;
        }
        values.push_back(std::move(*value));
      }
      auto result = detail::evaluate_prelude_primitive(
          function, values, diagnostics, compiler.evaluation_limits().steps);
      if (!result || function.results().size() != 1U) {
        return std::nullopt;
      }
      auto encoded =
          detail::execution_value(*result, function.results().front());
      if (!encoded) {
        diagnostics.report("Prelude primitive '" +
                           function.symbol().qualified_name() +
                           "' produced an unsupported value");
        return std::nullopt;
      }
      detail::ExecutionValues results;
      results.push_back(std::move(*encoded));
      return results;
    };
    bind_native(function, std::move(implementation), HostEvaluation::Hermetic);
  }
}

bool Compiler::can_evaluate_binding(const Module::FunctionDecl& function,
                                    bool under_residual_control) const {
  if (function.form() == Module::FunctionDecl::Form::Body) {
    return true;
  }
  const auto binding = state_->bindings.find(function.symbol().stable_name());
  return binding != state_->bindings.end() &&
         (!under_residual_control ||
          binding->second.evaluation == HostEvaluation::Hermetic);
}

std::optional<detail::ParameterValue>
Compiler::evaluate_binding(Module::FunctionDecl function,
                           std::span<const detail::ParameterValue> arguments,
                           bool under_residual_control) {
  if (!detail::ir_inputs(function).empty() ||
      !detail::ir_results(function).empty() ||
      detail::parameter_inputs(function).size() != arguments.size() ||
      detail::parameter_results(function).size() != 1U) {
    state_->diagnostics.report("function '" +
                               function.symbol().qualified_name() +
                               "' cannot be evaluated from Known values");
    return std::nullopt;
  }
  std::optional<std::string> cache_key;
  if (function.form() == Module::FunctionDecl::Form::External) {
    const auto binding = state_->bindings.find(function.symbol().stable_name());
    if (binding != state_->bindings.end() &&
        binding->second.evaluation == HostEvaluation::Hermetic) {
      cache_key = function.symbol().stable_name();
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
  std::vector<detail::ExecutionValue> values;
  values.reserve(arguments.size());
  const auto parameters = detail::parameter_inputs(function);
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    auto converted =
        detail::execution_value(arguments[index], parameters[index]);
    if (!converted) {
      state_->diagnostics.report(
          "compiler execution cannot represent argument '" +
          parameters[index].name + "'");
      return std::nullopt;
    }
    values.push_back(std::move(*converted));
  }
  auto produced = execute(function, std::move(values), under_residual_control);
  if (!produced || produced->size() != 1U) {
    return std::nullopt;
  }
  auto result = detail::parameter_value(produced->front());
  if (!result || !detail::matches_parameter(
                     detail::parameter_results(function).front(), *result)) {
    state_->diagnostics.report("compiler execution of function '" +
                               function.symbol().qualified_name() +
                               "' produced a value with the wrong type");
    return std::nullopt;
  }
  if (cache_key) {
    state_->hermetic_evaluations.emplace(std::move(*cache_key), *result);
  }
  return result;
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
Compiler::lookup_method(Module::FunctionDecl declaration,
                        std::string_view reference) {
  const auto symbol = declaration.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (!state_->linked || owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
    state_->diagnostics.report("function '" + symbol.qualified_name() +
                               "' is not in this compilation");
    return std::nullopt;
  }
  return lookup_method(owner->second, declaration.name(),
                       declaration.interfaces(), reference,
                       Module::SymbolKind::Function);
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(const Attribute& subject, std::string_view reference) {
  return lookup_method(subject.schema(), reference);
}

std::optional<Module::InterfaceDecl::MethodDecl>
Compiler::lookup_method(const ir::Instruction& subject,
                        std::string_view reference) {
  return lookup_method(subject.callee(), reference);
}

bool Compiler::verify(const ir::Function& function) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot verify a function before the compiler is linked");
    return false;
  }
  if (!detail::FunctionAccess::verify_structure(function,
                                                state_->diagnostics)) {
    return false;
  }
  bool valid = detail::FunctionAccess::verify_contracts(function, *this,
                                                        state_->diagnostics);
  for (const ir::Instruction& instruction : function.instructions()) {
    const Module::FunctionDecl schema = instruction.callee();
    const Module::Symbol symbol = schema.symbol();
    const auto location = detail::FunctionAccess::location(instruction);
    const auto verifier =
        state_->instruction_verifiers.find(symbol.stable_name());
    if (verifier != state_->instruction_verifiers.end()) {
      Diagnostics reported;
      const bool accepted = verifier->second(instruction, reported);
      if (!accepted && reported.ok()) {
        reported.report("Instruction verifier rejected call to '" +
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
  }
  return valid;
}

bool Compiler::check_run_signature(const Module::FunctionDecl& schema,
                                   std::span<const std::string_view> inputs,
                                   std::span<const std::string_view> results) {
  if (matches_run_signature(schema, inputs, results)) {
    return true;
  }
  state_->diagnostics.report("invocation of function '" +
                             schema.symbol().qualified_name() +
                             "' does not match its declared type");
  return false;
}

bool Compiler::matches_run_signature(
    const Module::FunctionDecl& schema,
    std::span<const std::string_view> inputs,
    std::span<const std::string_view> results) const {
  if (!state_->linked) {
    return false;
  }
  const Module::Symbol symbol = schema.symbol();
  const auto owner = state_->modules.find(symbol.module_name());
  if (owner == state_->modules.end() ||
      owner->second.version() != symbol.module_version() ||
      owner->second.digest() != symbol.module_digest()) {
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

std::optional<Module::FunctionDecl>
Compiler::find_function(std::string_view name) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot run a compiler function before the compiler is linked");
    return std::nullopt;
  }
  const auto member = qualified_member(name);
  if (!member) {
    state_->diagnostics.report("compiler-function name '" + std::string(name) +
                               "' must be qualified as module.member");
    return std::nullopt;
  }
  const auto owner = state_->modules.find(member->first);
  if (owner == state_->modules.end()) {
    state_->diagnostics.report("compiler function '" + std::string(name) +
                               "' names an unlinked module");
    return std::nullopt;
  }
  const auto declaration = owner->second.declaration(member->second);
  if (!declaration) {
    state_->diagnostics.report("unknown compiler function '" +
                               std::string(name) + "'");
  }
  return declaration;
}

std::optional<detail::ExecutionValues>
Compiler::execute(Module::FunctionDecl declaration,
                  std::vector<detail::ExecutionValue> arguments,
                  bool under_residual_control) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot run a compiler function before the compiler is linked");
    return std::nullopt;
  }
  if (arguments.size() != declaration.inputs().size()) {
    state_->diagnostics.report("compiler function '" +
                               declaration.symbol().qualified_name() +
                               "' received the wrong argument count");
    return std::nullopt;
  }
  for (auto& argument : arguments) {
    if (!project_host_value(argument)) {
      return std::nullopt;
    }
  }
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    if (!accepts_host_type(declaration, declaration.inputs()[index],
                           detail::execution_value_type(arguments[index]))) {
      state_->diagnostics.report("compiler function '" +
                                 declaration.symbol().qualified_name() +
                                 "' received an argument with the wrong type");
      return std::nullopt;
    }
  }
  if (!check_host_values(declaration, arguments)) {
    return std::nullopt;
  }

  std::vector<std::pair<std::shared_ptr<ir::Function>,
                        std::shared_ptr<const ir::Function::Snapshot>>>
      checkpoints;
  for (detail::ExecutionValue& argument : arguments) {
    if (detail::execution_value_type(argument) == typeid(ir::Function).name()) {
      auto function = std::get<std::shared_ptr<ir::Function>>(argument);
      if (!verify(*function)) {
        return std::nullopt;
      }
      checkpoints.emplace_back(function, function->snapshot());
    }
  }
  const std::size_t before = state_->diagnostics.size();
  std::size_t steps = 0;
  std::size_t depth = 0;
  const auto execute = [&](const auto& self,
                           const Module::FunctionDecl& current,
                           std::vector<detail::ExecutionValue> values)
      -> std::optional<detail::ExecutionValues> {
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
      state_->diagnostics.report("compiler function '" +
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
                             detail::execution_value_type(values[index]))) {
        state_->diagnostics.report(
            "compiler function '" + current.symbol().qualified_name() +
            "' received an argument with the wrong type");
        return std::nullopt;
      }
      if (detail::execution_value_type(values[index]) ==
          typeid(ir::Function).name()) {
        const auto function =
            std::get<std::shared_ptr<ir::Function>>(values[index]);
        if (!function->accepts(current.symbol())) {
          state_->diagnostics.report(
              "compiler function '" + current.symbol().qualified_name() +
              "' is outside the function's module closure");
          return std::nullopt;
        }
      }
    }
    if (!check_host_values(current, values)) {
      return std::nullopt;
    }
    switch (current.form()) {
    case Module::FunctionDecl::Form::External: {
      const auto binding =
          state_->bindings.find(current.symbol().stable_name());
      if (binding == state_->bindings.end()) {
        state_->diagnostics.report("compiler function '" +
                                   current.symbol().qualified_name() +
                                   "' has no C++ binding");
        return std::nullopt;
      }
      if (under_residual_control &&
          binding->second.evaluation != HostEvaluation::Hermetic) {
        state_->diagnostics.report(
            "host implementation of function '" +
            current.symbol().qualified_name() +
            "' is guarded and cannot execute under Residual control");
        return std::nullopt;
      }
      std::optional<detail::ExecutionValues> execution;
      try {
        execution =
            binding->second.callable(*this, values, state_->diagnostics);
      } catch (const std::bad_variant_access&) {
        state_->diagnostics.report(
            "C++ binding for compiler function '" +
            current.symbol().qualified_name() +
            "' disagrees with its registered host representation");
        return std::nullopt;
      } catch (const std::exception& exception) {
        state_->diagnostics.report("C++ binding for compiler function '" +
                                   current.symbol().qualified_name() +
                                   "' threw: " + exception.what());
        return std::nullopt;
      } catch (...) {
        state_->diagnostics.report("C++ binding for compiler function '" +
                                   current.symbol().qualified_name() +
                                   "' threw an unknown exception");
        return std::nullopt;
      }
      if (!execution) {
        if (state_->diagnostics.size() == before) {
          state_->diagnostics.report("compiler function '" +
                                     current.symbol().qualified_name() +
                                     "' failed");
        }
        return std::nullopt;
      }
      if (execution->size() != current.results().size()) {
        state_->diagnostics.report("compiler function '" +
                                   current.symbol().qualified_name() +
                                   "' produced the wrong number of values");
        return std::nullopt;
      }
      for (std::size_t index = 0; index < execution->size(); ++index) {
        if (!project_host_value((*execution)[index]) ||
            !accepts_host_type(
                current, current.results()[index],
                detail::execution_value_type((*execution)[index]))) {
          state_->diagnostics.report("compiler function '" +
                                     current.symbol().qualified_name() +
                                     "' produced a value with the wrong type");
          return std::nullopt;
        }
      }
      if (!check_host_values(current, values, *execution)) {
        return std::nullopt;
      }
      return execution;
    }
    case Module::FunctionDecl::Form::Body: {
      const auto owner = state_->modules.find(current.symbol().module_name());
      const auto body =
          owner == state_->modules.end()
              ? std::shared_ptr<const detail::FunctionBody>{}
              : detail::ModuleAccess::body(owner->second, current);
      if (!body) {
        state_->diagnostics.report("compiler function '" +
                                   current.symbol().qualified_name() +
                                   "' has no executable body");
        return std::nullopt;
      }
      const detail::ExecuteFunction invoke =
          [&](Module::FunctionDecl function,
              std::vector<detail::ExecutionValue> arguments) {
            return self(self, function, std::move(arguments));
          };
      auto evaluated = detail::execute_body(
          *this, current, *body, values, state_->evaluation_limits, steps,
          under_residual_control, state_->diagnostics, invoke);
      if (!evaluated) {
        return std::nullopt;
      }
      if (evaluated->size() != current.results().size()) {
        state_->diagnostics.report("compiler function '" +
                                   current.symbol().qualified_name() +
                                   "' returned the wrong number of values");
        return std::nullopt;
      }
      for (std::size_t index = 0; index < evaluated->size(); ++index) {
        if (!project_host_value((*evaluated)[index]) ||
            !accepts_host_type(
                current, current.results()[index],
                detail::execution_value_type((*evaluated)[index]))) {
          state_->diagnostics.report("compiler function '" +
                                     current.symbol().qualified_name() +
                                     "' returned a value with the wrong type");
          return std::nullopt;
        }
      }
      if (!check_host_values(current, values, *evaluated)) {
        return std::nullopt;
      }
      return evaluated;
    }
    }
    return std::nullopt;
  };

  std::optional<detail::ExecutionValues> result;
  try {
    result = execute(execute, declaration, std::move(arguments));
  } catch (...) {
    for (auto& [function, snapshot] : checkpoints) {
      function->restore(std::move(snapshot));
    }
    throw;
  }
  bool valid = result.has_value() && state_->diagnostics.size() == before;
  if (valid) {
    for (const auto& value : *result) {
      if (detail::execution_value_type(value) == typeid(ir::Function).name()) {
        valid =
            verify(*std::get<std::shared_ptr<ir::Function>>(value)) && valid;
      }
    }
  }
  if (!valid) {
    for (auto& [function, snapshot] : checkpoints) {
      function->restore(std::move(snapshot));
    }
    return std::nullopt;
  }
  return result;
}

bool Compiler::run(ir::Function& function, Module::FunctionDecl transform) {
  const Module::Symbol symbol = transform.symbol();
  const bool function_transform =
      transform.inputs().size() == 1U &&
      parameter_domain(transform.inputs().front()) ==
          detail::Domain{detail::ValueKind::Function, false} &&
      transform.results().size() == 1U &&
      parameter_domain(transform.results().front()) ==
          detail::Domain{detail::ValueKind::Function, false};
  if (!function_transform) {
    state_->diagnostics.report(
        "compiler function '" + symbol.qualified_name() +
        "' is not a function -> function transformation");
    return false;
  }
  std::vector<detail::ExecutionValue> arguments;
  arguments.push_back(
      {std::shared_ptr<ir::Function>(&function, [](ir::Function*) {})});
  return execute(std::move(transform), std::move(arguments)).has_value();
}

bool Compiler::run(ir::Function& function, std::string_view transform) {
  const auto declaration = find_function(transform);
  return declaration && run(function, *declaration);
}

const Diagnostics& Compiler::diagnostics() const { return state_->diagnostics; }

}  // namespace joggle
