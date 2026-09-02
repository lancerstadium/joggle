#pragma once

#include <array>
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
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/graph.h"
#include "joggle/module.h"
#include "joggle/type.h"

namespace joggle {

namespace detail {

struct CompilerAccess;

template <typename> struct OptionalValue {
  static constexpr bool value = false;
  using type = void;
};

template <typename T> struct OptionalValue<std::optional<T>> {
  static constexpr bool value = true;
  using type = T;
};

template <bool WithDiagnostics, typename Callable> struct QueryResult;

template <typename Callable> struct QueryResult<true, Callable> {
  using type = std::invoke_result_t<Callable&, const Graph&, Diagnostics&>;
};

template <typename Callable> struct QueryResult<false, Callable> {
  using type = std::invoke_result_t<Callable&, const Graph&>;
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
        "typed interface binding disagrees with the declared argument kinds");
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
  using PassFunction = std::function<bool(Compiler&, Graph&, Diagnostics&)>;

public:
  Compiler();
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

  std::optional<Module> module(std::string_view name) const;
  std::vector<Module> modules() const;
  bool load_behavior(std::string_view module,
                     const std::filesystem::path& library);
  bool load_behavior(std::string_view module);

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
  std::optional<Graph> graph();
  std::optional<Graph> graph(Module::Symbol symbol);
  std::optional<Graph> graph(std::string_view name);

  bool conforms(const Module::TypeDecl& declaration,
                const Module::InterfaceDecl& interface) const;
  bool conforms(const Module::AttributeDecl& declaration,
                const Module::InterfaceDecl& interface) const;
  bool conforms(const Module::OperationDecl& declaration,
                const Module::InterfaceDecl& interface) const;

  template <typename Result, typename... Arguments, typename Function>
  void bind(Module::TypeDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_typed_method<Type, Result, Arguments...>(
        std::move(declaration), std::move(method),
        std::forward<Function>(function));
  }

  template <typename Result, typename... Arguments, typename Function>
  void bind(Module::AttributeDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_typed_method<Attribute, Result, Arguments...>(
        std::move(declaration), std::move(method),
        std::forward<Function>(function));
  }

