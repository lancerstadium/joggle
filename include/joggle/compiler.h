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
using ExecutionValue =
    std::variant<std::int64_t, double, bool, std::string, Type,
                 Bytes, std::shared_ptr<Function>, IntegerList, RealList,
                 BooleanList, StringList, TypeList, HostValue>;
using ExecutionValues = std::vector<ExecutionValue>;

template <typename T>
inline constexpr bool is_builtin_host_value =
    std::is_same_v<std::remove_cvref_t<T>, std::int64_t> ||
    std::is_same_v<std::remove_cvref_t<T>, double> ||
    std::is_same_v<std::remove_cvref_t<T>, bool> ||
    std::is_same_v<std::remove_cvref_t<T>, std::string> ||
    std::is_same_v<std::remove_cvref_t<T>, Type> ||
    std::is_same_v<std::remove_cvref_t<T>, Bytes> ||
    std::is_same_v<std::remove_cvref_t<T>, IntegerList> ||
    std::is_same_v<std::remove_cvref_t<T>, RealList> ||
    std::is_same_v<std::remove_cvref_t<T>, BooleanList> ||
    std::is_same_v<std::remove_cvref_t<T>, StringList> ||
    std::is_same_v<std::remove_cvref_t<T>, TypeList>;

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
  if constexpr (std::is_same_v<Value, Function>) {
    return {std::make_shared<Function>(std::forward<T>(value))};
  } else if constexpr (is_builtin_host_value<Value>) {
    return {Value(std::forward<T>(value))};
  } else {
    return {HostValue{std::string(host_type_name<Value>()),
                      std::make_shared<Value>(std::forward<T>(value)),
                      std::nullopt}};
  }
}

