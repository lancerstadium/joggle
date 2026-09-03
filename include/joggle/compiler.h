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
#include <typeinfo>
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

// Controls whether a native implementation may be evaluated while the
// current source path is guarded by Residual control. Hermetic is an explicit
// promise that evaluation is deterministic and has no observable host effect.
enum class HostEvaluation { Guarded, Hermetic };

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

using ExecutionValue =
    std::variant<std::int64_t, double, bool, std::string, Type, Attribute,
                 Bytes, std::shared_ptr<ir::Function>, IntegerList, RealList,
                 BooleanList, StringList, TypeList, AttributeList, HostValue>;
using ExecutionValues = std::vector<ExecutionValue>;

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

template <typename T> ExecutionValue store_execution_value(T&& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, ir::Function>) {
    return {std::make_shared<ir::Function>(std::forward<T>(value))};
  } else if constexpr (is_builtin_host_value<Value>) {
    return {Value(std::forward<T>(value))};
  } else {
    return {HostValue{std::string(host_type_name<Value>()),
                      std::make_shared<Value>(std::forward<T>(value))}};
  }
}

template <typename T> ExecutionValue store_execution_input(T&& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, ir::Function>) {
    return {std::make_shared<ir::Function>(std::forward<T>(value))};
  } else if constexpr (is_builtin_host_value<Value>) {
    return {Value(std::forward<T>(value))};
  } else {
    return {HostValue{std::string(host_type_name<Value>()),
                      std::make_shared<Value>(std::forward<T>(value))}};
  }
}

