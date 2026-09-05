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

#include "joggle/diag.h"
#include "joggle/ir.h"
#include "joggle/mod.h"
#include "joggle/type.h"

namespace joggle {

class Compiler;

// Controls whether a native implementation may be evaluated while the
// current source path is guarded by Residual control. Hermetic is an explicit
// promise that evaluation is deterministic and has no observable host effect.
enum class HostEval { Guarded, Hermetic };

namespace detail {

struct CompilerAccess;

struct HostVal {
  std::string cpp_type;
  std::shared_ptr<void> storage;
  std::optional<Type> concrete_type;
};

using IntegerList = std::vector<std::int64_t>;
using RealList = std::vector<double>;
using BooleanList = std::vector<bool>;
using StringList = std::vector<std::string>;
using TypeList = std::vector<Type>;
using ExecVal =
    std::variant<std::int8_t, std::uint8_t, std::int16_t, std::uint16_t,
                 std::int32_t, std::uint32_t, std::int64_t, std::uint64_t,
                 float, double, bool, std::string, Type, Bytes,
                 std::shared_ptr<Fn>, IntegerList, RealList, BooleanList,
                 StringList, TypeList, HostVal>;
using ExecVals = std::vector<ExecVal>;

template <typename T>
inline constexpr bool is_builtin_host_value =
    std::is_same_v<std::remove_cvref_t<T>, std::int8_t> ||
    std::is_same_v<std::remove_cvref_t<T>, std::uint8_t> ||
    std::is_same_v<std::remove_cvref_t<T>, std::int16_t> ||
    std::is_same_v<std::remove_cvref_t<T>, std::uint16_t> ||
    std::is_same_v<std::remove_cvref_t<T>, std::int32_t> ||
    std::is_same_v<std::remove_cvref_t<T>, std::uint32_t> ||
    std::is_same_v<std::remove_cvref_t<T>, std::int64_t> ||
    std::is_same_v<std::remove_cvref_t<T>, std::uint64_t> ||
    std::is_same_v<std::remove_cvref_t<T>, float> ||
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

inline bool has_domain(const Mod::ParamDecl& field, std::string_view domain) {
  return field.domain.kind == Mod::Expr::Kind::Reference &&
         field.domain.text == domain && field.domain.arguments.empty();
}

template <typename T> ExecVal store_exec_val(T&& value) {
  using Stored = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Stored, Fn>) {
    return {std::make_shared<Fn>(std::forward<T>(value))};
  } else if constexpr (is_builtin_host_value<Stored>) {
    return {Stored(std::forward<T>(value))};
  } else {
    return {HostVal{std::string(host_type_name<Stored>()),
                    std::make_shared<Stored>(std::forward<T>(value)),
                    std::nullopt}};
  }
}

template <typename T> decltype(auto) execution_argument(ExecVal& value) {
  using Stored = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Stored, Fn>) {
    auto& fn = *std::get<std::shared_ptr<Fn>>(value);
    if constexpr (std::is_reference_v<T>) {
      return static_cast<T>(fn);
    } else {
      return fn;
    }
  } else if constexpr (is_builtin_host_value<Stored>) {
    auto& stored = std::get<Stored>(value);
    return static_cast<T>(stored);
  } else {
    auto& host = std::get<HostVal>(value);
    if (host.cpp_type != host_type_name<Stored>() || !host.storage) {
      throw std::bad_variant_access{};
    }
    auto& stored = *static_cast<Stored*>(host.storage.get());
    return static_cast<T>(stored);
  }
}

template <typename T> std::optional<T> take_exec_val(ExecVal value) {
  using Stored = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Stored, Fn>) {
    auto fn = std::get<std::shared_ptr<Fn>>(std::move(value));
    return fn ? std::optional<T>{std::move(*fn)} : std::nullopt;
  } else if constexpr (is_builtin_host_value<Stored>) {
    return std::get<Stored>(std::move(value));
  } else {
    auto host = std::get<HostVal>(std::move(value));
    if (host.cpp_type != host_type_name<Stored>() || !host.storage) {
      return std::nullopt;
    }
    return std::move(*static_cast<Stored*>(host.storage.get()));
  }
}