template <typename T> decltype(auto) execution_argument(ExecutionValue& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, Function>) {
    auto& function = *std::get<std::shared_ptr<Function>>(value);
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

template <typename T>
inline constexpr bool valid_function_input =
    !std::is_reference_v<T> || (std::is_lvalue_reference_v<T> &&
                                std::is_const_v<std::remove_reference_t<T>>);

template <typename T>
inline constexpr bool valid_function_result = !std::is_reference_v<T>;

// The C++ spelling of an ordinary fn implementation. Compiler& and
// Diagnostics& are host services rather than declared arguments, so the same
// traits drive both overload selection and type-erased invocation.
template <typename Function> struct FunctionBinding {
  using Callable = std::decay_t<Function>;
  using Traits = CallableTraits<Callable>;
  using Arguments = typename Traits::arguments;
  static constexpr std::size_t arity = Traits::arity;
  static constexpr bool with_compiler = [] {
    if constexpr (arity == 0U) {
      return false;
    } else {
      return std::is_same_v<std::tuple_element_t<0, Arguments>, Compiler&>;
    }
  }();
  static constexpr bool with_diagnostics = [] {
    if constexpr (arity == 0U) {
      return false;
    } else {
      return std::is_same_v<std::tuple_element_t<arity - 1U, Arguments>,
                            Diagnostics&>;
    }
  }();
  static constexpr std::size_t argument_count =
      arity - static_cast<std::size_t>(with_compiler) -
      static_cast<std::size_t>(with_diagnostics);
  static constexpr std::size_t offset = with_compiler ? 1U : 0U;
  static_assert(valid_function_result<typename Traits::result>,
                "fn results must be values");
  using Produced = std::remove_cvref_t<typename Traits::result>;
  using Result =
      std::conditional_t<OptionalValue<Produced>::value,
                         typename OptionalValue<Produced>::type, Produced>;

  static auto input_types() {
    return []<std::size_t... Indices>(std::index_sequence<Indices...>) {
      static_assert((valid_function_input<
                         std::tuple_element_t<offset + Indices, Arguments>> &&
                     ...),
                    "fn inputs must be values or const references");
      return std::array<std::string_view, sizeof...(Indices)>{host_type_name<
          std::tuple_element_t<offset + Indices, Arguments>>()...};
    }(std::make_index_sequence<argument_count>{});
  }

  static std::vector<std::string_view> result_types() {
    return execution_result_types<Result>();
  }
};

template <typename Function, typename Arguments, std::size_t Offset,
          bool WithCompiler, bool WithDiagnostics, std::size_t... Indices>
std::optional<ExecutionValues> invoke_typed_function(
    Function& function, Compiler& compiler, std::span<ExecutionValue> arguments,
    Diagnostics& diagnostics, std::index_sequence<Indices...>) {
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

}  // namespace detail

class Compiler {
private:
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
  void add(Module module);
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
  std::optional<Module::FunctionDecl> lookup(std::string_view qualified);
  bool load_native(std::string_view module,
                   const std::filesystem::path& library);
  bool load_native(std::string_view module);

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

  template <typename T> std::optional<Value> known(Type type, T&& value) {
    return make_known(std::move(type),
                      detail::encode_parameter(std::forward<T>(value)));
  }

  // Creates an empty executable body in this linked compilation.
  std::optional<Function> create_function();

  // Materializes every source-defined Function whose results are
  // IR-representable and whose compiler inputs and generics have a complete
  // default specialization. Other declarations stay declarations in the
  // returned copy; the linked Module is unchanged.
  std::optional<Module> materialize(const Module& module);

  // Specializes a source-defined Function and materializes its residual body.
  // The declaration, symbol, or qualified name selects the same Module member;
  // known_arguments bind compile-time parameters before residualization.
  std::optional<Function> materialize(Module::FunctionDecl declaration);
  std::optional<Function> materialize(Module::FunctionDecl declaration,
                                      std::vector<Value> known_arguments);
  std::optional<Function> materialize(Module::Symbol symbol);
  std::optional<Function> materialize(Module::Symbol symbol,
                                      std::vector<Value> known_arguments);
  std::optional<Function> materialize(std::string_view name);
  std::optional<Function> materialize(std::string_view name,
                                      std::vector<Value> known_arguments);
  // Specializes a source-defined callee from one already typed call. Concrete
  // operand, property, and result types recover the call's generic bindings.
  std::optional<Function> materialize(const Op& call);
  std::optional<Function> materialize(const Op& call,
                                      Diagnostics& diagnostics);

  // Recursively specializes source-defined calls until every remaining call
  // is accepted by boundary. The returned Module owns each concrete
  // specialization and the input Module is not modified. An unaccepted
  // external call, recursive source expansion, or invalid rewrite fails the
  // whole operation.
  std::optional<Module>
  specialize(const Module& module,
             const std::function<bool(const Module::FunctionDecl&)>& boundary,
             Diagnostics& diagnostics);

  template <typename Function>
  void verify(Module::TypeDecl schema, Function&& function) {
    bind_typed_verifier<Type>(std::move(schema),
                              std::forward<Function>(function));
  }

  template <typename Function>
  void verify(Module::FunctionDecl schema, Function&& function) {
    bind_typed_verifier<Op>(std::move(schema),
                            std::forward<Function>(function));
  }

  // Binds an implementation whose C++ input and result types match the
  // declared fn. Semantic validators use verify() instead.
  // A Module and local name are sufficient for normal package code. The
  // callable's C++ signature selects an overload without a generated wrapper.
  template <typename Function>
  void bind(const Module& module, std::string_view name, Function&& function,
            HostEvaluation evaluation = HostEvaluation::Guarded) {
    using Binding = detail::FunctionBinding<Function>;
    const auto inputs = Binding::input_types();
    const auto results = Binding::result_types();
    const auto declaration = lookup_binding(module, name, inputs, results);
    if (declaration) {
      bind(*declaration, std::forward<Function>(function), evaluation);
    }
  }

  // A declaration handle remains useful when an implementation already
  // reflects members for rewriting or when exact identity is intentional.
  template <typename Function>
  void bind(Module::FunctionDecl schema, Function&& function,
            HostEvaluation evaluation = HostEvaluation::Guarded) {
    using Binding = detail::FunctionBinding<Function>;
    using Callable = typename Binding::Callable;
    using Arguments = typename Binding::Arguments;
    constexpr std::size_t argument_count = Binding::argument_count;
    constexpr std::size_t offset = Binding::offset;
    constexpr bool with_compiler = Binding::with_compiler;
    constexpr bool with_diagnostics = Binding::with_diagnostics;
    const auto argument_types = Binding::input_types();
    const auto result_types = Binding::result_types();
    if (!check_binding_signature(schema, argument_types, result_types)) {
      return;
    }
    NativeFunction binding =
        [callable = Callable(std::forward<Function>(function))](
            Compiler& compiler, std::span<detail::ExecutionValue> arguments,
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

  bool verify(const Function& function);
  bool verify(const Module& module);

  template <typename Result = void, typename... Arguments>
  bool invocable(const Module::FunctionDecl& function) const {
    const std::array<std::string_view, sizeof...(Arguments)> inputs{
        detail::host_type_name<Arguments>()...};
    return matches_run_signature(function, inputs,
                                 detail::execution_result_types<Result>());
  }

  template <typename Result = void, typename... Arguments>
  std::conditional_t<std::is_void_v<Result>, bool, std::optional<Result>>
  run(Module::FunctionDecl function, Arguments&&... arguments) {
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
    (values.push_back(
         detail::store_execution_value(std::forward<Arguments>(arguments))),
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
    const std::array<std::string_view, sizeof...(Arguments)> inputs{
        detail::host_type_name<Arguments>()...};
    const auto results = detail::execution_result_types<Result>();
    const auto declaration = lookup_run(name, inputs, results);
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
  std::optional<Value> make_known(Type type, detail::ParameterValue value);
  std::optional<Type> make(const Module::TypeDecl& schema,
                           std::span<const detail::ParameterValue> parameters);
  void bind_verifier(Module::TypeDecl schema, VerifierFunction<Type> verifier);
  void bind_verifier(Module::FunctionDecl schema,
                     VerifierFunction<Op> verifier);
  bool bind_representation(Module::TypeDecl schema, std::string_view type);
  bool bind_representation(Module::TypeDecl schema, std::string_view type,
                           RepresentationProjector projector);
  bool project_host_value(detail::ExecutionValue& value);
  bool check_host_values(const Module::FunctionDecl& function,
                         std::span<const detail::ExecutionValue> arguments,
                         std::span<const detail::ExecutionValue> results = {});
  bool accepts_host_type(const Module::FunctionDecl& function,
                         const Module::ParameterDecl& field,
                         std::string_view type) const;
  void bind_native(Module::FunctionDecl schema, NativeFunction function,
                   HostEvaluation evaluation);
  void bind_prelude_module();
  void bind_prelude_primitives();
  bool check_binding_signature(const Module::FunctionDecl& schema,
                               std::span<const std::string_view> inputs,
                               std::span<const std::string_view> results);
  std::optional<Module::FunctionDecl>
  lookup_binding(const Module& module, std::string_view name,
                 std::span<const std::string_view> inputs,
                 std::span<const std::string_view> results);
  std::optional<Module::FunctionDecl>
  resolve_host_overload(const Module& module, std::string_view name,
                        std::span<const std::string_view> inputs,
                        std::span<const std::string_view> results,
                        std::string_view purpose);
  std::optional<Module::FunctionDecl>
  lookup_run(std::string_view name, std::span<const std::string_view> inputs,
             std::span<const std::string_view> results);
  bool check_run_signature(const Module::FunctionDecl& schema,
                           std::span<const std::string_view> inputs,
                           std::span<const std::string_view> results);
  bool matches_run_signature(const Module::FunctionDecl& schema,
                             std::span<const std::string_view> inputs,
                             std::span<const std::string_view> results) const;
  std::optional<detail::ExecutionValues>
  execute(Module::FunctionDecl declaration,
          std::vector<detail::ExecutionValue> arguments,
          bool under_residual_control = false);
  std::optional<detail::ParameterValue>
  evaluate_binding(Module::FunctionDecl function,
                   std::span<const detail::ParameterValue> arguments,
                   bool under_residual_control);
  bool can_evaluate_binding(const Module::FunctionDecl& function,
                            bool under_residual_control) const;
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

  std::optional<Module> lookup_module(const Module& module);
  bool load_native(const Module& module, const std::filesystem::path& library);
  bool load_native(const Module& module);
  void add_module(Module module, bool explicit_module,
                  std::optional<std::filesystem::path> source);

  struct State;
  std::unique_ptr<State> state_;
  friend struct detail::CompilerAccess;
};

}  // namespace joggle
