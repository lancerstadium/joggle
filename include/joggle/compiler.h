#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/ir.h"
#include "joggle/module.h"
#include "joggle/type.h"

namespace joggle {

using Bytes = std::vector<std::byte>;
class Compiler;

namespace detail {

struct CompilerAccess;

struct HostValue {
  std::string cpp_type;
  std::shared_ptr<void> storage;
  std::optional<Type> concrete_type;
};

using IntegerList = std::vector<std::int64_t>;
using RealList = std::vector<double>;
using BooleanList = std::vector<bool>;
using StringList = std::vector<std::string>;
using TypeList = std::vector<Type>;
using AttributeList = std::vector<Attribute>;

using PassValue =
    std::variant<std::monostate, std::int64_t, double, bool, std::string, Type,
                 Attribute, Bytes, std::shared_ptr<Function>, IntegerList,
                 RealList, BooleanList, StringList, TypeList, AttributeList,
                 HostValue>;

template <typename T>
inline constexpr bool is_builtin_host_value =
    std::is_same_v<std::remove_cvref_t<T>, std::int64_t> ||
    std::is_same_v<std::remove_cvref_t<T>, double> ||
    std::is_same_v<std::remove_cvref_t<T>, bool> ||
    std::is_same_v<std::remove_cvref_t<T>, std::string> ||
    std::is_same_v<std::remove_cvref_t<T>, Type> ||
    std::is_same_v<std::remove_cvref_t<T>, Attribute> ||
    std::is_same_v<std::remove_cvref_t<T>, Bytes> ||
    std::is_same_v<std::remove_cvref_t<T>, IntegerList> ||
    std::is_same_v<std::remove_cvref_t<T>, RealList> ||
    std::is_same_v<std::remove_cvref_t<T>, BooleanList> ||
    std::is_same_v<std::remove_cvref_t<T>, StringList> ||
    std::is_same_v<std::remove_cvref_t<T>, TypeList> ||
    std::is_same_v<std::remove_cvref_t<T>, AttributeList>;

template <typename T> std::string_view host_type_name() {
  return typeid(std::remove_cvref_t<T>).name();
}

inline bool has_domain(const Module::ParameterDecl& field,
                       std::string_view domain) {
  return field.domain.kind == Module::Expression::Kind::Reference &&
         field.domain.text == domain && field.domain.arguments.empty();
}

template <typename T> PassValue store_pass_value(T&& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, Function>) {
    return {std::make_shared<Function>(std::forward<T>(value))};
  } else if constexpr (is_builtin_host_value<Value>) {
    return {Value(std::forward<T>(value))};
  } else {
    return {HostValue{std::string(host_type_name<Value>()),
                      std::make_shared<Value>(std::forward<T>(value))}};
  }
}

template <typename T> PassValue store_pass_input(T&& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, Function>) {
    static_assert(std::is_lvalue_reference_v<T>,
                  "a Function compiler input must be an lvalue");
    static_assert(!std::is_const_v<std::remove_reference_t<T>>,
                  "compiler-function invocation requires a non-const Function "
                  "handle");
    return {std::shared_ptr<Function>(std::addressof(value), [](Function*) {})};
  } else if constexpr (is_builtin_host_value<Value>) {
    return {Value(std::forward<T>(value))};
  } else {
    return {HostValue{std::string(host_type_name<Value>()),
                      std::make_shared<Value>(std::forward<T>(value))}};
  }
}

template <typename T> decltype(auto) pass_argument(PassValue& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, Function>) {
    auto& function = *std::get<std::shared_ptr<Function>>(value);
    return static_cast<T>(function);
  } else if constexpr (is_builtin_host_value<Value>) {
    auto& stored = std::get<Value>(value);
    return static_cast<T>(stored);
  } else {
    auto& host = std::get<HostValue>(value);
    if (host.cpp_type != host_type_name<Value>() || !host.storage) {
      throw std::bad_variant_access{};
    }
    auto& stored = *static_cast<Value*>(host.storage.get());
    return static_cast<T>(stored);
  }
}

