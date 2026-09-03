#include "joggle/compiler.h"

#include "prelude.h"
#include "domain.h"
#include "expression_syntax.h"
#include "ir_internal.h"
#include "function_body.h"
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
#include <functional>
#include <locale>
#include <map>
#include <limits>
#include <sstream>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
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
    return value.kind() == ParameterValue::Kind::I64 &&
           *value.as_i64() >= 0;
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
  return std::all_of(
      value.elements().begin(), value.elements().end(),
      [&](const ParameterValue& element) {
        return accepts_known_value(*parameters.front().as_type(), element);
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

std::string_view execution_value_type(const detail::ExecutionValue& value) {
  if (std::holds_alternative<std::int64_t>(value)) {
    return typeid(std::int64_t).name();
  }
  if (std::holds_alternative<double>(value)) {
    return typeid(double).name();
  }
  if (std::holds_alternative<bool>(value)) {
    return typeid(bool).name();
  }
  if (std::holds_alternative<std::string>(value)) {
    return typeid(std::string).name();
  }
  if (std::holds_alternative<Type>(value)) {
    return typeid(Type).name();
  }
  if (std::holds_alternative<Attribute>(value)) {
    return typeid(Attribute).name();
  }
  if (std::holds_alternative<Bytes>(value)) {
    return typeid(Bytes).name();
  }
  if (std::holds_alternative<std::shared_ptr<Function>>(value)) {
    return typeid(Function).name();
  }
  if (std::holds_alternative<detail::IntegerList>(value)) {
    return typeid(detail::IntegerList).name();
  }
  if (std::holds_alternative<detail::RealList>(value)) {
    return typeid(detail::RealList).name();
  }
  if (std::holds_alternative<detail::BooleanList>(value)) {
    return typeid(detail::BooleanList).name();
  }
  if (std::holds_alternative<detail::StringList>(value)) {
    return typeid(detail::StringList).name();
  }
  if (std::holds_alternative<detail::TypeList>(value)) {
    return typeid(detail::TypeList).name();
  }
  if (std::holds_alternative<detail::AttributeList>(value)) {
    return typeid(detail::AttributeList).name();
  }
  if (std::holds_alternative<detail::HostValue>(value)) {
    return std::get<detail::HostValue>(value).cpp_type;
  }
  return typeid(void).name();
}

std::optional<detail::Domain> cpp_value_domain(std::string_view type) {
  if (type == typeid(std::int64_t).name()) {
    return detail::Domain{detail::ValueKind::Integer, false};
  }
  if (type == typeid(double).name()) {
    return detail::Domain{detail::ValueKind::Real, false};
  }
  if (type == typeid(bool).name()) {
    return detail::Domain{detail::ValueKind::Boolean, false};
  }
  if (type == typeid(std::string).name()) {
    return detail::Domain{detail::ValueKind::String, false};
  }
  if (type == typeid(Type).name()) {
    return detail::Domain{detail::ValueKind::Type, false};
  }
  if (type == typeid(Attribute).name()) {
    return detail::Domain{detail::ValueKind::Attribute, false};
  }
  if (type == typeid(Bytes).name()) {
    return detail::Domain{detail::ValueKind::Bytes, false};
  }
  if (type == typeid(Function).name()) {
    return detail::Domain{detail::ValueKind::Function, false};
  }
  if (type == typeid(detail::IntegerList).name()) {
    return detail::Domain{detail::ValueKind::Integer, true};
  }
  if (type == typeid(detail::RealList).name()) {
    return detail::Domain{detail::ValueKind::Real, true};
  }
  if (type == typeid(detail::BooleanList).name()) {
    return detail::Domain{detail::ValueKind::Boolean, true};
  }
  if (type == typeid(detail::StringList).name()) {
    return detail::Domain{detail::ValueKind::String, true};
  }
  if (type == typeid(detail::TypeList).name()) {
    return detail::Domain{detail::ValueKind::Type, true};
  }
  if (type == typeid(detail::AttributeList).name()) {
    return detail::Domain{detail::ValueKind::Attribute, true};
  }
  return std::nullopt;
}

std::optional<detail::Domain>
pass_field_domain(const Module::ParameterDecl& field) {
  return detail::kernel_domain(field.domain);
}

std::string_view resolve_prefix(const Module& module, std::string_view prefix);

template <typename Modules>
std::optional<Module::TypeDecl> field_type_declaration(
    const Modules& modules, const Module::FunctionDecl& function,
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

template <typename T>
std::optional<detail::ExecutionValue>
list_execution_value(const ParameterValue& value) {
  auto decoded = detail::decode_parameter<std::vector<T>>(value);
  return decoded ? std::optional<detail::ExecutionValue>{std::move(*decoded)}
                 : std::nullopt;
}

std::optional<detail::ExecutionValue>
execution_value(const ParameterValue& value,
                const Module::ParameterDecl& parameter) {
  const auto domain = detail::kernel_domain(parameter.domain);
  if (domain && domain->list) {
    switch (domain->element) {
    case detail::ValueKind::Integer:
      return list_execution_value<std::int64_t>(value);
    case detail::ValueKind::Real:
      return list_execution_value<double>(value);
    case detail::ValueKind::Boolean:
      return list_execution_value<bool>(value);
    case detail::ValueKind::String:
      return list_execution_value<std::string>(value);
    case detail::ValueKind::Type:
      return list_execution_value<Type>(value);
    case detail::ValueKind::Attribute:
      return list_execution_value<Attribute>(value);
    case detail::ValueKind::Function:
    case detail::ValueKind::Bytes:
      return std::nullopt;
    }
  }
  switch (value.kind()) {
  case ParameterValue::Kind::I64:
    return detail::ExecutionValue{*value.as_i64()};
  case ParameterValue::Kind::F64:
    return detail::ExecutionValue{*value.as_f64()};
  case ParameterValue::Kind::Boolean:
    return detail::ExecutionValue{*value.as_bool()};
  case ParameterValue::Kind::String:
    return detail::ExecutionValue{*value.as_string()};
  case ParameterValue::Kind::Type:
    return detail::ExecutionValue{*value.as_type()};
  case ParameterValue::Kind::Attribute:
    return detail::ExecutionValue{*value.as_attribute()};
  case ParameterValue::Kind::List:
    return std::nullopt;
  }
  return std::nullopt;
}

template <typename T>
ParameterValue list_parameter_value(const std::vector<T>& values) {
  std::vector<ParameterValue> elements;
  elements.reserve(values.size());
  for (const T& value : values) {
    elements.emplace_back(value);
  }
  return ParameterValue::list(std::move(elements));
}

ParameterValue list_parameter_value(const std::vector<bool>& values) {
  std::vector<ParameterValue> elements;
  elements.reserve(values.size());
  for (const bool value : values) {
    elements.emplace_back(value);
  }
  return ParameterValue::list(std::move(elements));
}

std::optional<ParameterValue>
parameter_value(const detail::ExecutionValue& value) {
  if (const auto* stored = std::get_if<std::int64_t>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<double>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<bool>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<std::string>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<Type>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<Attribute>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<detail::IntegerList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<detail::RealList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<detail::BooleanList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<detail::StringList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<detail::TypeList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<detail::AttributeList>(&value)) {
    return list_parameter_value(*stored);
  }
  return std::nullopt;
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
  if constexpr (std::is_same_v<Subject, Instruction>) {
    if (!subject.valid()) {
      diagnostics.report("invalid operation used as an interface subject");
      return std::nullopt;
    }
  }
  const auto declaration = [&] {
    if constexpr (std::is_same_v<Subject, Instruction>) {
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
  if constexpr (std::is_same_v<Subject, Instruction>) {
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

template <typename Modules>
std::optional<Module::FunctionDecl> resolve_function(const Modules& modules,
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
  return module == modules.end() ? std::nullopt : module->second.function(local);
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
  std::map<std::string, VerifierFunction<Instruction>, std::less<>>
      operation_verifiers;
  std::map<std::string, MethodFunction<Attribute>, std::less<>>
      attribute_methods;
  std::map<std::string, MethodFunction<Instruction>, std::less<>>
      operation_methods;
  std::map<std::string, BoundFunction, std::less<>> bindings;
  std::map<std::string, HostRepresentation, std::less<>> host_types;
  std::map<std::string, std::string, std::less<>> host_representations;
  std::set<std::string, std::less<>> constructing_types;
  EvaluationLimits evaluation_limits;
  bool linked = false;
};

Compiler::Compiler() : Compiler(EvaluationLimits{}) {}

Compiler::Compiler(EvaluationLimits limits) : state_(std::make_unique<State>()) {
  state_->evaluation_limits = limits;
  auto prelude = parse_module(detail::prelude_module_source(),
                              state_->diagnostics, "<prelude>");
  if (prelude) {
    add_module(std::move(*prelude), false, std::nullopt);
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

    const auto validate_compile_time_expression =
        [&](const auto& self, const Module::Expression& expression,
            const Module::Expression& expected,
            std::span<const Module::ParameterDecl> variables,
            std::string_view declaration,
            std::optional<SourceRange> location) -> void {
      using Kind = Module::Expression::Kind;
      const auto report = [&](std::string message) {
        state_->diagnostics.report(std::move(message), location);
      };
      const auto domain = detail::kernel_domain(expected);
      if (!domain) {
        report("unknown result domain in compile-time definition '" +
               std::string(declaration) + "'");
        return;
      }
      if (expression.kind == Kind::FunctionType) {
        const auto signature = detail::callable_type(expression);
        if (domain->list || domain->element != detail::ValueKind::Type ||
            !signature) {
          report("malformed function type in compile-time definition '" +
                 std::string(declaration) + "'");
          return;
        }
        const auto type_domain =
            detail::domain_expression(detail::ValueKind::Type);
        for (const auto side : {signature->inputs, signature->results}) {
          for (const auto& element : side) {
            self(self, element, type_domain, variables, declaration,
                 location);
          }
        }
        return;
      }
      if (expression.kind == Kind::Variable) {
        const auto variable = std::find_if(
            variables.begin(), variables.end(), [&](const auto& candidate) {
              return candidate.name == expression.text;
            });
        if (variable == variables.end() || variable->domain != expected) {
          report("variable '" + expression.text +
                 "' has the wrong domain in compile-time definition '" +
                 std::string(declaration) + "'");
        }
        return;
      }
      if (expression.kind == Kind::Evaluate) {
        if (expression.arguments.size() != 1U) {
          report("malformed compile-time evaluation in definition '" +
                 std::string(declaration) + "'");
          return;
        }
        self(self, expression.arguments.front(), expected, variables,
             declaration, location);
        return;
      }
      if (expression.kind == Kind::If) {
        if (expression.arguments.size() != 3U) {
          report("malformed if expression in definition '" +
                 std::string(declaration) + "'");
          return;
        }
        self(self, expression.arguments[0],
             detail::domain_expression(detail::ValueKind::Boolean), variables,
             declaration, location);
        self(self, expression.arguments[1], expected, variables, declaration,
             location);
        self(self, expression.arguments[2], expected, variables, declaration,
             location);
        return;
      }
      if (domain->list && expression.kind != Kind::Call) {
        if (expression.kind != Kind::List) {
          report("expected a list expression in compile-time definition '" +
                 std::string(declaration) + "'");
          return;
        }
        for (const auto& element : expression.arguments) {
          self(self, element, detail::domain_expression(domain->element),
               variables, declaration, location);
        }
        return;
      }
      if (expression.kind == Kind::List) {
        report("unexpected list expression in compile-time definition '" +
               std::string(declaration) + "'");
        return;
      }
      const bool operation = expression.kind == Kind::Prefix ||
                             expression.kind == Kind::Infix ||
                             expression.kind == Kind::Postfix;
      if (operation) {
        if (domain->element != detail::ValueKind::Integer &&
            domain->element != detail::ValueKind::Real) {
          report("arithmetic has the wrong domain in compile-time "
                 "definition '" +
                 std::string(declaration) + "'");
          return;
        }
        const std::size_t arity = expression.kind == Kind::Infix ? 2U : 1U;
        if (expression.arguments.size() != arity) {
          report("malformed operator expression in compile-time definition '" +
                 std::string(declaration) + "'");
          return;
        }
        for (const auto& argument : expression.arguments) {
          self(self, argument, expected, variables, declaration, location);
        }
        return;
      }
      if (expression.kind == Kind::Call) {
        const bool kernel_call = expression.text == "ceildiv" ||
                                 expression.text == "min" ||
                                 expression.text == "max";
        if (kernel_call) {
          if (domain->element != detail::ValueKind::Integer ||
              expression.arguments.size() != 2U) {
            report("ill-typed kernel call '" + expression.text +
                   "' in compile-time definition '" +
                   std::string(declaration) + "'");
            return;
          }
          for (const auto& argument : expression.arguments) {
            self(self, argument,
                 detail::domain_expression(detail::ValueKind::Integer),
                 variables, declaration, location);
          }
          return;
        }
        const std::size_t dot = expression.text.find('.');
        const std::string owner =
            dot == std::string::npos
                ? name
                : std::string(resolve_prefix(
                      module,
                      std::string_view(expression.text).substr(0, dot)));
        const std::string local =
            dot == std::string::npos ? expression.text
                                     : expression.text.substr(dot + 1U);
        const auto source = state_->modules.find(owner);
        const auto function =
            source == state_->modules.end()
                ? std::optional<Module::FunctionDecl>{}
                : source->second.function(local);
        if (!function || function->results().front().domain != expected ||
            function->inputs().size() != expression.arguments.size()) {
          report("unknown or ill-typed pure call '" + expression.text +
                 "' in compile-time definition '" +
                 std::string(declaration) + "'");
          return;
        }
        for (std::size_t index = 0; index < expression.arguments.size();
             ++index) {
          self(self, expression.arguments[index],
               function->inputs()[index].domain, variables, declaration,
               location);
        }
        return;
      }
      if (expression.kind == Kind::Number ||
          expression.kind == Kind::Boolean ||
          expression.kind == Kind::String) {
        const bool matches =
            (expression.kind == Kind::Number &&
             (domain->element == detail::ValueKind::Integer ||
              domain->element == detail::ValueKind::Real)) ||
            (expression.kind == Kind::Boolean &&
             domain->element == detail::ValueKind::Boolean) ||
            (expression.kind == Kind::String &&
             domain->element == detail::ValueKind::String);
        if (!matches) {
          report("literal has the wrong domain in compile-time definition '" +
                 std::string(declaration) + "'");
        }
        return;
      }
      if (domain->element != detail::ValueKind::Type &&
          domain->element != detail::ValueKind::Attribute) {
        report("reference has the wrong domain in compile-time definition '" +
               std::string(declaration) + "'");
        return;
      }
      const std::size_t dot = expression.text.find('.');
      const std::string owner =
          dot == std::string::npos
              ? name
              : std::string(resolve_prefix(
                    module,
                    std::string_view(expression.text).substr(0, dot)));
      const std::string local =
          dot == std::string::npos ? expression.text
                                   : expression.text.substr(dot + 1U);
      const auto source = state_->modules.find(owner);
      if (source == state_->modules.end()) {
        report("reference uses missing module '" + owner +
               "' in compile-time definition '" +
               std::string(declaration) + "'");
        return;
      }
      std::span<const Module::ParameterDecl> parameters;
      if (domain->element == detail::ValueKind::Type) {
        const auto target = source->second.type(local);
        if (!target) {
          report("unknown type '" + expression.text +
                 "' in compile-time definition '" +
                 std::string(declaration) + "'");
          return;
        }
        parameters = target->parameters();
      } else {
        const auto target = source->second.attribute(local);
        if (!target) {
          report("unknown attribute '" + expression.text +
                 "' in compile-time definition '" +
                 std::string(declaration) + "'");
          return;
        }
        parameters = target->parameters();
      }
      if (expression.arguments.size() > parameters.size()) {
        report("too many arguments for '" + expression.text +
               "' in compile-time definition '" +
               std::string(declaration) + "'");
        return;
      }
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        self(self, expression.arguments[index], parameters[index].domain,
             variables, declaration, location);
      }
      for (std::size_t index = expression.arguments.size();
           index < parameters.size(); ++index) {
        if (!parameters[index].default_value) {
          report("missing argument '" + parameters[index].name + "' for '" +
                 expression.text + "' in compile-time definition '" +
                 std::string(declaration) + "'");
        }
      }
    };

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
        const auto interface =
            source == state_->modules.end()
                ? std::optional<Module::InterfaceDecl>{}
                : source->second.interface(local);
        if (interface) {
          required_fields.insert(required_fields.end(),
                                 interface->fields().begin(),
                                 interface->fields().end());
        }
      }
      for (const auto& field : required_fields) {
        const auto parameter = std::find_if(
            type.parameters().begin(), type.parameters().end(),
            [&](const auto& candidate) {
              return candidate.name == field.name &&
                     candidate.domain == field.domain;
            });
        const auto supplied = std::find_if(
            type.derived_parameters().begin(), type.derived_parameters().end(),
            [&](const auto& candidate) { return candidate.name == field.name; });
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
              dot == std::string::npos ? reference
                                       : reference.substr(dot + 1U);
          const auto source = state_->modules.find(interface_owner);
          const auto interface =
              source == state_->modules.end()
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
        std::vector<Module::ParameterDecl> variables(type.parameters().begin(),
                                                     type.parameters().end());
        validate_compile_time_expression(
            validate_compile_time_expression, derived.value,
            fields.front().domain, variables,
            std::string(type.name()) + "." + derived.name, location);
      }
    }

    for (const Module::FunctionDecl& function : module.functions()) {
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
      validate_compile_time_expression(
          validate_compile_time_expression,
          *detail::ModuleAccess::expression(function),
          results.front().domain, inputs,
          function.name(), location);
    }
    for (const Module::FunctionDecl& declaration : module.functions()) {
      if (detail::ir_inputs(declaration).empty() &&
          detail::ir_results(declaration).empty()) {
        continue;
      }
      const auto operation_source = detail::ModuleAccess::declaration_source(
          module, Module::SymbolKind::Function, declaration.name());
      const auto report_operation = [&](std::string message) {
        state_->diagnostics.report(std::move(message), operation_source);
      };
      validate_interfaces(declaration.interfaces(),
                          Module::SymbolKind::Function, declaration.name(),
                          operation_source);
      const auto& contract = detail::FunctionTypeAccess::get(declaration);
      for (const auto& generic : contract.generics) {
        if (!generic.constraint) {
          continue;
        }
        const std::size_t dot = generic.constraint->find('.');
        const std::string owner =
            dot == std::string::npos
                ? name
                : std::string(resolve_prefix(
                      module,
                      std::string_view(*generic.constraint).substr(0, dot)));
        const std::string local =
            dot == std::string::npos
                ? *generic.constraint
                : generic.constraint->substr(dot + 1U);
        const auto source_module = state_->modules.find(owner);
        const auto interface =
            source_module == state_->modules.end()
                ? std::optional<Module::InterfaceDecl>{}
                : source_module->second.interface(local);
        if (!interface) {
          report_operation("generic '" + generic.name + "' in operation '" +
                           name + "." + std::string(declaration.name()) +
                           "' references unknown interface '" +
                           *generic.constraint + "'");
        } else if (interface->subject() != Module::SymbolKind::Type) {
          report_operation("generic '" + generic.name + "' in operation '" +
                           name + "." + std::string(declaration.name()) +
                           "' is constrained by a non-type interface '" +
                           *generic.constraint + "'");
        }
      }
      const auto validate_expression =
          [&](const auto& self, const detail::TypeExpression& expression,
              const Module::Expression& expected) -> void {
        using Kind = detail::TypeExpression::Kind;
        const auto domain = detail::kernel_domain(expected);
        if (!domain) {
          report_operation("unknown parameter domain in operation '" + name +
                           "." + std::string(declaration.name()) + "'");
          return;
        }
        if (expression.kind == Kind::FunctionType) {
          const auto signature = detail::callable_type(expression);
          if (domain->list || domain->element != detail::ValueKind::Type ||
              !signature) {
            report_operation("operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' contains a malformed function type");
            return;
          }
          const auto type_domain =
              detail::domain_expression(detail::ValueKind::Type);
          for (const auto side : {signature->inputs, signature->results}) {
            for (const auto& element : side) {
              self(self, element, type_domain);
            }
          }
          return;
        }
        if (expression.kind == Kind::Variable) {
          const auto variable =
              std::find_if(contract.generics.begin(), contract.generics.end(),
                           [&](const auto& generic) {
                             return generic.name == expression.text;
                           });
          if (variable == contract.generics.end() ||
              variable->domain != expected) {
            report_operation("type variable '" + expression.text +
                             "' in operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' has the wrong kind");
          }
          return;
        }
        if (expression.kind == Kind::Evaluate) {
          if (expression.arguments.size() != 1U) {
            report_operation("operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' contains malformed compile-time evaluation");
            return;
          }
          self(self, expression.arguments.front(), expected);
          return;
        }
        if (expression.kind == Kind::If) {
          if (expression.arguments.size() != 3U) {
            report_operation("operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' contains malformed if expression");
            return;
          }
          self(self, expression.arguments[0],
               detail::domain_expression(detail::ValueKind::Boolean));
          self(self, expression.arguments[1], expected);
          self(self, expression.arguments[2], expected);
          return;
        }
        if (domain->list && expression.kind != Kind::Call &&
            expression.kind != Kind::Reference) {
          if (expression.kind != Kind::List) {
            report_operation("operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' expects a list-valued type expression");
            return;
          }
          for (const auto& element : expression.arguments) {
            self(self, element, detail::domain_expression(domain->element));
          }
          return;
        }
        if (expression.kind == Kind::List) {
          report_operation("operation '" + name + "." +
                           std::string(declaration.name()) +
                           "' contains an unexpected list type expression");
          return;
        }
        const bool operation = expression.kind == Kind::Prefix ||
                               expression.kind == Kind::Infix ||
                               expression.kind == Kind::Postfix;
        if (operation) {
          if (domain->element != detail::ValueKind::Integer &&
              domain->element != detail::ValueKind::Real) {
            report_operation("arithmetic expression in operation '" + name +
                             "." + std::string(declaration.name()) +
                             "' has the wrong kind");
            return;
          }
          const std::size_t arity = expression.kind == Kind::Infix ? 2U : 1U;
          if (expression.arguments.size() != arity) {
            report_operation("operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' contains malformed operator expression");
            return;
          }
          for (const auto& argument : expression.arguments) {
            self(self, argument, expected);
          }
          return;
        }
        if (expression.kind == Kind::Reference) {
          const std::size_t field_dot = expression.text.find('.');
          if (field_dot != std::string::npos) {
            const std::string_view receiver(expression.text.data(), field_dot);
            const auto generic = std::find_if(
                contract.generics.begin(), contract.generics.end(),
                [&](const auto& candidate) { return candidate.name == receiver; });
            if (generic != contract.generics.end()) {
              if (!generic->constraint) {
                report_operation("generic '" + std::string(receiver) +
                                 "' has no interface exposing derived parameter '" +
                                 expression.text.substr(field_dot + 1U) +
                                 "' in operation '" + name + "." +
                                 std::string(declaration.name()) + "'");
                return;
              }
              const std::size_t constraint_dot = generic->constraint->find('.');
              const std::string interface_owner =
                  constraint_dot == std::string::npos
                      ? name
                      : std::string(resolve_prefix(
                            module, std::string_view(*generic->constraint)
                                        .substr(0, constraint_dot)));
              const std::string interface_name =
                  constraint_dot == std::string::npos
                      ? *generic->constraint
                      : generic->constraint->substr(constraint_dot + 1U);
              const auto source = state_->modules.find(interface_owner);
              const auto interface =
                  source == state_->modules.end()
                      ? std::optional<Module::InterfaceDecl>{}
                      : source->second.interface(interface_name);
              const std::string_view field_name =
                  std::string_view(expression.text).substr(field_dot + 1U);
              const auto field =
                  interface
                      ? std::find_if(interface->fields().begin(),
                                     interface->fields().end(),
                                     [&](const auto& candidate) {
                                       return candidate.name == field_name;
                                     })
                      : std::span<const Module::ParameterDecl>::iterator{};
              if (!interface || field == interface->fields().end() ||
                  field->domain != expected) {
                report_operation("unknown or ill-typed derived parameter '" +
                                 expression.text + "' in operation '" + name +
                                 "." + std::string(declaration.name()) + "'");
              }
              return;
            }
          }
        }
        if (expression.kind == Kind::Call) {
          const bool integer_call = expression.text == "ceildiv" ||
                                    expression.text == "min" ||
                                    expression.text == "max";
          if (integer_call) {
            if (domain->element != detail::ValueKind::Integer ||
                expression.arguments.size() != 2U) {
              report_operation("ill-typed kernel call '" + expression.text +
                               "' in operation '" + name + "." +
                               std::string(declaration.name()) + "'");
              return;
            }
            for (const auto& argument : expression.arguments) {
              self(self, argument,
                   detail::domain_expression(detail::ValueKind::Integer));
            }
            return;
          }

          const std::size_t dot = expression.text.find('.');
          const std::string owner =
              dot == std::string::npos
                  ? name
                  : std::string(resolve_prefix(
                        module,
                        std::string_view(expression.text).substr(0, dot)));
          const std::string local =
              dot == std::string::npos
                  ? expression.text
                  : expression.text.substr(dot + 1U);
          const auto source = state_->modules.find(owner);
          const auto function =
              source == state_->modules.end()
                  ? std::optional<Module::FunctionDecl>{}
                  : source->second.function(local);
          if (!function || function->results().front().domain != expected ||
              function->inputs().size() != expression.arguments.size()) {
            report_operation("unknown or ill-typed pure call '" +
                             expression.text + "' in operation '" + name +
                             "." + std::string(declaration.name()) + "'");
            return;
          }
          for (std::size_t index = 0; index < expression.arguments.size();
               ++index) {
            self(self, expression.arguments[index],
                 function->inputs()[index].domain);
          }
          return;
        }
        if (expression.kind == Kind::Number ||
            expression.kind == Kind::Boolean ||
            expression.kind == Kind::String) {
          const bool matches = (expression.kind == Kind::Number &&
                                (domain->element == detail::ValueKind::Integer ||
                                 domain->element == detail::ValueKind::Real)) ||
                               (expression.kind == Kind::Boolean &&
                                domain->element == detail::ValueKind::Boolean) ||
                               (expression.kind == Kind::String &&
                                domain->element == detail::ValueKind::String);
          if (!matches) {
            report_operation("literal in operation '" + name + "." +
                             std::string(declaration.name()) +
                             "' has the wrong kind");
          }
          return;
        }
        if (domain->element != detail::ValueKind::Type &&
            domain->element != detail::ValueKind::Attribute) {
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
        if (domain->element == detail::ValueKind::Type) {
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
          self(self, expression.arguments[index], parameters[index].domain);
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
      if (!contract.bindings.empty() &&
          contract.bindings.size() != declaration.inputs().size()) {
        report_operation("operation '" + name + "." +
                         std::string(declaration.name()) +
                         "' has an invalid type contract");
        continue;
      }
      for (const auto& input : detail::ir_inputs(declaration)) {
        validate_expression(validate_expression, input.domain,
                            detail::domain_expression(detail::ValueKind::Type));
      }
      for (std::size_t index = 0; index < declaration.inputs().size(); ++index) {
        if (!contract.bindings.empty() && contract.bindings[index]) {
          validate_expression(validate_expression,
                              *contract.bindings[index],
                              declaration.inputs()[index].domain);
        }
      }
      for (const auto& result : detail::ir_results(declaration)) {
        validate_expression(validate_expression, result.domain,
                            detail::domain_expression(detail::ValueKind::Type));
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
  const auto visit_function =
      [&](const auto& self, const Module::FunctionDecl& function,
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
                  owner->second, Module::SymbolKind::Function,
                  function.name());
    bool valid = true;
    const auto walk = [&](const auto& walk_self,
                          const Module::Expression& expression) -> void {
      if (expression.kind == Module::Expression::Kind::Call &&
          expression.text != "ceildiv" && expression.text != "min" &&
          expression.text != "max" && owner != state_->modules.end()) {
        const std::size_t dot = expression.text.find('.');
        const std::string module_name =
            dot == std::string::npos
                ? std::string(owner->second.name())
                : std::string(resolve_prefix(
                      owner->second,
                      std::string_view(expression.text).substr(0, dot)));
        const std::string local =
            dot == std::string::npos ? expression.text
                                     : expression.text.substr(dot + 1U);
        const auto dependency = state_->modules.find(module_name);
        const auto target =
            dependency == state_->modules.end()
                ? std::optional<Module::FunctionDecl>{}
                : dependency->second.function(local);
        if (target && !self(self, *target, location)) {
          valid = false;
        }
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
    for (const Module::FunctionDecl& function : module.functions()) {
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
  auto operation_verifiers = state_->operation_verifiers;
  auto attribute_methods = state_->attribute_methods;
  auto operation_methods = state_->operation_methods;
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
    state_->operation_verifiers = std::move(operation_verifiers);
    state_->attribute_methods = std::move(attribute_methods);
    state_->operation_methods = std::move(operation_methods);
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
  auto derived = detail::resolve_derived_parameters(
      *this, schema, *values, state_->diagnostics);
  state_->constructing_types.erase(construction);
  if (!derived) {
    return std::nullopt;
  }
  Type type = detail::TypeAccess::make(schema, std::move(*values),
                                       std::move(*derived));
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
  const auto declaration =
      owner == state_->modules.end() ? std::optional<Module::TypeDecl>{}
                                     : owner->second.type(prelude_type);
  if (!declaration) {
    state_->diagnostics.report("Prelude type '" + std::string(prelude_type) +
                               "' is unavailable");
    return std::nullopt;
  }
  return make(*declaration, std::span<const ParameterValue>{});
}

std::optional<Value> Compiler::make_known(Type type, ParameterValue value) {
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
    state_->diagnostics.report(
        "Known value payload does not match type '" +
        type.schema().symbol().qualified_name() + "'");
    return std::nullopt;
  }
  return Value(std::move(type), std::move(value));
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

std::optional<Function> Compiler::function() {
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
  return Function(std::move(modules));
}

std::optional<Function>
Compiler::function(Module::FunctionDecl declaration) {
  return function(std::move(declaration), {});
}

std::optional<Function>
Compiler::function(Module::FunctionDecl declaration,
                   std::vector<Value> known_arguments) {
  return function(declaration.symbol(), std::move(known_arguments));
}

std::optional<Function> Compiler::function(std::string_view name) {
  return function(name, {});
}

std::optional<Function>
Compiler::function(std::string_view name,
                   std::vector<Value> known_arguments) {
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

std::optional<Function> Compiler::function(Module::Symbol symbol) {
  return function(std::move(symbol), {});
}

std::optional<Function>
Compiler::function(Module::Symbol symbol,
                   std::vector<Value> known_arguments) {
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
                           MethodFunction<Instruction> function) {
  bind_interface_method(
      state_->linked, std::move(declaration), std::move(method),
      std::move(function), state_->operation_methods, state_->diagnostics,
      [this](const auto& subject, const auto& interface) {
        return conforms(subject, interface);
      },
      "operation");
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
Compiler::call(const Instruction& subject,
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

void Compiler::bind_verifier(Module::FunctionDecl schema,
                             VerifierFunction<Instruction> verifier) {
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

bool Compiler::bind_representation(Module::TypeDecl schema,
                                   std::string_view type) {
  if (!schema.parameters().empty()) {
    state_->diagnostics.report(
        "a parameterized host representation needs a projection returning "
        "its ordered type parameters");
    return false;
  }
  RepresentationProjector projector =
      [](Compiler& compiler, const Module::TypeDecl& declaration,
         const void*) { return compiler.make(declaration); };
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
  if (cpp_value_domain(type)) {
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
  if (const auto domain = cpp_value_domain(type)) {
    return pass_field_domain(field) == domain;
  }
  const auto declaration =
      field_type_declaration(state_->modules, function, field);
  const auto representation =
      declaration ? state_->host_representations.find(
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
    state_->diagnostics.report(
        "host type projection threw: " + std::string(exception.what()));
    return false;
  } catch (...) {
    state_->diagnostics.report("host type projection threw an unknown exception");
    return false;
  }
  if (!projected ||
      projected->schema() != representation->second.schema ||
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
    const detail::ExecutionValue* result) {
  const bool has_host_input =
      std::any_of(arguments.begin(), arguments.end(), [](const auto& value) {
        return std::holds_alternative<detail::HostValue>(value);
      });
  const bool has_host_result =
      result != nullptr &&
      std::holds_alternative<detail::HostValue>(*result);
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
    known_arguments.push_back(parameter_value(arguments[index]));
  }

  std::vector<std::optional<Type>> expected_results;
  expected_results.reserve(detail::ir_results(function).size());
  for (std::size_t index = 0; index < function.results().size(); ++index) {
    if (!contract.ir_results[index]) {
      continue;
    }
    const auto* host = result == nullptr
                           ? nullptr
                           : std::get_if<detail::HostValue>(result);
    expected_results.push_back(host == nullptr ? std::optional<Type>{}
                                               : host->concrete_type);
  }
  return detail::resolve_operation_types(
             *this, function, value_arguments, known_arguments,
             expected_results, state_->diagnostics)
      .has_value();
}

bool Compiler::check_binding_signature(
    const Module::FunctionDecl& schema,
    std::span<const std::string_view> inputs,
    std::optional<std::string_view> result) {
  const bool input_match = schema.inputs().size() == inputs.size() &&
                           std::equal(schema.inputs().begin(),
                                      schema.inputs().end(), inputs.begin(),
                                      [&](const auto& field, auto type) {
                                        return accepts_host_type(schema, field,
                                                                 type);
                                      });
  const bool result_match =
      schema.results().size() <= 1U &&
      (schema.results().empty() ? !result
                                : result && accepts_host_type(
                                                schema,
                                                schema.results().front(),
                                                *result));
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
    state_->diagnostics.report("compiler function '" +
                               symbol.qualified_name() +
                               "' already has a binding");
  }
}

std::optional<detail::ParameterValue> Compiler::evaluate_binding(
    Module::FunctionDecl function,
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
  std::vector<detail::ExecutionValue> values;
  values.reserve(arguments.size());
  const auto parameters = detail::parameter_inputs(function);
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    auto converted = execution_value(arguments[index], parameters[index]);
    if (!converted) {
      state_->diagnostics.report(
          "compiler execution cannot represent argument '" +
          parameters[index].name + "'");
      return std::nullopt;
    }
    values.push_back(std::move(*converted));
  }
  auto produced = execute(function, std::move(values), under_residual_control);
  if (!produced) {
    return std::nullopt;
  }
  auto result = parameter_value(*produced);
  if (!result ||
      !detail::matches_parameter(detail::parameter_results(function).front(),
                                 *result)) {
    state_->diagnostics.report("compiler execution of function '" +
                               function.symbol().qualified_name() +
                               "' produced a value with the wrong type");
    return std::nullopt;
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
    state_->diagnostics.report("operation '" + symbol.qualified_name() +
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
Compiler::lookup_method(const Instruction& subject, std::string_view reference) {
  return lookup_method(subject.callee(), reference);
}

bool Compiler::verify(const Function& function) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot verify a function before the compiler is linked");
    return false;
  }
  if (!detail::FunctionAccess::verify_structure(function, state_->diagnostics)) {
    return false;
  }
  bool valid = detail::FunctionAccess::verify_contracts(
      function, *this, state_->diagnostics);
  for (const Instruction& operation : function.instructions()) {
      const Module::FunctionDecl schema = operation.callee();
      const Module::Symbol symbol = schema.symbol();
      const auto location = detail::FunctionAccess::location(operation);
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
  }
  return valid;
}

bool Compiler::check_run_signature(
    const Module::FunctionDecl& schema,
    std::span<const std::string_view> inputs,
    std::optional<std::string_view> result) {
  if (matches_run_signature(schema, inputs, result)) {
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
    std::optional<std::string_view> result) const {
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
  const bool input_match = schema.inputs().size() == inputs.size() &&
                           std::equal(schema.inputs().begin(),
                                      schema.inputs().end(), inputs.begin(),
                                      [&](const auto& field, auto type) {
                                        return accepts_host_type(schema, field,
                                                                 type);
                                      });
  const bool result_match =
      schema.results().size() <= 1U &&
      (schema.results().empty() ? !result
                                : result && accepts_host_type(
                                                schema,
                                                schema.results().front(),
                                                *result));
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
  const auto declaration = owner->second.function(member->second);
  if (!declaration) {
    state_->diagnostics.report("unknown compiler function '" +
                               std::string(name) + "'");
  }
  return declaration;
}

std::optional<detail::ExecutionValue>
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
                           execution_value_type(arguments[index]))) {
      state_->diagnostics.report("compiler function '" +
                                 declaration.symbol().qualified_name() +
                                 "' received an argument with the wrong type");
      return std::nullopt;
    }
  }
  if (!check_host_values(declaration, arguments)) {
    return std::nullopt;
  }

  std::vector<std::pair<std::shared_ptr<Function>,
                        std::shared_ptr<const Function::Snapshot>>>
      checkpoints;
  for (detail::ExecutionValue& argument : arguments) {
    if (execution_value_type(argument) == typeid(Function).name()) {
      auto function = std::get<std::shared_ptr<Function>>(argument);
      if (!verify(*function)) {
        return std::nullopt;
      }
      checkpoints.emplace_back(function, function->snapshot());
    }
  }
  const std::size_t before = state_->diagnostics.size();
  std::size_t steps = 0;
  std::size_t depth = 0;
  const auto execute = [&](const auto& self, const Module::FunctionDecl& current,
                           std::vector<detail::ExecutionValue> values)
      -> std::optional<detail::ExecutionValue> {
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
                             execution_value_type(values[index]))) {
        state_->diagnostics.report("compiler function '" +
                                   current.symbol().qualified_name() +
                                   "' received an argument with the wrong type");
        return std::nullopt;
      }
      if (execution_value_type(values[index]) == typeid(Function).name()) {
        const auto function =
            std::get<std::shared_ptr<Function>>(values[index]);
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
      const auto binding = state_->bindings.find(current.symbol().stable_name());
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
      std::optional<detail::ExecutionValue> execution;
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
        state_->diagnostics.report(
            "C++ binding for compiler function '" +
            current.symbol().qualified_name() + "' threw: " +
            exception.what());
        return std::nullopt;
      } catch (...) {
        state_->diagnostics.report(
            "C++ binding for compiler function '" +
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
      if (!project_host_value(*execution)) {
        return std::nullopt;
      }
      if (!current.results().empty()) {
        if (current.results().size() != 1U ||
            !accepts_host_type(current, current.results().front(),
                               execution_value_type(*execution))) {
          state_->diagnostics.report("compiler function '" +
                                     current.symbol().qualified_name() +
                                     "' produced a value with the wrong type");
          return std::nullopt;
        }
      }
      if (!check_host_values(current, values, &*execution)) {
        return std::nullopt;
      }
      return execution;
    }
    case Module::FunctionDecl::Form::Body: {
      const auto owner = state_->modules.find(current.symbol().module_name());
      const auto body = owner == state_->modules.end()
                            ? std::shared_ptr<const detail::FunctionBody>{}
                            : detail::ModuleAccess::body(owner->second,
                                                         current);
      if (!body || body->blocks.size() != 1U ||
          body->blocks.front().terminator) {
        state_->diagnostics.report(
            "compiler execution of function '" +
            current.symbol().qualified_name() +
            "' requires a structured body rather than explicit CFG blocks");
        return std::nullopt;
      }

      enum class Control { Next, Return, Break, Continue, Error };
      struct Flow {
        Control control = Control::Next;
        std::optional<detail::ExecutionValue> value;
      };
      using Scope =
          std::unordered_map<std::string, detail::ExecutionValue>;
      using Scopes = std::vector<Scope>;

      Scopes scopes(1U);
      for (std::size_t index = 0; index < current.inputs().size(); ++index) {
        scopes.front().emplace(current.inputs()[index].name, values[index]);
      }
      const auto report = [&](std::string message,
                              detail::SyntaxRange range) {
        state_->diagnostics.report(
            std::move(message),
            SourceRange{body->source, range.begin, range.end});
      };
      const auto step = [&](detail::SyntaxRange range) {
        if (steps++ < state_->evaluation_limits.steps) {
          return true;
        }
        report("compiler execution step limit exceeded", range);
        return false;
      };
      const auto find_local = [](Scopes& environment,
                                 std::string_view name)
          -> detail::ExecutionValue* {
        for (auto scope = environment.rbegin(); scope != environment.rend();
             ++scope) {
          const auto found = scope->find(std::string(name));
          if (found != scope->end()) {
            return &found->second;
          }
        }
        return nullptr;
      };

      std::function<std::optional<detail::ExecutionValue>(
          const Module::Expression&, detail::SyntaxRange, Scopes&,
          const Module::ParameterDecl*)>
          evaluate;
      evaluate = [&](const Module::Expression& expression,
                     detail::SyntaxRange range, Scopes& environment,
                     const Module::ParameterDecl* expected)
          -> std::optional<detail::ExecutionValue> {
        if (!step(range)) {
          return std::nullopt;
        }
        using Kind = Module::Expression::Kind;
        if ((expression.kind == Kind::Variable ||
             expression.kind == Kind::Reference) &&
            expression.arguments.empty()) {
          if (auto* value = find_local(environment, expression.text)) {
            return *value;
          }
          if (expression.kind == Kind::Variable) {
            report("compiler function '" +
                       current.symbol().qualified_name() +
                       "' references unknown value '" + expression.text +
                       "'",
                   range);
            return std::nullopt;
          }
        }

        if (expression.kind == Kind::Number) {
          if (expression.text.find_first_of(".eE") == std::string::npos) {
            std::int64_t integer = 0;
            const auto parsed = std::from_chars(
                expression.text.data(),
                expression.text.data() + expression.text.size(), integer);
            if (parsed.ec == std::errc{} &&
                parsed.ptr ==
                    expression.text.data() + expression.text.size()) {
              return detail::ExecutionValue{integer};
            }
          } else {
            double real = 0.0;
            std::istringstream input(expression.text);
            input.imbue(std::locale::classic());
            input >> real;
            if (input && input.peek() == std::char_traits<char>::eof()) {
              return detail::ExecutionValue{real};
            }
          }
          report("invalid compiler numeric literal", range);
          return std::nullopt;
        }
        if (expression.kind == Kind::Boolean) {
          return detail::ExecutionValue{expression.text == "true"};
        }
        if (expression.kind == Kind::String) {
          return detail::ExecutionValue{expression.text};
        }
        if (expression.kind == Kind::Evaluate) {
          if (expression.arguments.size() != 1U) {
            report("malformed compiler evaluation expression", range);
            return std::nullopt;
          }
          return evaluate(expression.arguments.front(), range, environment,
                          expected);
        }
        if (expression.kind == Kind::If) {
          if (expression.arguments.size() != 3U) {
            report("malformed compiler if expression", range);
            return std::nullopt;
          }
          const Module::ParameterDecl condition{
              "condition",
              detail::domain_expression(detail::ValueKind::Boolean), false,
              std::nullopt};
          auto value = evaluate(expression.arguments[0], range, environment,
                                &condition);
          const bool* selected = value ? std::get_if<bool>(&*value) : nullptr;
          if (selected == nullptr) {
            report("compiler if condition must be bool", range);
            return std::nullopt;
          }
          return evaluate(expression.arguments[*selected ? 1U : 2U], range,
                          environment, expected);
        }
        if (expression.kind == Kind::List) {
          std::vector<detail::ExecutionValue> elements;
          elements.reserve(expression.arguments.size());
          for (const auto& element : expression.arguments) {
            auto value = evaluate(element, range, environment, nullptr);
            if (!value) {
              return std::nullopt;
            }
            elements.push_back(std::move(*value));
          }
          const auto domain =
              expected ? detail::kernel_domain(expected->domain)
                       : std::optional<detail::Domain>{};
          const auto element_type = [&]() -> std::string_view {
            if (!elements.empty()) {
              return execution_value_type(elements.front());
            }
            if (!domain || !domain->list) {
              return {};
            }
            switch (domain->element) {
            case detail::ValueKind::Integer:
              return typeid(std::int64_t).name();
            case detail::ValueKind::Real:
              return typeid(double).name();
            case detail::ValueKind::Boolean:
              return typeid(bool).name();
            case detail::ValueKind::String:
              return typeid(std::string).name();
            case detail::ValueKind::Type:
              return typeid(Type).name();
            case detail::ValueKind::Attribute:
              return typeid(Attribute).name();
            case detail::ValueKind::Bytes:
            case detail::ValueKind::Function:
              return {};
            }
            return {};
          }();
          if (element_type.empty() ||
              !std::all_of(elements.begin(), elements.end(),
                           [&](const detail::ExecutionValue& element) {
                             return execution_value_type(element) ==
                                    element_type;
                           })) {
            report(elements.empty()
                       ? "an empty compiler list needs a contextual element type"
                       : "compiler list elements have different types",
                   range);
            return std::nullopt;
          }
          if (element_type == typeid(std::int64_t).name()) {
            detail::IntegerList result;
            for (auto& element : elements) {
              result.push_back(std::get<std::int64_t>(element));
            }
            return detail::ExecutionValue{std::move(result)};
          }
          if (element_type == typeid(double).name()) {
            detail::RealList result;
            for (auto& element : elements) {
              result.push_back(std::get<double>(element));
            }
            return detail::ExecutionValue{std::move(result)};
          }
          if (element_type == typeid(bool).name()) {
            detail::BooleanList result;
            for (auto& element : elements) {
              result.push_back(std::get<bool>(element));
            }
            return detail::ExecutionValue{std::move(result)};
          }
          if (element_type == typeid(std::string).name()) {
            detail::StringList result;
            for (auto& element : elements) {
              result.push_back(std::get<std::string>(std::move(element)));
            }
            return detail::ExecutionValue{std::move(result)};
          }
          if (element_type == typeid(Type).name()) {
            detail::TypeList result;
            for (auto& element : elements) {
              result.push_back(std::get<Type>(std::move(element)));
            }
            return detail::ExecutionValue{std::move(result)};
          }
          if (element_type == typeid(Attribute).name()) {
            detail::AttributeList result;
            for (auto& element : elements) {
              result.push_back(std::get<Attribute>(std::move(element)));
            }
            return detail::ExecutionValue{std::move(result)};
          }
          report("compiler list element type is not representable", range);
          return std::nullopt;
        }

        if (expression.kind == Kind::Prefix ||
            expression.kind == Kind::Infix ||
            expression.kind == Kind::Postfix ||
            expression.kind == Kind::FunctionType ||
            (expression.kind == Kind::Reference && expected != nullptr)) {
          std::optional<Module::ParameterDecl> inferred;
          if (expected == nullptr &&
              (expression.kind == Kind::Prefix ||
               expression.kind == Kind::Infix ||
               expression.kind == Kind::Postfix) &&
              !expression.arguments.empty()) {
            const Module::Expression& operand = expression.arguments.front();
            std::optional<detail::Domain> domain;
            if ((operand.kind == Kind::Variable ||
                 operand.kind == Kind::Reference) &&
                operand.arguments.empty()) {
              if (const auto* value = find_local(environment, operand.text)) {
                domain = cpp_value_domain(execution_value_type(*value));
              }
            } else if (operand.kind == Kind::Number) {
              domain = detail::Domain{
                  operand.text.find_first_of(".eE") == std::string::npos
                      ? detail::ValueKind::Integer
                      : detail::ValueKind::Real,
                  false};
            }
            if (domain && !domain->list) {
              inferred = Module::ParameterDecl{
                  "operator result",
                  detail::domain_expression(domain->element), false,
                  std::nullopt};
              expected = &*inferred;
            }
          }
          if (expected == nullptr) {
            report("compiler operator needs a contextual result type", range);
            return std::nullopt;
          }
          detail::KnownBindings bindings;
          for (const auto& scope : environment) {
            for (const auto& [name, stored] : scope) {
              if (auto value = parameter_value(stored)) {
                bindings.insert_or_assign(name, std::move(*value));
              }
            }
          }
          auto value = detail::evaluate_known_expression(
              *this, current.symbol().module_name(), expression, *expected,
              bindings, state_->diagnostics,
              SourceRange{body->source, range.begin, range.end},
              !under_residual_control);
          return value ? execution_value(*value, *expected)
                       : std::optional<detail::ExecutionValue>{};
        }

        if (expression.kind != Kind::Call) {
          report("compiler function '" +
                     current.symbol().qualified_name() +
                     "' contains an unsupported expression",
                 range);
          return std::nullopt;
        }
        const auto next = resolve_function(state_->modules,
                                           current.symbol().module_name(),
                                           expression.text);
        if (!next) {
          report("function '" + current.symbol().qualified_name() +
                     "' cannot resolve one callee named '" + expression.text +
                     "'",
                 range);
          return std::nullopt;
        }
        const auto parameters = next->inputs();
        std::vector<std::optional<detail::ExecutionValue>> bound(
            parameters.size());
        std::size_t positional = 0;
        for (std::size_t index = 0; index < expression.arguments.size();
             ++index) {
          const std::string_view label =
              index < expression.labels.size() ? expression.labels[index]
                                               : std::string_view{};
          std::size_t target = parameters.size();
          if (!label.empty()) {
            const auto found = std::find_if(
                parameters.begin(), parameters.end(),
                [&](const Module::ParameterDecl& parameter) {
                  return parameter.name == label;
                });
            if (found != parameters.end()) {
              target = static_cast<std::size_t>(
                  std::distance(parameters.begin(), found));
            }
          } else if (positional < parameters.size()) {
            target = positional++;
          }
          if (target == parameters.size() || bound[target]) {
            report("compiler call has invalid argument placement", range);
            return std::nullopt;
          }
          auto value = evaluate(expression.arguments[index], range,
                                environment, &parameters[target]);
          if (!value) {
            return std::nullopt;
          }
          bound[target] = std::move(*value);
        }
        std::vector<detail::ExecutionValue> call_arguments;
        call_arguments.reserve(parameters.size());
        for (std::size_t index = 0; index < parameters.size(); ++index) {
          if (!bound[index] && parameters[index].default_value) {
            const auto value = detail::parameter_default(parameters[index]);
            bound[index] = value ? execution_value(*value, parameters[index])
                                 : std::nullopt;
          }
          if (!bound[index]) {
            report("compiler call is missing argument '" +
                       parameters[index].name + "'",
                   range);
            return std::nullopt;
          }
          call_arguments.push_back(std::move(*bound[index]));
        }
        return self(self, *next, std::move(call_arguments));
      };

      std::function<Flow(std::span<const detail::StatementSyntax>, Scopes&)>
          execute_statements;
      execute_statements = [&](std::span<const detail::StatementSyntax> code,
                               Scopes& environment) -> Flow {
        for (const detail::StatementSyntax& statement : code) {
          if (!step(statement.range)) {
            return {Control::Error, std::nullopt};
          }
          if (statement.kind == detail::StatementSyntax::Kind::Return) {
            if (statement.values.size() != current.results().size() ||
                statement.values.size() > 1U) {
              report("compiler return does not match its function signature",
                     statement.range);
              return {Control::Error, std::nullopt};
            }
            if (statement.values.empty()) {
              return {Control::Return, detail::ExecutionValue{}};
            }
            auto value = evaluate(statement.values.front().value,
                                  statement.values.front().range, environment,
                                  &current.results().front());
            return value ? Flow{Control::Return, std::move(value)}
                         : Flow{Control::Error, std::nullopt};
          }
          if (statement.kind == detail::StatementSyntax::Kind::Break) {
            return {Control::Break, std::nullopt};
          }
          if (statement.kind == detail::StatementSyntax::Kind::Continue) {
            return {Control::Continue, std::nullopt};
          }
          if (statement.kind == detail::StatementSyntax::Kind::If) {
            const Module::ParameterDecl condition{
                "condition",
                detail::domain_expression(detail::ValueKind::Boolean), false,
                std::nullopt};
            auto value = evaluate(statement.expression.value,
                                  statement.expression.range, environment,
                                  &condition);
            const bool* selected = value ? std::get_if<bool>(&*value) : nullptr;
            if (selected == nullptr) {
              report("compiler if condition must be bool", statement.range);
              return {Control::Error, std::nullopt};
            }
            environment.emplace_back();
            Flow flow = execute_statements(
                *selected ? std::span(statement.body)
                          : std::span(statement.otherwise),
                environment);
            environment.pop_back();
            if (flow.control != Control::Next) {
              return flow;
            }
            continue;
          }
          if (statement.kind == detail::StatementSyntax::Kind::While) {
            while (true) {
              const Module::ParameterDecl condition{
                  "condition",
                  detail::domain_expression(detail::ValueKind::Boolean), false,
                  std::nullopt};
              auto value = evaluate(statement.expression.value,
                                    statement.expression.range, environment,
                                    &condition);
              const bool* selected =
                  value ? std::get_if<bool>(&*value) : nullptr;
              if (selected == nullptr) {
                report("compiler while condition must be bool",
                       statement.range);
                return {Control::Error, std::nullopt};
              }
              if (!*selected) {
                break;
              }
              environment.emplace_back();
              Flow flow = execute_statements(statement.body, environment);
              environment.pop_back();
              if (flow.control == Control::Return ||
                  flow.control == Control::Error) {
                return flow;
              }
              if (flow.control == Control::Break) {
                break;
              }
            }
            continue;
          }

          const Module::ParameterDecl* expected = nullptr;
          if (statement.bindings.size() > 1U) {
            report("compiler execution currently supports one call result",
                   statement.range);
            return {Control::Error, std::nullopt};
          }
          auto value = evaluate(statement.expression.value,
                                statement.expression.range, environment,
                                expected);
          if (!value) {
            return {Control::Error, std::nullopt};
          }
          if (statement.bindings.empty()) {
            continue;
          }
          const detail::BindingSyntax& binding = statement.bindings.front();
          if (binding.rebind) {
            auto* target = find_local(environment, binding.name);
            if (target == nullptr) {
              report("cannot rebind unknown compiler value '" +
                         binding.name + "'",
                     binding.range);
              return {Control::Error, std::nullopt};
            }
            *target = std::move(*value);
          } else if (!environment.back()
                          .emplace(binding.name, std::move(*value))
                          .second) {
            report("compiler value '" + binding.name +
                       "' is already defined in this scope",
                   binding.range);
            return {Control::Error, std::nullopt};
          }
        }
        return {};
      };

      Flow flow = execute_statements(body->blocks.front().statements, scopes);
      if (flow.control != Control::Return || !flow.value) {
        if (flow.control != Control::Error) {
          report("compiler function path falls through without returning",
                 body->range);
        }
        return std::nullopt;
      }
      const bool result_matches =
          current.results().empty()
              ? std::holds_alternative<std::monostate>(*flow.value)
              : current.results().size() == 1U &&
                    accepts_host_type(current, current.results().front(),
                                      execution_value_type(*flow.value));
      if (!result_matches) {
        report("compiler function returned a value with the wrong type",
               body->range);
        return std::nullopt;
      }
      if (!project_host_value(*flow.value) ||
          !check_host_values(current, values, &*flow.value)) {
        return std::nullopt;
      }
      return flow.value;
    }
    }
    return std::nullopt;
  };

  std::optional<detail::ExecutionValue> result;
  try {
    result = execute(execute, declaration, std::move(arguments));
  } catch (...) {
    for (auto& [function, snapshot] : checkpoints) {
      function->restore(std::move(snapshot));
    }
    throw;
  }
  bool valid = result.has_value() && state_->diagnostics.size() == before;
  if (valid && execution_value_type(*result) == typeid(Function).name()) {
    valid = verify(*std::get<std::shared_ptr<Function>>(*result)) && valid;
  }
  if (!valid) {
    for (auto& [function, snapshot] : checkpoints) {
      function->restore(std::move(snapshot));
    }
    return std::nullopt;
  }
  return result;
}

bool Compiler::run(Function& function, Module::FunctionDecl transform) {
  const Module::Symbol symbol = transform.symbol();
  const bool function_transform =
      transform.inputs().size() == 1U &&
      pass_field_domain(transform.inputs().front()) ==
          detail::Domain{detail::ValueKind::Function, false} &&
      transform.results().size() == 1U &&
      pass_field_domain(transform.results().front()) ==
          detail::Domain{detail::ValueKind::Function, false};
  if (!function_transform) {
    state_->diagnostics.report("compiler function '" +
                               symbol.qualified_name() +
                               "' is not a function -> function transformation");
    return false;
  }
  std::vector<detail::ExecutionValue> arguments;
  arguments.push_back(
      {std::shared_ptr<Function>(&function, [](Function*) {})});
  return execute(std::move(transform), std::move(arguments)).has_value();
}

bool Compiler::run(Function& function, std::string_view transform) {
  const auto declaration = find_function(transform);
  return declaration && run(function, *declaration);
}

const Diagnostics& Compiler::diagnostics() const { return state_->diagnostics; }

}  // namespace joggle