template <typename> struct IsTuple : std::false_type {};

template <typename... Vals>
struct IsTuple<std::tuple<Vals...>> : std::true_type {};

template <typename T>
inline constexpr bool is_tuple = IsTuple<std::remove_cvref_t<T>>::value;

template <typename T> std::vector<std::string_view> execution_result_types() {
  using Result = std::remove_cvref_t<T>;
  if constexpr (std::is_void_v<Result>) {
    return {};
  } else if constexpr (is_tuple<Result>) {
    static_assert(std::tuple_size_v<Result> >= 2U,
                  "use T for one fn result and void for no results");
    return []<std::size_t... Indices>(std::index_sequence<Indices...>) {
      return std::vector<std::string_view>{
          host_type_name<std::tuple_element_t<Indices, Result>>()...};
    }(std::make_index_sequence<std::tuple_size_v<Result>>{});
  } else {
    return {host_type_name<Result>()};
  }
}

template <typename T> ExecVals store_exec_vals(T&& value) {
  using Stored = std::remove_cvref_t<T>;
  if constexpr (is_tuple<Stored>) {
    ExecVals result;
    result.reserve(std::tuple_size_v<Stored>);
    std::apply(
        [&](auto&&... elements) {
          (result.push_back(
               store_exec_val(std::forward<decltype(elements)>(elements))),
           ...);
        },
        std::forward<T>(value));
    return result;
  } else {
    ExecVals result;
    result.push_back(store_exec_val(std::forward<T>(value)));
    return result;
  }
}

template <typename Tuple, std::size_t... Indices>
std::optional<Tuple> take_execution_tuple(ExecVals values,
                                          std::index_sequence<Indices...>) {
  if (values.size() != sizeof...(Indices)) {
    return std::nullopt;
  }
  std::tuple<std::optional<std::tuple_element_t<Indices, Tuple>>...> decoded{
      take_exec_val<std::tuple_element_t<Indices, Tuple>>(
          std::move(values[Indices]))...};
  if (!(std::get<Indices>(decoded).has_value() && ...)) {
    return std::nullopt;
  }
  return Tuple{std::move(*std::get<Indices>(decoded))...};
}

template <typename T> std::optional<T> take_exec_vals(ExecVals values) {
  using Result = std::remove_cvref_t<T>;
  if constexpr (is_tuple<Result>) {
    return take_execution_tuple<Result>(
        std::move(values),
        std::make_index_sequence<std::tuple_size_v<Result>>{});
  } else {
    return values.size() == 1U ? take_exec_val<T>(std::move(values.front()))
                               : std::nullopt;
  }
}

template <typename> struct OptionalVal {
  static constexpr bool value = false;
  using type = void;
};