template <typename T> std::optional<T> take_pass_value(PassValue value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, Function>) {
    auto function = std::get<std::shared_ptr<Function>>(std::move(value));
    return function ? std::optional<T>{std::move(*function)} : std::nullopt;
  } else if constexpr (is_builtin_host_value<Value>) {
    return std::get<Value>(std::move(value));
  } else {
    auto host = std::get<HostValue>(std::move(value));
    if (host.cpp_type != host_type_name<Value>() || !host.storage) {
      return std::nullopt;
    }
    return std::move(*static_cast<Value*>(host.storage.get()));
  }
}

template <typename> struct OptionalValue {
  static constexpr bool value = false;
  using type = void;
};

template <typename T> struct OptionalValue<std::optional<T>> {
  static constexpr bool value = true;
  using type = T;
};

template <typename T>
struct CallableTraits : CallableTraits<decltype(&T::operator())> {};

template <typename Result, typename... Arguments>
struct CallableTraits<Result(Arguments...)> {
  using result = Result;
  using arguments = std::tuple<Arguments...>;
  static constexpr std::size_t arity = sizeof...(Arguments);
};

template <typename Result, typename... Arguments>
struct CallableTraits<Result (*)(Arguments...)>
    : CallableTraits<Result(Arguments...)> {};

template <typename Owner, typename Result, typename... Arguments>
struct CallableTraits<Result (Owner::*)(Arguments...)>
    : CallableTraits<Result(Arguments...)> {};

template <typename Owner, typename Result, typename... Arguments>
struct CallableTraits<Result (Owner::*)(Arguments...) const>
    : CallableTraits<Result(Arguments...)> {};

template <typename Owner, typename Result, typename... Arguments>
struct CallableTraits<Result (Owner::*)(Arguments...) noexcept>
    : CallableTraits<Result(Arguments...)> {};

template <typename Owner, typename Result, typename... Arguments>
struct CallableTraits<Result (Owner::*)(Arguments...) const noexcept>
    : CallableTraits<Result(Arguments...)> {};

template <typename Function, typename Arguments, std::size_t Offset,
          bool WithCompiler, bool WithDiagnostics, std::size_t... Indices>
std::optional<PassValue>
invoke_typed_pass(Function& function, Compiler& compiler,
                  std::span<PassValue> arguments, Diagnostics& diagnostics,
                  bool function_transform, std::index_sequence<Indices...>) {
  using Traits = CallableTraits<Function>;
  using Produced = typename Traits::result;
  const auto invoke = [&]() -> Produced {
    if constexpr (WithCompiler && WithDiagnostics) {
      return std::invoke(
          function, compiler,
          pass_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...,
          diagnostics);
    } else if constexpr (WithCompiler) {
      return std::invoke(
          function, compiler,
          pass_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...);
    } else if constexpr (WithDiagnostics) {
      return std::invoke(
          function,
          pass_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...,
          diagnostics);
    } else {
      return std::invoke(
          function,
          pass_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...);
    }
  };

  if constexpr (std::is_void_v<Produced>) {
    invoke();
    return PassValue{};
  } else {
    auto produced = invoke();
    if constexpr (std::is_same_v<std::remove_cvref_t<Produced>, bool>) {
      if (function_transform) {
        return produced ? std::optional<PassValue>{arguments.front()}
                        : std::nullopt;
      }
    }
    using Value = std::remove_cvref_t<Produced>;
    if constexpr (OptionalValue<Value>::value) {
      if (!produced) {
        return std::nullopt;
      }
      return store_pass_value(std::move(*produced));
    } else {
      return store_pass_value(std::move(produced));
    }
  }
}

template <typename Result, typename... Arguments, typename Function,
          typename Subject, std::size_t... Indices>