template <typename T> decltype(auto) execution_argument(ExecutionValue& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, ir::Function>) {
    auto& function = *std::get<std::shared_ptr<ir::Function>>(value);
    if constexpr (std::is_reference_v<T>) {
      return static_cast<T>(function);
    } else {
      return function;
    }
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

template <typename T>
std::optional<T> take_execution_value(ExecutionValue value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, ir::Function>) {
    auto function = std::get<std::shared_ptr<ir::Function>>(std::move(value));
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

template <typename> struct IsTuple : std::false_type {};

template <typename... Values>
struct IsTuple<std::tuple<Values...>> : std::true_type {};

template <typename T>
inline constexpr bool is_tuple = IsTuple<std::remove_cvref_t<T>>::value;

template <typename T> std::vector<std::string_view> execution_result_types() {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_void_v<Value>) {
    return {};
  } else if constexpr (is_tuple<Value>) {
    static_assert(std::tuple_size_v<Value> >= 2U,
                  "use T for one function result and void for no results");
    return []<std::size_t... Indices>(std::index_sequence<Indices...>) {
      return std::vector<std::string_view>{
          host_type_name<std::tuple_element_t<Indices, Value>>()...};
    }(std::make_index_sequence<std::tuple_size_v<Value>>{});
  } else {
    return {host_type_name<Value>()};
  }
}

template <typename T> ExecutionValues store_execution_values(T&& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (is_tuple<Value>) {
    ExecutionValues result;
    result.reserve(std::tuple_size_v<Value>);
    std::apply(
        [&](auto&&... elements) {
          (result.push_back(store_execution_value(
               std::forward<decltype(elements)>(elements))),
           ...);
        },
        std::forward<T>(value));
    return result;
  } else {
    ExecutionValues result;
    result.push_back(store_execution_value(std::forward<T>(value)));
    return result;
  }
}

template <typename Tuple, std::size_t... Indices>
std::optional<Tuple> take_execution_tuple(ExecutionValues values,
                                          std::index_sequence<Indices...>) {
  if (values.size() != sizeof...(Indices)) {
    return std::nullopt;
  }
  std::tuple<std::optional<std::tuple_element_t<Indices, Tuple>>...> decoded{
      take_execution_value<std::tuple_element_t<Indices, Tuple>>(
          std::move(values[Indices]))...};
  if (!(std::get<Indices>(decoded).has_value() && ...)) {
    return std::nullopt;
  }
  return Tuple{std::move(*std::get<Indices>(decoded))...};
}

template <typename T>
std::optional<T> take_execution_values(ExecutionValues values) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (is_tuple<Value>) {
    return take_execution_tuple<Value>(
        std::move(values),
        std::make_index_sequence<std::tuple_size_v<Value>>{});
  } else {
    return values.size() == 1U
               ? take_execution_value<T>(std::move(values.front()))
               : std::nullopt;
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
std::optional<ExecutionValues>
invoke_typed_function(Function& function, Compiler& compiler,
                      std::span<ExecutionValue> arguments,
                      Diagnostics& diagnostics,
                      std::index_sequence<Indices...>) {
  using Traits = CallableTraits<Function>;
  using Produced = typename Traits::result;
  const auto invoke = [&]() -> Produced {
    if constexpr (WithCompiler && WithDiagnostics) {
      return std::invoke(
          function, compiler,
          execution_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...,
          diagnostics);
    } else if constexpr (WithCompiler) {
      return std::invoke(
          function, compiler,
          execution_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...);
    } else if constexpr (WithDiagnostics) {
      return std::invoke(
          function,
          execution_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...,
          diagnostics);
    } else {
      return std::invoke(
          function,
          execution_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...);
    }
  };

  if constexpr (std::is_void_v<Produced>) {
    invoke();
    return ExecutionValues{};
  } else {
    auto produced = invoke();
    using Value = std::remove_cvref_t<Produced>;
    if constexpr (OptionalValue<Value>::value) {
      if (!produced) {
        return std::nullopt;
      }
      return store_execution_values(std::move(*produced));
    } else {
      return store_execution_values(std::move(produced));
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
  using NativeFunction = std::function<std::optional<detail::ExecutionValues>(
      Compiler&, std::span<detail::ExecutionValue>, Diagnostics&)>;
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
  // Resolves one uniquely named callable member after linking. The name must
  // be qualified as module.function; overloads remain explicit because this
  // lookup has no call arguments from which to infer a selection.
  std::optional<Module::Function> lookup(std::string_view qualified);
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
    static_assert(
        requires { std::tuple_size<Parameters>::value; },
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
            return compiler.make(declaration,
                                 std::forward<decltype(values)>(values)...);
          },
          std::move(parameters));
    };
    return bind_representation(
        std::move(schema), detail::host_type_name<Value>(), std::move(erased));
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

  template <typename T> std::optional<ir::Value> known(Type type, T&& value) {
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
  // Creates an empty executable body in this linked compilation.
  std::optional<ir::Function> body();

  // Specializes a source-defined Function and materializes its residual body.
  // The declaration, symbol, or qualified name selects the same Module member;
  // known_arguments bind compile-time parameters before residualization.
  std::optional<ir::Function> materialize(Module::Function declaration);
  std::optional<ir::Function>
  materialize(Module::Function declaration,
              std::vector<ir::Value> known_arguments);
  std::optional<ir::Function> materialize(Module::Symbol symbol);
  std::optional<ir::Function>
  materialize(Module::Symbol symbol, std::vector<ir::Value> known_arguments);
  std::optional<ir::Function> materialize(std::string_view name);
  std::optional<ir::Function>
  materialize(std::string_view name, std::vector<ir::Value> known_arguments);

  bool conforms(const Module::TypeDecl& declaration,
                const Module::InterfaceDecl& interface) const;
  bool conforms(const Module::AttributeDecl& declaration,
                const Module::InterfaceDecl& interface) const;
  bool conforms(const Module::Function& declaration,
                const Module::InterfaceDecl& interface) const;

  template <typename Result, typename... Arguments, typename Function>
  void bind(Module::AttributeDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_typed_method<Attribute, Result, Arguments...>(
        std::move(declaration), std::move(method),
        std::forward<Function>(function));
  }

  template <typename Result, typename... Arguments, typename Function>
  void bind(Module::Function declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_typed_method<ir::Instruction, Result, Arguments...>(
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
  void bind(Module::Function declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_inferred<ir::Instruction>(std::move(declaration), std::move(method),
                                   std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::AttributeDecl declaration, std::string_view method,
            Function&& function) {
    const auto member = lookup_method(declaration, method);
    if (member) {
      bind(std::move(declaration), *member, std::forward<Function>(function));
    }
  }

  template <typename Function>
  void bind(Module::Function declaration, std::string_view method,
            Function&& function) {
    const auto member = lookup_method(declaration, method);
    if (member) {
      bind(std::move(declaration), *member, std::forward<Function>(function));
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
                      std::is_same_v<Subject, ir::Instruction>,
                  "an interface call subject must be an Attribute or "
                  "Instruction; Type fields use Type::get");
    const auto declaration = lookup_method(subject, method);
    return declaration ? call<Result>(subject, *declaration,
                                      std::forward<Arguments>(arguments)...)
                       : std::nullopt;
  }

  template <typename Function>
  void verify(Module::TypeDecl schema, Function&& function) {
    bind_typed_verifier<Type>(std::move(schema),
                              std::forward<Function>(function));
  }

  template <typename Function>
  void verify(Module::AttributeDecl schema, Function&& function) {
    bind_typed_verifier<Attribute>(std::move(schema),
                                   std::forward<Function>(function));
  }

  template <typename Function>
  void verify(Module::Function schema, Function&& function) {
    bind_typed_verifier<ir::Instruction>(std::move(schema),
                                         std::forward<Function>(function));
  }

  // Binds an implementation whose C++ input and result types match the
  // declared fn. Semantic validators use verify() instead.
  template <typename Function>
  void bind(Module::Function schema, Function&& function,
            HostEvaluation evaluation = HostEvaluation::Guarded) {
    using Callable = std::decay_t<Function>;
    using Traits = detail::CallableTraits<Callable>;
    using Arguments = typename Traits::arguments;
    constexpr std::size_t arity = Traits::arity;
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
      return std::array<std::string_view, sizeof...(Indices)>{
          detail::host_type_name<
              std::tuple_element_t<offset + Indices, Arguments>>()...};
    }(std::make_index_sequence<argument_count>{});
    using Produced = std::remove_cvref_t<typename Traits::result>;
    std::vector<std::string_view> result_types;
    if constexpr (!std::is_void_v<Produced>) {
      using Value =
          std::conditional_t<detail::OptionalValue<Produced>::value,
                             typename detail::OptionalValue<Produced>::type,
                             Produced>;
      result_types = detail::execution_result_types<Value>();
    }
    if (!check_binding_signature(schema, argument_types, result_types)) {
      return;
    }
    NativeFunction binding = [callable =
                                  Callable(std::forward<Function>(function))](
                                 Compiler& compiler,
                                 std::span<detail::ExecutionValue> arguments,
                                 Diagnostics& diagnostics) mutable {
      if (arguments.size() != argument_count) {
        diagnostics.report("function binding received the wrong argument "
                           "count");
        return std::optional<detail::ExecutionValues>{};
      }
      return detail::invoke_typed_function<Callable, Arguments, offset,
                                           with_compiler, with_diagnostics>(
          callable, compiler, arguments, diagnostics,
          std::make_index_sequence<argument_count>{});
    };
    bind_native(std::move(schema), std::move(binding), evaluation);
  }

  bool verify(const ir::Function& function);
  bool run(ir::Function& function, Module::Function transform);
  bool run(ir::Function& function, std::string_view transform);

  template <typename Result = void, typename... Arguments>
  bool invocable(const Module::Function& function) const {
    const std::array<std::string_view, sizeof...(Arguments)> inputs{
        detail::host_type_name<Arguments>()...};
    return matches_run_signature(function, inputs,
                                 detail::execution_result_types<Result>());
  }

  template <typename Result = void, typename... Arguments>
  std::conditional_t<std::is_void_v<Result>, bool, std::optional<Result>>
  run(Module::Function function, Arguments&&... arguments) {
    const std::array<std::string_view, sizeof...(Arguments)> types{
        detail::host_type_name<Arguments>()...};
    const auto result_types = detail::execution_result_types<Result>();
    if (!check_run_signature(function, types, result_types)) {
      if constexpr (std::is_void_v<Result>) {
        return false;
      } else {
        return std::nullopt;
      }
    }
    std::vector<detail::ExecutionValue> values;
    values.reserve(sizeof...(Arguments));
    (values.push_back(detail::store_execution_input<Arguments>(
         std::forward<Arguments>(arguments))),
     ...);
    auto results = execute(std::move(function), std::move(values));
    if constexpr (std::is_void_v<Result>) {
      return results.has_value();
    } else {
      return results
                 ? detail::take_execution_values<Result>(std::move(*results))
                 : std::optional<Result>{};
    }
  }

  template <typename Result = void, typename... Arguments>
  std::conditional_t<std::is_void_v<Result>, bool, std::optional<Result>>
  run(std::string_view name, Arguments&&... arguments) {
    const auto declaration = lookup(name);
    if (!declaration) {
      if constexpr (std::is_void_v<Result>) {
        return false;
      } else {
        return std::nullopt;
      }
    }
    return run<Result>(*declaration, std::forward<Arguments>(arguments)...);
  }
  const Diagnostics& diagnostics() const;

private:
  std::optional<ir::Value> make_known(Type type, detail::ParameterValue value);
  std::optional<Type> make(const Module::TypeDecl& schema,
                           std::span<const detail::ParameterValue> parameters);
  std::optional<Attribute>
  make(const Module::AttributeDecl& schema,
       std::span<const detail::ParameterValue> parameters);
  void bind_method(Module::AttributeDecl declaration,
                   Module::InterfaceDecl::MethodDecl method,
                   MethodFunction<Attribute> function);
  void bind_method(Module::Function declaration,
                   Module::InterfaceDecl::MethodDecl method,
                   MethodFunction<ir::Instruction> function);
  void bind_verifier(Module::TypeDecl schema, VerifierFunction<Type> verifier);
  void bind_verifier(Module::AttributeDecl schema,
                     VerifierFunction<Attribute> verifier);
  void bind_verifier(Module::Function schema,
                     VerifierFunction<ir::Instruction> verifier);
  bool bind_representation(Module::TypeDecl schema, std::string_view type);
  bool bind_representation(Module::TypeDecl schema, std::string_view type,
                           RepresentationProjector projector);
  bool project_host_value(detail::ExecutionValue& value);
  bool check_host_values(const Module::Function& function,
                         std::span<const detail::ExecutionValue> arguments,
                         std::span<const detail::ExecutionValue> results = {});
  bool accepts_host_type(const Module::Function& function,
                         const Module::ParameterDecl& field,
                         std::string_view type) const;
  void bind_native(Module::Function schema, NativeFunction function,
                   HostEvaluation evaluation);
  void bind_prelude_module();
  void bind_prelude_primitives();
  bool check_binding_signature(const Module::Function& schema,
                               std::span<const std::string_view> inputs,
                               std::span<const std::string_view> results);
  bool check_run_signature(const Module::Function& schema,
                           std::span<const std::string_view> inputs,
                           std::span<const std::string_view> results);
  bool matches_run_signature(const Module::Function& schema,
                             std::span<const std::string_view> inputs,
                             std::span<const std::string_view> results) const;
  std::optional<detail::ExecutionValues>
  execute(Module::Function declaration,
          std::vector<detail::ExecutionValue> arguments,
          bool under_residual_control = false);
  std::optional<detail::ParameterValue>
  evaluate_binding(Module::Function function,
                   std::span<const detail::ParameterValue> arguments,
                   bool under_residual_control);
  bool can_evaluate_binding(const Module::Function& function,
                            bool under_residual_control) const;
  std::optional<detail::ParameterValue>
  call(const Attribute& subject, Module::InterfaceDecl::MethodDecl method,
       std::span<const detail::ParameterValue> parameters);
  std::optional<detail::ParameterValue>
  call(const ir::Instruction& subject, Module::InterfaceDecl::MethodDecl method,
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
          } else if constexpr (std::is_invocable_r_v<bool, Callable&,
                                                     const Subject&>) {
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
  lookup_method(Module::Function declaration, std::string_view reference);
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
  lookup_method(const ir::Instruction& subject, std::string_view reference);
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