  template <typename Result, typename... Arguments, typename Function>
  void bind(Module::OperationDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_typed_method<Operation, Result, Arguments...>(
        std::move(declaration), std::move(method),
        std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::TypeDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_inferred<Type>(std::move(declaration), std::move(method),
                        std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::AttributeDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_inferred<Attribute>(std::move(declaration), std::move(method),
                             std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::OperationDecl declaration,
            Module::InterfaceDecl::MethodDecl method, Function&& function) {
    bind_inferred<Operation>(std::move(declaration), std::move(method),
                             std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::TypeDecl declaration, std::string_view method,
            Function&& function) {
    const auto member = lookup_method(declaration, method);
    if (member) {
      bind(std::move(declaration), *member,
           std::forward<Function>(function));
    }
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
  void bind(Module::OperationDecl declaration, std::string_view method,
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
    static_assert(std::is_same_v<Subject, Type> ||
                      std::is_same_v<Subject, Attribute> ||
                      std::is_same_v<Subject, Operation>,
                  "an interface call subject must be a Type, Attribute, or "
                  "Operation");
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
  void bind(Module::OperationDecl schema, Function&& function) {
    bind_typed_verifier<Operation>(std::move(schema),
                                   std::forward<Function>(function));
  }

  template <typename Function>
  void bind(Module::PassDecl schema, Function&& function) {
    using Callable = std::decay_t<Function>;
    PassFunction pass = [callable = Callable(std::forward<Function>(function))](
                            Compiler& compiler, Graph& graph,
                            Diagnostics& diagnostics) mutable {
      if constexpr (std::is_invocable_r_v<bool, Callable&, Compiler&, Graph&,
                                          Diagnostics&>) {
        return std::invoke(callable, compiler, graph, diagnostics);
      } else if constexpr (std::is_invocable_r_v<bool, Callable&, Compiler&,
                                                 Graph&>) {
        return std::invoke(callable, compiler, graph);
      } else if constexpr (std::is_invocable_r_v<bool, Callable&, Graph&,
                                                 Diagnostics&>) {
        return std::invoke(callable, graph, diagnostics);
      } else if constexpr (
          std::is_invocable_r_v<bool, Callable&, Graph&>) {
        return std::invoke(callable, graph);
      } else {
        static_assert(
            std::is_invocable_r_v<bool, Callable&, Graph&>,
            "a pass binding must accept Graph&, optionally preceded by "
            "Compiler& or followed by Diagnostics&, and return bool");
        return false;
      }
    };
    bind_pass(std::move(schema), std::move(pass));
  }

  template <typename Function>
  auto query(const Graph& graph, Function&& function) {
    using Callable = std::decay_t<Function>;
    constexpr bool with_diagnostics =
        std::is_invocable_v<Callable&, const Graph&, Diagnostics&>;
    static_assert(with_diagnostics ||
                      std::is_invocable_v<Callable&, const Graph&>,
                  "a query must accept const Graph&, with an optional "
                  "Diagnostics& last");
    using Produced =
        typename detail::QueryResult<with_diagnostics, Callable>::type;
    using Result = std::conditional_t<
        detail::OptionalValue<std::remove_cvref_t<Produced>>::value,
        typename detail::OptionalValue<std::remove_cvref_t<Produced>>::type,
        std::remove_cvref_t<Produced>>;
    if (!prepare_query(graph)) {
      return std::optional<Result>{};
    }
    Diagnostics& diagnostics = query_diagnostics();
    const std::size_t before = diagnostics.size();
    Callable callable(std::forward<Function>(function));
    const auto invoke_query = [&]() -> Produced {
      if constexpr (with_diagnostics) {
        return std::invoke(callable, graph, diagnostics);
      } else {
        return std::invoke(callable, graph);
      }
    };
    if constexpr (detail::OptionalValue<std::remove_cvref_t<Produced>>::value) {
      auto result = invoke_query();
      if (!result && diagnostics.size() == before) {
        diagnostics.report("query did not produce a result");
      }
      return diagnostics.size() == before ? std::move(result)
                                          : std::optional<Result>{};
    } else {
      Result result = invoke_query();
      return diagnostics.size() == before
                 ? std::optional<Result>{std::move(result)}
                 : std::optional<Result>{};
    }
  }

  bool verify(const Graph& graph);
  bool run(Graph& graph, Module::PassDecl pass);
  bool run(Graph& graph, std::string_view pass);
  const Diagnostics& diagnostics() const;

private:
  std::optional<Type> make(const Module::TypeDecl& schema,
                           std::span<const detail::ParameterValue> parameters);
  std::optional<Attribute>
  make(const Module::AttributeDecl& schema,
       std::span<const detail::ParameterValue> parameters);
  void bind_method(Module::TypeDecl declaration,
                   Module::InterfaceDecl::MethodDecl method,
                   MethodFunction<Type> function);
  void bind_method(Module::AttributeDecl declaration,
                   Module::InterfaceDecl::MethodDecl method,
                   MethodFunction<Attribute> function);
  void bind_method(Module::OperationDecl declaration,
                   Module::InterfaceDecl::MethodDecl method,
                   MethodFunction<Operation> function);
  void bind_verifier(Module::TypeDecl schema, VerifierFunction<Type> verifier);
  void bind_verifier(Module::AttributeDecl schema,
                     VerifierFunction<Attribute> verifier);
  void bind_verifier(Module::OperationDecl schema,
                     VerifierFunction<Operation> verifier);
  void bind_pass(Module::PassDecl schema, PassFunction function);
  std::optional<detail::ParameterValue>
  call(const Type& subject, Module::InterfaceDecl::MethodDecl method,
       std::span<const detail::ParameterValue> parameters);
  std::optional<detail::ParameterValue>
  call(const Attribute& subject, Module::InterfaceDecl::MethodDecl method,
       std::span<const detail::ParameterValue> parameters);
  std::optional<detail::ParameterValue>
  call(const Operation& subject, Module::InterfaceDecl::MethodDecl method,
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
  bool prepare_query(const Graph& graph);
  Diagnostics& query_diagnostics();
  std::optional<Module> lookup_module(const Module& module);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(Module::TypeDecl declaration, std::string_view reference);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(Module::AttributeDecl declaration, std::string_view reference);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(Module::OperationDecl declaration, std::string_view reference);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(const Module& module, std::string_view reference,
                Module::SymbolKind subject);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(const Module& module, std::string_view declaration,
                std::span<const std::string> interfaces,
                std::string_view reference, Module::SymbolKind subject);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(const Type& subject, std::string_view reference);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(const Attribute& subject, std::string_view reference);
  std::optional<Module::InterfaceDecl::MethodDecl>
  lookup_method(const Operation& subject, std::string_view reference);
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