std::optional<ParameterValue>
invoke_typed_method(Function& function, const Subject& subject,
                    std::span<const ParameterValue> arguments,
                    Diagnostics& diagnostics, std::index_sequence<Indices...>) {
  if (arguments.size() != sizeof...(Arguments)) {
    diagnostics.report("typed interface binding received the wrong argument "
                       "count");
    return std::nullopt;
  }
  auto decoded = std::tuple{
      decode_parameter<std::remove_cvref_t<Arguments>>(arguments[Indices])...};
  const bool valid = std::apply(
      [](const auto&... values) { return (values.has_value() && ...); },
      decoded);
  if (!valid) {
    diagnostics.report(
        "typed interface binding disagrees with the declared argument domains");
    return std::nullopt;
  }
  const auto encode_result = []<typename Produced>(Produced&& produced) {
    using Value = std::remove_cvref_t<Produced>;
    if constexpr (OptionalValue<Value>::value) {
      static_assert(std::is_same_v<typename OptionalValue<Value>::type, Result>,
                    "typed interface binding returns the wrong optional type");
      return produced ? std::optional<ParameterValue>{encode_parameter(
                            std::move(*produced))}
                      : std::optional<ParameterValue>{};
    } else {
      static_assert(std::is_same_v<Value, Result>,
                    "typed interface binding returns the wrong type");
      return std::optional<ParameterValue>{
          encode_parameter(std::forward<Produced>(produced))};
    }
  };
  if constexpr (std::is_invocable_v<Function&, const Subject&,
                                    std::remove_cvref_t<Arguments>...,
                                    Diagnostics&>) {
    return encode_result(std::invoke(function, subject,
                                     std::move(*std::get<Indices>(decoded))...,
                                     diagnostics));
  } else {
    static_assert(std::is_invocable_v<Function&, const Subject&,
                                      std::remove_cvref_t<Arguments>...>,
                  "a typed interface binding must accept its subject and "
                  "declared arguments, with an optional Diagnostics& last");
    return encode_result(std::invoke(
        function, subject, std::move(*std::get<Indices>(decoded))...));
  }
}

template <typename Result, typename... Arguments, typename Function,
          typename Subject>
std::optional<ParameterValue>
invoke_typed_method(Function& function, const Subject& subject,
                    std::span<const ParameterValue> arguments,
                    Diagnostics& diagnostics) {
  return invoke_typed_method<Result, Arguments...>(
      function, subject, arguments, diagnostics,
      std::index_sequence_for<Arguments...>{});
}

}  // namespace detail

class Compiler {
private:
  template <typename Subject>
  using MethodFunction = std::function<std::optional<detail::ParameterValue>(
      const Subject&, std::span<const detail::ParameterValue>, Diagnostics&)>;
  template <typename Subject>
  using VerifierFunction = std::function<bool(const Subject&, Diagnostics&)>;
  using PassFunction = std::function<std::optional<detail::PassValue>(
      Compiler&, std::span<detail::PassValue>, Diagnostics&)>;
  using RepresentationProjector = std::function<std::optional<Type>(
      Compiler&, const Module::TypeDecl&, const void*)>;

public:
  struct EvaluationLimits {
    std::size_t steps = 100000;
    std::size_t depth = 256;
  };

  Compiler();
  explicit Compiler(EvaluationLimits limits);
  ~Compiler();
  Compiler(Compiler&&) noexcept;
  Compiler& operator=(Compiler&&) noexcept;
  Compiler(const Compiler&) = delete;
  Compiler& operator=(const Compiler&) = delete;

  // Adds a module without resolving its imports. Errors accumulate in this
  // compiler and can be inspected after loading a complete module set.
  void add(std::string_view text, std::string source = "<memory>");
  void load(const std::filesystem::path& path);
  void search(std::filesystem::path root);
  void lock(const std::filesystem::path& path);

  // Checks the complete module closure and seals it on success.
  bool link();
  bool ok() const;
  bool linked() const;
  EvaluationLimits evaluation_limits() const;