template <typename T> struct OptionalVal<std::optional<T>> {
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
inline constexpr bool valid_fn_input =
    !std::is_reference_v<T> || (std::is_lvalue_reference_v<T> &&
                                std::is_const_v<std::remove_reference_t<T>>);

template <typename T>
inline constexpr bool valid_fn_result = !std::is_reference_v<T>;

// The C++ spelling of an ordinary fn implementation. Compiler& and
// Diag& are host services rather than declared arguments, so the same
// traits drive both overload selection and type-erased invocation.
template <typename Implementation> struct FnBinding {
  using Callable = std::decay_t<Implementation>;
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
      return std::is_same_v<std::tuple_element_t<arity - 1U, Arguments>, Diag&>;
    }
  }();
  static constexpr std::size_t argument_count =
      arity - static_cast<std::size_t>(with_compiler) -
      static_cast<std::size_t>(with_diagnostics);
  static constexpr std::size_t offset = with_compiler ? 1U : 0U;
  static_assert(valid_fn_result<typename Traits::result>,
                "fn results must be values");
  using Produced = std::remove_cvref_t<typename Traits::result>;
  using Result =
      std::conditional_t<OptionalVal<Produced>::value,
                         typename OptionalVal<Produced>::type, Produced>;

  static auto input_types() {
    return []<std::size_t... Indices>(std::index_sequence<Indices...>) {
      static_assert(
          (valid_fn_input<std::tuple_element_t<offset + Indices, Arguments>> &&
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

template <typename Callable, typename Arguments, std::size_t Offset,
          bool WithCompiler, bool WithDiag, std::size_t... Indices>
std::optional<ExecVals> invoke_typed_fn(Callable& callable, Compiler& compiler,
                                        std::span<ExecVal> arguments,
                                        Diag& diagnostics,
                                        std::index_sequence<Indices...>) {
  using Traits = CallableTraits<Callable>;
  using Produced = typename Traits::result;
  const auto invoke = [&]() -> Produced {
    if constexpr (WithCompiler && WithDiag) {
      return std::invoke(
          callable, compiler,
          execution_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...,
          diagnostics);
    } else if constexpr (WithCompiler) {
      return std::invoke(
          callable, compiler,
          execution_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...);
    } else if constexpr (WithDiag) {
      return std::invoke(
          callable,
          execution_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...,
          diagnostics);
    } else {
      return std::invoke(
          callable,
          execution_argument<std::tuple_element_t<Offset + Indices, Arguments>>(
              arguments[Indices])...);
    }
  };

  if constexpr (std::is_void_v<Produced>) {
    invoke();
    return ExecVals{};
  } else {
    auto produced = invoke();
    using Stored = std::remove_cvref_t<Produced>;
    if constexpr (OptionalVal<Stored>::value) {
      if (!produced) {
        return std::nullopt;
      }
      return store_exec_vals(std::move(*produced));
    } else {
      return store_exec_vals(std::move(produced));
    }
  }
}

}  // namespace detail

class Compiler {
private:
  template <typename Subject>
  using VerifierFn = std::function<bool(const Subject&, Diag&)>;
  using NativeFn = std::function<std::optional<detail::ExecVals>(
      Compiler&, std::span<detail::ExecVal>, Diag&)>;
  using RepresentationProjector = std::function<std::optional<Type>(
      Compiler&, const Mod::TypeDecl&, const void*)>;

public:
  struct Limits {
    std::size_t steps = 100000;
    std::size_t depth = 256;
  };

  Compiler();
  explicit Compiler(Limits limits);
  ~Compiler();
  Compiler(Compiler&&) noexcept;
  Compiler& operator=(Compiler&&) noexcept;
  Compiler(const Compiler&) = delete;
  Compiler& operator=(const Compiler&) = delete;

  // Adds a mod without resolving its imports. Errors accumulate in this
  // compiler and can be inspected after loading a complete mod set.
  void add(std::string_view text, std::string source = "<memory>");
  void add(Mod mod);
  void load(const std::filesystem::path& path);
  void search(std::filesystem::path root);
  void lock(const std::filesystem::path& path);

  // Checks the complete mod closure and seals it on success.
  bool link();
  bool ok() const;
  bool linked() const;
  Limits evaluation_limits() const;

  std::optional<Mod> mod(std::string_view name) const;
  std::vector<Mod> mods() const;
  // Resolves one uniquely named callable member after linking. The name must
  // be qualified as mod.fn; overloads remain explicit because this
  // lookup has no call arguments from which to infer a selection.
  std::optional<Mod::FnDecl> lookup(std::string_view qualified);
  bool load_native(std::string_view mod, const std::filesystem::path& library);
  bool load_native(std::string_view mod);

  // Associates an ordinary C++ value type with a Mod-declared type for
  // compiler-fn invocation. The Mod remains the schema authority.
  template <typename T> bool represent(Mod::TypeDecl schema) {
    using Host = std::remove_cvref_t<T>;
    static_assert(std::is_copy_constructible_v<Host>,
                  "a host representation must be copy constructible");
    return bind_representation(std::move(schema),
                               detail::host_type_name<Host>());
  }

  // A parameterized host representation projects a C++ value to the ordered
  // parameters of its Mod Type declaration. A std::tuple keeps the Mod
  // as the schema authority without requiring a wrapper or generated class.
  template <typename T, typename Projection>
  bool represent(Mod::TypeDecl schema, Projection&& projection) {
    using Host = std::remove_cvref_t<T>;
    using Callable = std::decay_t<Projection>;
    using Parameters =
        std::remove_cvref_t<std::invoke_result_t<Callable&, const Host&>>;
    static_assert(std::is_copy_constructible_v<Host>,
                  "a host representation must be copy constructible");
    static_assert(
        requires { std::tuple_size<Parameters>::value; },
        "a host type projection must return a std::tuple");
    RepresentationProjector erased =
        [callable = Callable(std::forward<Projection>(projection))](
            Compiler& compiler, const Mod::TypeDecl& declaration,
            const void* storage) mutable -> std::optional<Type> {
      if (storage == nullptr) {
        return std::nullopt;
      }
      auto parameters =
          std::invoke(callable, *static_cast<const Host*>(storage));
      return std::apply(
          [&](auto&&... values) {
            return compiler.make(declaration,
                                 std::forward<decltype(values)>(values)...);
          },
          std::move(parameters));
    };
    return bind_representation(
        std::move(schema), detail::host_type_name<Host>(), std::move(erased));
  }

  template <typename... Arguments>
  std::optional<Type> make(const Mod::TypeDecl& schema,
                           Arguments&&... arguments) {
    std::vector<detail::ParamVal> values;
    values.reserve(sizeof...(Arguments));
    (values.push_back(
         detail::encode_parameter(std::forward<Arguments>(arguments))),
     ...);
    return make(schema, std::span<const detail::ParamVal>(values));
  }

  // Constructs an ambient, parameterless Prelude type such as int, i32, f32,
  // or index. Parameterized Prelude types use the declaration overload above.
  std::optional<Type> make(std::string_view prelude_type);

  template <typename T> std::optional<Val> known(Type type, T&& value) {
    return make_known(std::move(type),
                      detail::encode_parameter(std::forward<T>(value)));
  }

  // Creates an empty executable body in this linked compilation.
  std::optional<Fn> create_fn();

  // Materializes every source-defined Fn whose results are
  // IR-representable and whose compiler inputs and generics have a complete
  // default specialization. Other declarations stay declarations in the
  // returned copy; the linked Mod is unchanged.
  std::optional<Mod> materialize(const Mod& mod);

  // Specializes a source-defined Fn and materializes its residual body.
  // The declaration, symbol, or qualified name selects the same Mod member;
  // known_arguments bind compile-time parameters before residualization.
  std::optional<Fn> materialize(Mod::FnDecl declaration);
  std::optional<Fn> materialize(Mod::FnDecl declaration,
                                std::vector<Val> known_arguments);
  std::optional<Fn> materialize(Mod::Symbol symbol);
  std::optional<Fn> materialize(Mod::Symbol symbol,
                                std::vector<Val> known_arguments);
  std::optional<Fn> materialize(std::string_view name);
  std::optional<Fn> materialize(std::string_view name,
                                std::vector<Val> known_arguments);
  // Specializes a source-defined callee from one already typed call. Concrete
  // operand, property, and result types recover the call's generic bindings.
  std::optional<Fn> materialize(const Op& call);
  std::optional<Fn> materialize(const Op& call, Diag& diagnostics);

  // Recursively specializes source-defined calls until every remaining call
  // is accepted by boundary. The returned Mod owns each concrete
  // specialization and the input Mod is not modified. An unaccepted
  // external call, recursive source expansion, or invalid rewrite fails the
  // whole operation.
  std::optional<Mod>
  specialize(const Mod& mod,
             const std::function<bool(const Mod::FnDecl&)>& boundary,
             Diag& diagnostics);

  template <typename Callable>
  void verify(Mod::TypeDecl schema, Callable&& callable) {
    bind_typed_verifier<Type>(std::move(schema),
                              std::forward<Callable>(callable));
  }

  template <typename Callable>
  void verify(Mod::FnDecl schema, Callable&& callable) {
    bind_typed_verifier<Op>(std::move(schema),
                            std::forward<Callable>(callable));
  }

  // Binds an implementation whose C++ input and result types match the
  // declared fn. Semantic validators use verify() instead.
  // A Mod and local name are sufficient for normal package code. The
  // callable's C++ signature selects an overload without a generated wrapper.
  template <typename Callable>
  void bind(const Mod& mod, std::string_view name, Callable&& callable,
            HostEval evaluation = HostEval::Guarded) {
    using Binding = detail::FnBinding<Callable>;
    const auto inputs = Binding::input_types();
    const auto results = Binding::result_types();
    const auto declaration = lookup_binding(mod, name, inputs, results);
    if (declaration) {
      bind(*declaration, std::forward<Callable>(callable), evaluation);
    }
  }

  // A declaration handle remains useful when an implementation already
  // reflects members for rewriting or when exact identity is intentional.
  template <typename Implementation>
  void bind(Mod::FnDecl schema, Implementation&& implementation,
            HostEval evaluation = HostEval::Guarded) {
    using Binding = detail::FnBinding<Implementation>;
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
    NativeFn binding =
        [callable = Callable(std::forward<Implementation>(implementation))](
            Compiler& compiler, std::span<detail::ExecVal> arguments,
            Diag& diagnostics) mutable {
          if (arguments.size() != argument_count) {
            diagnostics.report("fn binding received the wrong argument "
                               "count");
            return std::optional<detail::ExecVals>{};
          }
          return detail::invoke_typed_fn<Callable, Arguments, offset,
                                         with_compiler, with_diagnostics>(
              callable, compiler, arguments, diagnostics,
              std::make_index_sequence<argument_count>{});
        };
    bind_native(std::move(schema), std::move(binding), evaluation);
  }

  bool verify(const Fn& fn);
  bool verify(const Mod& mod);

  template <typename Result = void, typename... Arguments>
  bool invocable(const Mod::FnDecl& fn) const {
    const std::array<std::string_view, sizeof...(Arguments)> inputs{
        detail::host_type_name<Arguments>()...};
    return matches_run_signature(fn, inputs,
                                 detail::execution_result_types<Result>());
  }

  template <typename Result = void, typename... Arguments>
  std::conditional_t<std::is_void_v<Result>, bool, std::optional<Result>>
  run(Mod::FnDecl fn, Arguments&&... arguments) {
    const std::array<std::string_view, sizeof...(Arguments)> types{
        detail::host_type_name<Arguments>()...};
    const auto result_types = detail::execution_result_types<Result>();
    if (!check_run_signature(fn, types, result_types)) {
      if constexpr (std::is_void_v<Result>) {
        return false;
      } else {
        return std::nullopt;
      }
    }
    std::vector<detail::ExecVal> values;
    values.reserve(sizeof...(Arguments));
    (values.push_back(
         detail::store_exec_val(std::forward<Arguments>(arguments))),
     ...);
    auto results = execute(std::move(fn), std::move(values));
    if constexpr (std::is_void_v<Result>) {
      return results.has_value();
    } else {
      return results ? detail::take_exec_vals<Result>(std::move(*results))
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
  const Diag& diag() const;

private:
  std::optional<Val> make_known(Type type, detail::ParamVal value);
  std::optional<Type> make(const Mod::TypeDecl& schema,
                           std::span<const detail::ParamVal> parameters);
  void bind_verifier(Mod::TypeDecl schema, VerifierFn<Type> verifier);
  void bind_verifier(Mod::FnDecl schema, VerifierFn<Op> verifier);
  bool bind_representation(Mod::TypeDecl schema, std::string_view type);
  bool bind_representation(Mod::TypeDecl schema, std::string_view type,
                           RepresentationProjector projector);
  bool project_host_value(detail::ExecVal& value);
  bool check_execution_values(const Mod::FnDecl& fn,
                              std::span<const detail::ExecVal> arguments,
                              std::span<const detail::ExecVal> results = {});
  bool accepts_host_type(const Mod::FnDecl& fn, const Mod::ParamDecl& field,
                         std::string_view type) const;
  void bind_native(Mod::FnDecl schema, NativeFn fn, HostEval evaluation);
  void bind_prelude_mod();
  void bind_prelude_primitives();
  bool check_binding_signature(const Mod::FnDecl& schema,
                               std::span<const std::string_view> inputs,
                               std::span<const std::string_view> results);
  std::optional<Mod::FnDecl>
  lookup_binding(const Mod& mod, std::string_view name,
                 std::span<const std::string_view> inputs,
                 std::span<const std::string_view> results);
  std::optional<Mod::FnDecl>
  resolve_host_overload(const Mod& mod, std::string_view name,
                        std::span<const std::string_view> inputs,
                        std::span<const std::string_view> results,
                        std::string_view purpose);
  std::optional<Mod::FnDecl>
  lookup_run(std::string_view name, std::span<const std::string_view> inputs,
             std::span<const std::string_view> results);
  bool check_run_signature(const Mod::FnDecl& schema,
                           std::span<const std::string_view> inputs,
                           std::span<const std::string_view> results);
  bool matches_run_signature(const Mod::FnDecl& schema,
                             std::span<const std::string_view> inputs,
                             std::span<const std::string_view> results) const;
  std::optional<detail::ExecVals>
  execute(Mod::FnDecl declaration, std::vector<detail::ExecVal> arguments,
          bool under_residual_control = false);
  std::optional<detail::ParamVal>
  evaluate_binding(Mod::FnDecl fn, std::span<const detail::ParamVal> arguments,
                   bool under_residual_control);
  bool can_evaluate_binding(const Mod::FnDecl& fn,
                            bool under_residual_control) const;
  template <typename Subject, typename Declaration, typename Implementation>
  void bind_typed_verifier(Declaration declaration,
                           Implementation&& implementation) {
    using Callable = std::decay_t<Implementation>;
    VerifierFn<Subject> verifier =
        [callable = Callable(std::forward<Implementation>(implementation))](
            const Subject& value, Diag& diagnostics) mutable {
          if constexpr (std::is_invocable_r_v<bool, Callable&, const Subject&,
                                              Diag&>) {
            return std::invoke(callable, value, diagnostics);
          } else if constexpr (std::is_invocable_r_v<bool, Callable&,
                                                     const Subject&>) {
            return std::invoke(callable, value);
          } else {
            static_assert(
                std::is_invocable_r_v<bool, Callable&, const Subject&>,
                "a verifier binding must accept its subject, with an optional "
                "Diag& last, and return bool");
            return false;
          }
        };
    bind_verifier(std::move(declaration), std::move(verifier));
  }

  std::optional<Mod> lookup_mod(const Mod& mod);
  bool load_native(const Mod& mod, const std::filesystem::path& library);
  bool load_native(const Mod& mod);
  void add_mod(Mod mod, bool explicit_mod,
               std::optional<std::filesystem::path> source);

  struct State;
  std::unique_ptr<State> state_;
  friend struct detail::CompilerAccess;
};

}  // namespace joggle