  std::optional<Module> module(std::string_view name) const;
  std::vector<Module> modules() const;
  bool load_behavior(std::string_view module,
                     const std::filesystem::path& library);
  bool load_behavior(std::string_view module);

  // Associates an ordinary C++ value type with a Module-declared type for
  // compiler-function invocation. The Module remains the schema authority.
  template <typename T> bool represent(Module::TypeDecl schema) {
    using Value = std::remove_cvref_t<T>;
    static_assert(std::is_copy_constructible_v<Value>,
                  "a host representation must be copy constructible");
    return bind_representation(std::move(schema),
                               detail::host_type_name<Value>());
  }

  // A parameterized host representation projects a C++ value to the ordered
  // parameters of its Module Type declaration. A std::tuple keeps the Module
  // as the schema authority without requiring a wrapper or generated class.
  template <typename T, typename Projection>
  bool represent(Module::TypeDecl schema, Projection&& projection) {
    using Value = std::remove_cvref_t<T>;
    using Callable = std::decay_t<Projection>;
    using Parameters =
        std::remove_cvref_t<std::invoke_result_t<Callable&, const Value&>>;
    static_assert(std::is_copy_constructible_v<Value>,
                  "a host representation must be copy constructible");
    static_assert(requires { std::tuple_size<Parameters>::value; },
                  "a host type projection must return a std::tuple");
    RepresentationProjector erased =
        [callable = Callable(std::forward<Projection>(projection))](
            Compiler& compiler, const Module::TypeDecl& declaration,
            const void* storage) mutable -> std::optional<Type> {
      if (storage == nullptr) {
        return std::nullopt;
      }
      auto parameters =
          std::invoke(callable, *static_cast<const Value*>(storage));
      return std::apply(
          [&](auto&&... values) {
            return compiler.make(
                declaration,
                std::forward<decltype(values)>(values)...);
          },
          std::move(parameters));
    };
    return bind_representation(std::move(schema),
                               detail::host_type_name<Value>(),
                               std::move(erased));
  }

  template <typename... Arguments>
  std::optional<Type> make(const Module::TypeDecl& schema,
                           Arguments&&... arguments) {
    std::vector<detail::ParameterValue> values;
    values.reserve(sizeof...(Arguments));
    (values.push_back(
         detail::encode_parameter(std::forward<Arguments>(arguments))),
     ...);
    return make(schema, std::span<const detail::ParameterValue>(values));
  }

  // Constructs an ambient, parameterless Prelude type such as int, i32, f32,
  // or index. Parameterized Prelude types use the declaration overload above.
  std::optional<Type> make(std::string_view prelude_type);

  template <typename T> std::optional<Value> known(Type type, T&& value) {
    return make_known(std::move(type),
                      detail::encode_parameter(std::forward<T>(value)));
  }

  template <typename... Arguments>
  std::optional<Attribute> make(const Module::AttributeDecl& schema,
                                Arguments&&... arguments) {
    std::vector<detail::ParameterValue> values;
    values.reserve(sizeof...(Arguments));
    (values.push_back(
         detail::encode_parameter(std::forward<Arguments>(arguments))),
     ...);
    return make(schema, std::span<const detail::ParameterValue>(values));
  }
  std::optional<Function> function();
  std::optional<Function> function(Module::FunctionDecl declaration);
  std::optional<Function> function(Module::FunctionDecl declaration,
                                   std::vector<Value> known_arguments);
  std::optional<Function> function(Module::Symbol symbol);
  std::optional<Function> function(Module::Symbol symbol,
                                   std::vector<Value> known_arguments);
  std::optional<Function> function(std::string_view name);
  std::optional<Function> function(std::string_view name,
                                   std::vector<Value> known_arguments);

  bool conforms(const Module::TypeDecl& declaration,
                const Module::InterfaceDecl& interface) const;
  bool conforms(const Module::AttributeDecl& declaration,
                const Module::InterfaceDecl& interface) const;
  bool conforms(const Module::FunctionDecl& declaration,
                const Module::InterfaceDecl& interface) const;

  template <typename Result, typename... Arguments, typename Function>
  void bind(Module::AttributeDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_typed_method<Attribute, Result, Arguments...>(
        std::move(declaration), std::move(method),
        std::forward<Function>(function));
  }

  template <typename Result, typename... Arguments, typename Function>
  void bind(Module::FunctionDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_typed_method<Instruction, Result, Arguments...>(
        std::move(declaration), std::move(method),
        std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::AttributeDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_inferred<Attribute>(std::move(declaration), std::move(method),
                             std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::FunctionDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_inferred<Instruction>(std::move(declaration), std::move(method),
                             std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::AttributeDecl declaration, std::string_view method,
            Function&& function) {
    const auto member = lookup_method(declaration, method);
    if (member) {
      bind(std::move(declaration), *member,
           std::forward<Function>(function));
    }
  }

  template <typename Function>
  void bind(Module::FunctionDecl declaration, std::string_view method,
            Function&& function) {
    const auto member = lookup_method(declaration, method);
    if (member) {
      bind(std::move(declaration), *member,
           std::forward<Function>(function));
    }
  }

  template <typename Result, typename Subject, typename... Arguments>
  std::optional<Result> call(const Subject& subject,
                             Module::InterfaceDecl::MethodDecl schema,
                             Arguments&&... arguments) {
    if (!check_method_result(
            schema, detail::cpp_parameter<std::remove_cvref_t<Result>>())) {
      return std::nullopt;
    }
    std::vector<detail::ParameterValue> values;
    values.reserve(sizeof...(Arguments));
    (values.push_back(
         detail::encode_parameter(std::forward<Arguments>(arguments))),
     ...);
    auto result = call(subject, std::move(schema),
                       std::span<const detail::ParameterValue>(values));
    return result ? detail::decode_parameter<Result>(*result) : std::nullopt;
  }

  template <typename Result, typename Subject, typename... Arguments>
  std::optional<Result> call(const Subject& subject, std::string_view method,
                             Arguments&&... arguments) {
    static_assert(std::is_same_v<Subject, Attribute> ||
                      std::is_same_v<Subject, Instruction>,
                  "an interface call subject must be an Attribute or "
                  "Instruction; Type fields use Type::get");
    const auto declaration = lookup_method(subject, method);
    return declaration ? call<Result>(subject, *declaration,
                                      std::forward<Arguments>(arguments)...)
                       : std::nullopt;
  }

  template <typename Function>
  void bind(Module::TypeDecl schema, Function&& function) {
    bind_typed_verifier<Type>(std::move(schema),
                              std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::AttributeDecl schema, Function&& function) {
    bind_typed_verifier<Attribute>(std::move(schema),
                                   std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::FunctionDecl schema, Function&& function) {
    using Callable = std::decay_t<Function>;
    using Traits = detail::CallableTraits<Callable>;
    using Arguments = typename Traits::arguments;
    constexpr std::size_t arity = Traits::arity;
    constexpr bool operation_verifier = [] {
      if constexpr (arity == 0U) {
        return false;
      } else {
        return std::is_same_v<
            std::remove_cvref_t<std::tuple_element_t<0, Arguments>>,
            Instruction>;
      }
    }();
    if constexpr (operation_verifier) {
      bind_typed_verifier<Instruction>(std::move(schema),
                                     std::forward<Function>(function));
    } else {
    constexpr bool with_compiler = [] {
      if constexpr (arity == 0U) {
        return false;
      } else {
        return std::is_same_v<std::tuple_element_t<0, Arguments>, Compiler&>;
      }
    }();
    constexpr bool with_diagnostics = [] {
      if constexpr (arity == 0U) {
        return false;
      } else {
        return std::is_same_v<std::tuple_element_t<arity - 1U, Arguments>,
                              Diagnostics&>;
      }
    }();
    constexpr std::size_t argument_count =
        arity - static_cast<std::size_t>(with_compiler) -
        static_cast<std::size_t>(with_diagnostics);
    constexpr std::size_t offset = with_compiler ? 1U : 0U;
    const auto argument_types = []<std::size_t... Indices>(
                                    std::index_sequence<Indices...>) {
      static_assert(
          ((!std::is_same_v<
                std::remove_cvref_t<
                    std::tuple_element_t<offset + Indices, Arguments>>,
                joggle::Function> ||
            std::is_reference_v<
                std::tuple_element_t<offset + Indices, Arguments>>) &&
           ...),
          "a Function compiler input must be Function& or const Function&");
      return std::array<std::string_view, sizeof...(Indices)>{
          detail::host_type_name<
              std::tuple_element_t<offset + Indices, Arguments>>()...};
    }(std::make_index_sequence<argument_count>{});
    const bool function_transform =
        schema.inputs().size() == 1U &&
        detail::has_domain(schema.inputs().front(), "function") &&
        schema.results().size() == 1U &&
        detail::has_domain(schema.results().front(), "function");
    using Produced = std::remove_cvref_t<typename Traits::result>;
    std::optional<std::string_view> result_type;
    if constexpr (!std::is_void_v<Produced>) {
      using Value = std::conditional_t<detail::OptionalValue<Produced>::value,
                                       typename detail::OptionalValue<Produced>::type,
                                       Produced>;
      if constexpr (std::is_same_v<Produced, bool>) {
        result_type =
            function_transform ? detail::host_type_name<joggle::Function>()
                               : detail::host_type_name<bool>();
      } else {
        result_type = detail::host_type_name<Value>();
      }
    }
    if (!check_pass_signature(schema, argument_types, result_type)) {
      return;
    }
    PassFunction pass =
        [callable = Callable(std::forward<Function>(function)),
         function_transform](Compiler& compiler,
                          std::span<detail::PassValue> arguments,
                          Diagnostics& diagnostics) mutable {
          if (arguments.size() != argument_count) {
            diagnostics.report(
                "compiler-function binding received the wrong argument count");
            return std::optional<detail::PassValue>{};
          }
          return detail::invoke_typed_pass<Callable, Arguments, offset,
                                           with_compiler, with_diagnostics>(
              callable, compiler, arguments, diagnostics, function_transform,
              std::make_index_sequence<argument_count>{});
        };
    bind_pass(std::move(schema), std::move(pass));
    }
  }

  bool verify(const Function& function);
  bool run(Function& function, Module::FunctionDecl pass);
  bool run(Function& function, std::string_view pass);

  template <typename Result = void, typename... Arguments>
  std::conditional_t<std::is_void_v<Result>, bool, std::optional<Result>>
  run(Module::FunctionDecl pass, Arguments&&... arguments) {
    const std::array<std::string_view, sizeof...(Arguments)> types{
        detail::host_type_name<Arguments>()...};
    const std::optional<std::string_view> result_type = [] {
      if constexpr (std::is_void_v<Result>) {
        return std::optional<std::string_view>{};
      } else {
        return std::optional<std::string_view>{
            detail::host_type_name<Result>()};
      }
    }();
    if (!check_run_signature(pass, types, result_type)) {
      if constexpr (std::is_void_v<Result>) {
        return false;
      } else {
        return std::nullopt;
      }
    }
    std::vector<detail::PassValue> values;
    values.reserve(sizeof...(Arguments));
    (values.push_back(
         detail::store_pass_input<Arguments>(std::forward<Arguments>(arguments))),
     ...);
    auto value = run_pass(std::move(pass), std::move(values));
    if constexpr (std::is_void_v<Result>) {
      return value.has_value();
    } else {
      return value ? detail::take_pass_value<Result>(std::move(*value))
                   : std::optional<Result>{};
    }
  }

  template <typename Result = void, typename... Arguments>
  std::conditional_t<std::is_void_v<Result>, bool, std::optional<Result>>
  run(std::string_view pass, Arguments&&... arguments) {
    const auto declaration = find_pass(pass);
    if (!declaration) {
      if constexpr (std::is_void_v<Result>) {
        return false;
      } else {
        return std::nullopt;
      }
    }
    return run<Result>(*declaration,
                       std::forward<Arguments>(arguments)...);
  }
  const Diagnostics& diagnostics() const;

private:
  std::optional<Value> make_known(Type type,
                                  detail::ParameterValue value);
  std::optional<Type> make(const Module::TypeDecl& schema,
                           std::span<const detail::ParameterValue> parameters);
  std::optional<Attribute>
  make(const Module::AttributeDecl& schema,
       std::span<const detail::ParameterValue> parameters);
  void bind_method(Module::AttributeDecl declaration,
                   Module::InterfaceDecl::MethodDecl method,
                   MethodFunction<Attribute> function);
  void bind_method(Module::FunctionDecl declaration,
                   Module::InterfaceDecl::MethodDecl method,
                   MethodFunction<Instruction> function);
  void bind_verifier(Module::TypeDecl schema, VerifierFunction<Type> verifier);
  void bind_verifier(Module::AttributeDecl schema,
                     VerifierFunction<Attribute> verifier);
  void bind_verifier(Module::FunctionDecl schema,
                     VerifierFunction<Instruction> verifier);
  bool bind_representation(Module::TypeDecl schema, std::string_view type);
  bool bind_representation(Module::TypeDecl schema, std::string_view type,
                           RepresentationProjector projector);
  bool project_host_value(detail::PassValue& value);
  bool check_host_values(const Module::FunctionDecl& function,
                         std::span<const detail::PassValue> arguments,
                         const detail::PassValue* result = nullptr);
  bool accepts_host_type(const Module::FunctionDecl& function,
                         const Module::ParameterDecl& field,
                         std::string_view type) const;
  void bind_pass(Module::FunctionDecl schema, PassFunction function);
  bool check_pass_signature(
      const Module::FunctionDecl& schema,
      std::span<const std::string_view> inputs,
      std::optional<std::string_view> result);
  bool check_run_signature(
      const Module::FunctionDecl& schema,
      std::span<const std::string_view> inputs,
      std::optional<std::string_view> result);
  std::optional<detail::PassValue>
  run_pass(Module::FunctionDecl pass,
           std::vector<detail::PassValue> arguments);
  std::optional<detail::ParameterValue>
  evaluate_binding(Module::FunctionDecl function,
                   std::span<const detail::ParameterValue> arguments);
  std::optional<Module::FunctionDecl> find_pass(std::string_view pass);
  std::optional<detail::ParameterValue>
  call(const Attribute& subject, Module::InterfaceDecl::MethodDecl method,
       std::span<const detail::ParameterValue> parameters);
  std::optional<detail::ParameterValue>
  call(const Instruction& subject, Module::InterfaceDecl::MethodDecl method,
       std::span<const detail::ParameterValue> parameters);

  template <typename Subject, typename Result, typename... Arguments,
            typename Declaration, typename Function>
  void bind_typed_method(Declaration declaration,
                         Module::InterfaceDecl::MethodDecl method,
                         Function&& function) {
    const std::array<Module::ParameterDecl, sizeof...(Arguments)> parameters{
        detail::cpp_parameter<std::remove_cvref_t<Arguments>>()...};
    if (!check_method_signature(
            method, parameters,
            detail::cpp_parameter<std::remove_cvref_t<Result>>())) {
      return;
    }
    using Callable = std::decay_t<Function>;
    MethodFunction<Subject> erased =
        [callable = Callable(std::forward<Function>(function))](
            const Subject& subject,
            std::span<const detail::ParameterValue> arguments,
            Diagnostics& diagnostics) mutable {
          return detail::invoke_typed_method<Result, Arguments...>(
              callable, subject, arguments, diagnostics);
        };
    bind_method(std::move(declaration), std::move(method), std::move(erased));
  }

  template <typename Subject, typename Declaration, typename Function>
  void bind_typed_verifier(Declaration declaration, Function&& function) {
    using Callable = std::decay_t<Function>;
    VerifierFunction<Subject> verifier =
        [callable = Callable(std::forward<Function>(function))](
            const Subject& value, Diagnostics& diagnostics) mutable {
          if constexpr (std::is_invocable_r_v<bool, Callable&, const Subject&,
                                              Diagnostics&>) {
            return std::invoke(callable, value, diagnostics);
          } else if constexpr (
              std::is_invocable_r_v<bool, Callable&, const Subject&>) {
            return std::invoke(callable, value);
          } else {
            static_assert(
                std::is_invocable_r_v<bool, Callable&, const Subject&>,
                "a verifier binding must accept its subject, with an optional "
                "Diagnostics& last, and return bool");
            return false;
          }
        };
    bind_verifier(std::move(declaration), std::move(verifier));
  }

  template <typename Subject, typename Declaration, typename Function>
  void bind_inferred(Declaration declaration,
                     Module::InterfaceDecl::MethodDecl method,
                     Function&& function) {
    using Callable = std::decay_t<Function>;
    using Traits = detail::CallableTraits<Callable>;
    if constexpr (Traits::arity < 1U) {
      static_assert(Traits::arity >= 1U, "a method binding needs a subject");
    } else {
      using Parameters = typename Traits::arguments;
      constexpr bool has_diagnostics =
          std::is_same_v<std::tuple_element_t<Traits::arity - 1U, Parameters>,
                         Diagnostics&>;
      bind_inferred<Subject>(
          std::move(declaration), std::move(method),
          std::forward<Function>(function),
          std::make_index_sequence<Traits::arity - 1U - has_diagnostics>{});
    }
  }

  template <typename Subject, typename Declaration, typename Function,
            std::size_t... Indices>
  void bind_inferred(Declaration declaration,
                     Module::InterfaceDecl::MethodDecl method,
                     Function&& function, std::index_sequence<Indices...>) {
    using Callable = std::decay_t<Function>;
    using Traits = detail::CallableTraits<Callable>;
    using Parameters = typename Traits::arguments;
    using SubjectParameter = std::tuple_element_t<0, Parameters>;
    static_assert(std::is_same_v<SubjectParameter, const Subject&>,
                  "the first method binding parameter must be the subject");
    using Produced = std::remove_cvref_t<typename Traits::result>;
    using Result =
        std::conditional_t<detail::OptionalValue<Produced>::value,
                           typename detail::OptionalValue<Produced>::type,
                           Produced>;
    this->template bind<
        Result,
        std::remove_cvref_t<std::tuple_element_t<Indices + 1U, Parameters>>...>(
        std::move(declaration), std::move(method),
        std::forward<Function>(function));
  }

  bool check_method_result(Module::InterfaceDecl::MethodDecl method,
                           const Module::ParameterDecl& result);
  bool check_method_signature(Module::InterfaceDecl::MethodDecl method,
                              std::span<const Module::ParameterDecl> parameters,
                              const Module::ParameterDecl& result);
  std::optional<Module> lookup_module(const Module& module);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(Module::AttributeDecl declaration, std::string_view reference);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(Module::FunctionDecl declaration, std::string_view reference);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(const Module& module, std::string_view reference,
                Module::SymbolKind subject);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(const Module& module, std::string_view declaration,
                std::span<const std::string> interfaces,
                std::string_view reference, Module::SymbolKind subject);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(const Attribute& subject, std::string_view reference);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(const Instruction& subject, std::string_view reference);
  bool load_behavior(const Module& module,
                     const std::filesystem::path& library);
  bool load_behavior(const Module& module);
  void add_module(Module module, bool explicit_module,
                  std::optional<std::filesystem::path> source);

  struct State;
  std::unique_ptr<State> state_;
  friend struct detail::CompilerAccess;
};

}  // namespace joggle
