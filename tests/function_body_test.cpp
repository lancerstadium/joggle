#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

constexpr std::string_view source = R"(
joggle 1;

module logic@1.0.0 {
  type word(width: int);
  type tensor(element: type, shape: list<int>);
  type payload(scale: real, labels: list<string>, element: type);

  fn source<T>(name: string, meta: type) -> T;
  fn identity<T>(input: T) -> T;
  fn generic_body<T>(input: T) -> T {
    return identity(input);
  }
  fn generic_body_user(input: word<8>) -> word<8> {
    return generic_body(input);
  }
  fn add<T>(lhs: T, rhs: T) -> T;
  fn apply<T, U>(input: T, body: (T) -> U) -> U;
  fn apply_same<T>(input: T, body: (T) -> T) -> T;
  fn callback_factory<T, U>() -> (T) -> U;
  fn main(lhs: tensor<word<8>, [2, 4]>) -> tensor<word<8>, [2, 4]> {
    input: tensor<word<8>, [2, 4]> = source(
      name = "input } // still a string",
      meta = payload<1.5, ["alpha", "beta"], word<8>>
    );
    sum = add(lhs, input);
    return sum;
  }

  fn configured<N: int>(scale: N, input: word<N>) -> word<N> {
    result = identity(input);
    return result;
  }
  fn default_configured<N: int>(scale: N = 8, input: word<N>) -> word<N> {
    result = identity(input);
    return result;
  }
  fn callback_user(input: word<8>, body: (word<8>) -> word<16>)
      -> word<16> {
    return apply(input, body);
  }
  fn callback(input: word<8>) -> word<16>;
  fn apply_fixed(input: word<8>, body: (word<8>) -> word<16>) -> word<16>;
  fn inline_callback(input: word<8>) -> word<16> {
    return apply_fixed(input, (value: word<8>) => callback(value));
  }
  fn generic_inline_callback(input: word<8>) -> word<16> {
    return apply(input, (value: word<8>) => callback(value));
  }
  fn choose(input: word<8>, body: (word<8>) -> word<16>) -> word<16>;
  fn choose(input: word<8>, body: (word<16>) -> word<16>) -> word<16>;
  fn overloaded_inline_callback(input: word<8>) -> word<16> {
    return choose(input, (value: word<8>) => callback(value));
  }
  fn callback_value(input: word<8>) -> word<16> {
    return apply(input, callback);
  }
  fn generic_callback<T>(input: T) -> T;
  fn generic_callback_value(input: word<8>) -> word<8> {
    body: (word<8>) -> word<8> = generic_callback;
    return apply_same(input, body);
  }
  fn direct_generic_callback_value(input: word<8>) -> word<8> {
    return apply_same(input, generic_callback);
  }
  fn overloaded_callback(input: word<8>) -> word<8>;
  fn overloaded_callback(input: word<16>) -> word<16>;
  fn overloaded_callback_value(input: word<8>) -> word<8> {
    body: (word<8>) -> word<8> = overloaded_callback;
    return apply_same(input, body);
  }
  fn direct_overloaded_callback_value(input: word<8>) -> word<8> {
    return apply_same(input, overloaded_callback);
  }

}
)";

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

std::optional<joggle::Function> load_function(joggle::Compiler& compiler,
                                              std::string_view text) {
  compiler.add(text, "logic.joggle");
  if (!compiler.link()) {
    return std::nullopt;
  }
  return compiler.materialize("logic.main");
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  const auto function = load_function(compiler, source);
  if (!function) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto module = compiler.module("logic");
  const auto operations = function->ops();
  bool ok = true;
  ok &= expect(module.has_value(), "the function owner remains available");
  const auto configured_decl =
      module ? module->function("configured") : std::nullopt;
  const auto default_configured_decl =
      module ? module->function("default_configured") : std::nullopt;
  const auto configured_int = compiler.make("int");
  const auto scale = configured_int
                         ? compiler.known(*configured_int, std::int64_t{8})
                         : std::nullopt;
  const auto configured = scale && configured_decl
                              ? compiler.materialize(*configured_decl, {*scale})
                              : std::nullopt;
  const auto default_configured =
      default_configured_decl ? compiler.materialize(*default_configured_decl)
                              : std::nullopt;
  const auto callback_user = compiler.materialize("logic.callback_user");
  const auto inline_callback = compiler.materialize("logic.inline_callback");
  const auto generic_inline_callback =
      compiler.materialize("logic.generic_inline_callback");
  const auto overloaded_inline_callback =
      compiler.materialize("logic.overloaded_inline_callback");
  const auto generic_body_user =
      compiler.materialize("logic.generic_body_user");
  const auto generic_body_call =
      generic_body_user && generic_body_user->ops().size() == 1U
          ? std::optional<joggle::Op>{generic_body_user->ops().front()}
          : std::nullopt;
  const auto generic_body = generic_body_call
                                ? compiler.materialize(*generic_body_call)
                                : std::nullopt;
  const auto callback_value = compiler.materialize("logic.callback_value");
  const auto generic_callback_value =
      compiler.materialize("logic.generic_callback_value");
  const auto overloaded_callback_value =
      compiler.materialize("logic.overloaded_callback_value");
  const auto direct_generic_callback_value =
      compiler.materialize("logic.direct_generic_callback_value");
  const auto direct_overloaded_callback_value =
      compiler.materialize("logic.direct_overloaded_callback_value");
  const auto callback_arguments =
      callback_user ? callback_user->arguments() : std::vector<joggle::Value>{};
  const auto callback_parameters =
      callback_arguments.size() == 2U
          ? callback_arguments[1].type().get<std::vector<joggle::Type>>(
                "inputs")
          : std::optional<std::vector<joggle::Type>>{};
  const auto callback_results =
      callback_arguments.size() == 2U
          ? callback_arguments[1].type().get<std::vector<joggle::Type>>(
                "results")
          : std::optional<std::vector<joggle::Type>>{};
  ok &= expect(
      configured_decl && configured && default_configured &&
          configured_decl->inputs().size() == 2U &&
          configured_decl->inputs().front().name == "scale" &&
          configured->arguments().size() == 1U &&
          configured->arguments().front().type().get<std::int64_t>("width") ==
              std::optional<std::int64_t>{8} &&
          configured->result_types().front().get<std::int64_t>("width") ==
              std::optional<std::int64_t>{8} &&
          default_configured->arguments().front().type().get<std::int64_t>(
              "width") == std::optional<std::int64_t>{8} &&
          default_configured->result_types().front().get<std::int64_t>(
              "width") == std::optional<std::int64_t>{8},
      "a function signature has one public parameter sequence while "
      "Known specialization resolves its residual boundary");
  ok &= expect(callback_user && callback_arguments.size() == 2U &&
                   callback_arguments[1].type().schema().name() == "callable" &&
                   callback_parameters && callback_parameters->size() == 1U &&
                   callback_results && callback_results->size() == 1U &&
                   callback_parameters->front().get<std::int64_t>("width") ==
                       std::optional<std::int64_t>{8} &&
                   callback_results->front().get<std::int64_t>("width") ==
                       std::optional<std::int64_t>{16} &&
                   callback_user->ops().size() == 1U,
               "function type syntax constructs a reflected callable type "
               "and participates in generic call inference");
  const auto inline_ops =
      inline_callback ? inline_callback->ops() : std::vector<joggle::Op>{};
  const auto inline_arguments =
      inline_ops.size() == 1U ? inline_ops.front().arguments()
                              : std::vector<joggle::Value>{};
  const auto inline_body =
      inline_arguments.size() == 2U
          ? inline_arguments.back().inline_function()
          : std::optional<joggle::Function>{};
  ok &= expect(inline_callback && inline_ops.size() == 1U &&
                   inline_arguments.size() == 2U && inline_body &&
                   !inline_arguments.back().referenced_function() &&
                   inline_body->arguments().size() == 1U &&
                   inline_body->ops().size() == 1U &&
                   inline_body->ops().front().callee().name() == "callback",
               "a typed lambda materializes through the ordinary expression "
               "and callable Value path");
  ok &= expect(generic_inline_callback &&
                   generic_inline_callback->ops().size() == 1U &&
                   generic_inline_callback->ops()
                       .front()
                       .arguments()
                       .back()
                       .inline_function(),
               "lambda annotations and the surrounding result infer a "
               "generic higher-order call");
  const auto overloaded_inline_arguments =
      overloaded_inline_callback &&
              overloaded_inline_callback->ops().size() == 1U
          ? overloaded_inline_callback->ops().front().arguments()
          : std::vector<joggle::Value>{};
  const auto overloaded_inline_body =
      overloaded_inline_arguments.size() == 2U
          ? overloaded_inline_arguments.back().inline_function()
          : std::optional<joggle::Function>{};
  ok &= expect(overloaded_inline_callback && overloaded_inline_body &&
                   overloaded_inline_body->arguments().front().type().get<
                       std::int64_t>("width") ==
                       std::optional<std::int64_t>{8},
               "lambda parameter annotations select a higher-order overload");
  const std::string inline_text =
      inline_callback ? joggle::format(*inline_callback, "compiled_inline")
                      : std::string{};
  joggle::Compiler inline_roundtrip;
  inline_roundtrip.add(source, "logic.joggle");
  inline_roundtrip.add("joggle 1;\nmodule inline_artifact@1.0.0 {\n"
                       "  import logic@1;\n" +
                           inline_text + "}\n",
                       "inline-artifact.joggle");
  const bool inline_roundtrip_linked = inline_roundtrip.link();
  const auto replayed_inline =
      inline_roundtrip_linked
          ? inline_roundtrip.materialize("inline_artifact.compiled_inline")
          : std::nullopt;
  if (!replayed_inline) {
    inline_roundtrip.diagnostics().print(std::cerr);
  }
  ok &= expect(inline_text.find("=>") != std::string::npos &&
                   replayed_inline && inline_roundtrip.verify(*replayed_inline),
               "typed lambda formatting is canonical and materializes after "
               "a source round trip");

  joggle::Compiler invalid_lambda;
  invalid_lambda.add(R"(
joggle 1;
module invalid_lambda@1.0.0 {
  type word(width: int);
  fn apply(input: word<8>, body: (word<8>) -> word<16>) -> word<16>;
  fn callback(input: word<8>) -> word<16>;

  fn captures(input: word<8>) -> word<16> {
    return apply(input, (value: word<8>) => callback(input));
  }

  fn mismatched(input: word<8>) -> word<16> {
    return apply(input, (value: word<16>) => callback(value));
  }
}
)",
                     "invalid-lambda.joggle");
  const bool invalid_lambda_linked = invalid_lambda.link();
  const auto captured = invalid_lambda_linked
                            ? invalid_lambda.materialize(
                                  "invalid_lambda.captures")
                            : std::nullopt;
  const auto mismatched = invalid_lambda_linked
                              ? invalid_lambda.materialize(
                                    "invalid_lambda.mismatched")
                              : std::nullopt;
  const bool reports_capture = std::any_of(
      invalid_lambda.diagnostics().entries().begin(),
      invalid_lambda.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("undefined local value 'input'") !=
               std::string::npos;
      });
  const bool reports_mismatch = std::any_of(
      invalid_lambda.diagnostics().entries().begin(),
      invalid_lambda.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("inline function") !=
                   std::string::npos &&
               diagnostic.message.find("does not match") != std::string::npos;
      });
  if (captured || mismatched || !reports_capture || !reports_mismatch) {
    invalid_lambda.diagnostics().print(std::cerr);
  }
  ok &= expect(invalid_lambda_linked && !captured && !mismatched &&
                   reports_capture && reports_mismatch,
               "typed lambdas reject residual captures and mismatched "
               "parameter annotations");

  joggle::Compiler ambiguous_lambda;
  ambiguous_lambda.add(R"(
joggle 1;
module ambiguous_lambda@1.0.0 {
  type word(width: int);
  fn callback(input: word<8>) -> word<16>;
  fn choose(input: word<8>, body: (word<8>) -> word<16>) -> word<16>;
  fn choose(input: word<8>, body: (word<8>) -> word<8>) -> word<16>;

  fn use(input: word<8>) -> word<16> {
    return choose(input, (value: word<8>) => callback(value));
  }
}
)",
                       "ambiguous-lambda.joggle");
  const bool ambiguous_lambda_linked = ambiguous_lambda.link();
  const auto ambiguous_lambda_body =
      ambiguous_lambda_linked
          ? ambiguous_lambda.materialize("ambiguous_lambda.use")
          : std::nullopt;
  const bool reports_lambda_ambiguity = std::any_of(
      ambiguous_lambda.diagnostics().entries().begin(),
      ambiguous_lambda.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("ambiguous between") !=
               std::string::npos;
      });
  ok &= expect(!ambiguous_lambda_body && reports_lambda_ambiguity,
               "a lambda remains ambiguous when annotations and surrounding "
               "result context cannot select one overload");

  joggle::Diagnostics duplicate_lambda_diagnostics;
  const auto duplicate_lambda = joggle::parse_module(R"(
joggle 1;
module duplicate_lambda@1.0.0 {
  type word();
  fn apply(input: word, body: (word, word) -> word) -> word;
  fn invalid(input: word) -> word {
    return apply(input, (value: word, value: word) => value);
  }
}
)",
                                                        duplicate_lambda_diagnostics,
                                                        "duplicate-lambda.joggle");
  ok &= expect(!duplicate_lambda && !duplicate_lambda_diagnostics.ok(),
               "duplicate lambda parameter names are rejected while parsing");
  ok &= expect(generic_body_user && generic_body_call && generic_body &&
                   compiler.verify(*generic_body) &&
                   generic_body->arguments().size() == 1U &&
                   generic_body->result_types().size() == 1U &&
                   generic_body->arguments().front().type() ==
                       generic_body->result_types().front() &&
                   generic_body->arguments().front().type().get<std::int64_t>(
                       "width") == std::optional<std::int64_t>{8} &&
                   generic_body->ops().size() == 1U &&
                   generic_body->ops().front().callee().name() == "identity",
               "a concrete typed call recovers generic bindings and "
               "materializes its source-defined callee body");
  const auto callback_operations = callback_value
                                       ? callback_value->ops()
                                       : std::vector<joggle::Op>{};
  const auto applied_arguments = callback_operations.size() == 1U
                                     ? callback_operations.front().arguments()
                                     : std::vector<joggle::Value>{};
  const auto referenced = applied_arguments.size() == 2U
                              ? applied_arguments[1].referenced_function()
                              : std::optional<joggle::Module::FunctionDecl>{};
  ok &=
      expect(callback_value && callback_operations.size() == 1U && referenced &&
                 referenced->symbol().qualified_name() == "logic.callback" &&
                 applied_arguments[1].type().schema().name() == "callable",
             "a named function is a typed module value without a region "
             "or wrapper op");
  const std::string callback_text =
      callback_value ? joggle::format(*callback_value, "compiled_callback")
                     : "";
  joggle::Compiler callback_compiler;
  callback_compiler.add(source, "logic.joggle");
  callback_compiler.add("joggle 1;\nmodule callback_artifact@1.0.0 {\n"
                        "  import logic@1;\n" +
                            callback_text + "}\n",
                        "callback-artifact.joggle");
  const bool callback_linked = callback_compiler.link();
  const auto replayed_callback =
      callback_linked
          ? callback_compiler.materialize("callback_artifact.compiled_callback")
          : std::optional<joggle::Function>{};
  ok &= expect(callback_text.find("logic.callback") != std::string::npos &&
                   replayed_callback &&
                   joggle::format(*replayed_callback, "compiled_callback") ==
                       callback_text,
               "named function values format and instantiate canonically");
  const auto generic_arguments =
      generic_callback_value && !generic_callback_value->ops().empty()
          ? generic_callback_value->ops().front().arguments()
          : std::vector<joggle::Value>{};
  const auto overloaded_arguments =
      overloaded_callback_value &&
              !overloaded_callback_value->ops().empty()
          ? overloaded_callback_value->ops().front().arguments()
          : std::vector<joggle::Value>{};
  const auto generic_reference =
      generic_arguments.size() == 2U
          ? generic_arguments[1].referenced_function()
          : std::optional<joggle::Module::FunctionDecl>{};
  const auto overloaded_reference =
      overloaded_arguments.size() == 2U
          ? overloaded_arguments[1].referenced_function()
          : std::optional<joggle::Module::FunctionDecl>{};
  ok &= expect(generic_reference &&
                   generic_reference->name() == "generic_callback" &&
                   overloaded_reference &&
                   overloaded_reference->signature().find("word<8>") !=
                       std::string::npos,
               "a callable annotation resolves generic and overloaded "
               "function values contextually");
  const std::string generic_text =
      generic_callback_value
          ? joggle::format(*generic_callback_value, "generic_value")
          : "";
  const std::string overloaded_text =
      overloaded_callback_value
          ? joggle::format(*overloaded_callback_value, "overloaded_value")
          : "";
  joggle::Compiler contextual_compiler;
  contextual_compiler.add(source, "logic.joggle");
  contextual_compiler.add("joggle 1;\nmodule contextual_artifact@1.0.0 {\n"
                          "  import logic@1;\n" +
                              generic_text + overloaded_text + "}\n",
                          "contextual-artifact.joggle");
  const bool contextual_linked = contextual_compiler.link();
  const auto replayed_generic =
      contextual_linked
          ? contextual_compiler.materialize("contextual_artifact.generic_value")
          : std::optional<joggle::Function>{};
  const auto replayed_overloaded =
      contextual_linked ? contextual_compiler.materialize(
                              "contextual_artifact.overloaded_value")
                        : std::optional<joggle::Function>{};
  ok &= expect(replayed_generic && replayed_overloaded &&
                   joggle::format(*replayed_generic, "generic_value") ==
                       generic_text &&
                   joggle::format(*replayed_overloaded, "overloaded_value") ==
                       overloaded_text,
               "context-selected function values preserve their callable "
               "type across canonical serialization");
  const auto direct_generic_arguments =
      direct_generic_callback_value &&
              direct_generic_callback_value->ops().size() == 1U
          ? direct_generic_callback_value->ops().front().arguments()
          : std::vector<joggle::Value>{};
  const auto direct_overloaded_arguments =
      direct_overloaded_callback_value &&
              direct_overloaded_callback_value->ops().size() == 1U
          ? direct_overloaded_callback_value->ops().front().arguments()
          : std::vector<joggle::Value>{};
  ok &= expect(direct_generic_arguments.size() == 2U &&
                   direct_generic_arguments[1].referenced_function() &&
                   direct_overloaded_arguments.size() == 2U &&
                   direct_overloaded_arguments[1].referenced_function() &&
                   direct_overloaded_arguments[1]
                           .referenced_function()
                           ->signature()
                           .find("word<8>") != std::string::npos,
               "a higher-order call propagates its inferred callable type "
               "into generic and overloaded function arguments");
  const std::string module_text = module ? joggle::format(*module) : "";
  joggle::Diagnostics module_roundtrip_diagnostics;
  const auto module_roundtrip = joggle::parse_module(
      module_text, module_roundtrip_diagnostics, "logic-roundtrip.joggle");
  ok &= expect(module_roundtrip &&
                   module_text.find("body: (T) -> U") != std::string::npos &&
                   module_text.find("callback_factory<T, U>() "
                                    "-> (T) -> U;") != std::string::npos &&
                   joggle::format(*module_roundtrip) == module_text,
               "function types format and parse canonically");
  joggle::Diagnostics invalid_callback_diagnostics;
  const auto invalid_callback = joggle::parse_module(
      R"(
joggle 1;
module invalid_callback@1.0.0 {
  fn apply(body: (missing) -> i32) -> i32;
}
)",
      invalid_callback_diagnostics, "invalid-callback.joggle");
  ok &= expect(!invalid_callback && !invalid_callback_diagnostics.ok(),
               "types nested in callable signatures are name-resolved");
  const auto main_symbol =
      module ? module->symbol(joggle::Module::SymbolKind::Function, "main")
             : std::nullopt;
  const auto reflected_function =
      main_symbol ? compiler.materialize(*main_symbol) : std::nullopt;
  ok &= expect(reflected_function &&
                   reflected_function->ops().size() == 2U,
               "a reflected function symbol opens without rebuilding its name");
  ok &= expect(function->declaration() && main_symbol &&
                   function->declaration()->symbol() == *main_symbol &&
                   function->result_types().size() == 1U &&
                   function->arguments().size() == 1U &&
                   operations.size() == 2U &&
                   function->entry().terminator().returned().size() == 1U &&
                   function->entry().terminator().returned().front() ==
                       operations.back().result(0) &&
                   operations.back().result(0).type() ==
                       function->arguments().front().type() &&
                   function->result_types().front() ==
                       operations.back().result(0).type(),
               "a concrete function signature and its SSA boundary agree");
  ok &= expect(operations.front().property<std::string>("name") ==
                   "input } // still a string",
               "function boundaries are parsed by the real string grammar");

  const std::string emitted = joggle::format(*function, "compiled");
  joggle::Compiler emitted_compiler;
  emitted_compiler.add(source, "logic.joggle");
  emitted_compiler.add("joggle 1;\nmodule artifact@1.0.0 {\n"
                       "  import logic@1;\n" +
                           emitted + "}\n",
                       "artifact.joggle");
  const bool emitted_linked = emitted_compiler.link();
  const auto emitted_function =
      emitted_linked ? emitted_compiler.materialize("artifact.compiled")
                     : std::optional<joggle::Function>{};
  ok &= expect(emitted_function &&
                   joggle::format(*emitted_function, "compiled") == emitted,
               "a committed Function formats to round-trippable canonical DSL");
  bool rejected_function_name = false;
  try {
    static_cast<void>(joggle::format(*function, "not.a.name"));
  } catch (const std::invalid_argument&) {
    rejected_function_name = true;
  }
  ok &= expect(rejected_function_name,
               "Function formatting rejects a non-DSL member name");

  joggle::Diagnostics roundtrip_diagnostics;
  const std::string canonical = joggle::format(*module);
  const auto roundtrip = joggle::parse_module(canonical, roundtrip_diagnostics,
                                              "canonical.joggle");
  ok &= expect(roundtrip && joggle::format(*roundtrip) == canonical,
               "one module formatter owns schema and function syntax");

  constexpr std::string_view cfg_source = R"(
joggle 1;
module cfg@1.0.0 {
  type word();
  fn identity(input: word) -> word;
  fn literal<T>(value: int) -> T ;
  fn choose(condition: i1, lhs: word, rhs: word) -> word {
    entry():
      branch condition, left(), right();

    left():
      jump merge(lhs);

    right():
      jump merge(rhs);

    merge(value: word):
      return value;
  }
  fn structured(condition: i1, lhs: word, rhs: word) -> word {
    return if condition { identity(lhs) } else { identity(rhs) };
  }
  fn specialized(lhs: word, rhs: word) -> word {
    return if true { identity(lhs) } else { identity(rhs) };
  }
  fn nested(first: i1, second: i1, lhs: word, middle: word, rhs: word)
      -> word {
    return if first {
      if second { identity(lhs) } else { identity(middle) }
    } else {
      identity(rhs)
    };
  }
  fn materialized(condition: i1) -> i32 {
    return if condition { 1 } else { 2 };
  }
  fn statement_branch(condition: i1, lhs: word, rhs: word) -> word {
    value = lhs;
    if condition {
      value = identity(value);
    } else {
      value = identity(rhs);
    }
    return value;
  }
  fn statement_specialized(lhs: word, rhs: word) -> word {
    value = lhs;
    if true {
      value = identity(value);
    } else {
      value = identity(rhs);
    }
    return value;
  }
  fn statement_without_else(condition: i1, lhs: word, rhs: word) -> word {
    value = lhs;
    if condition {
      value = identity(rhs);
    }
    return value;
  }
  fn early_return(condition: i1, lhs: word, rhs: word) -> word {
    if condition {
      return identity(lhs);
    }
    return identity(rhs);
  }
  fn early_return_both(condition: i1, lhs: word, rhs: word) -> word {
    if condition {
      return identity(lhs);
    } else {
      return identity(rhs);
    }
  }
  fn specialized_return(lhs: word, rhs: word) -> word {
    if true {
      return identity(lhs);
    }
    return identity(rhs);
  }
  fn loop_return(condition: i1, lhs: word, rhs: word) -> word {
    while condition {
      return identity(lhs);
    }
    return identity(rhs);
  }
  fn early_literal(condition: i1) -> i32 {
    if condition {
      return 1;
    }
    return 2;
  }
}
)";
  joggle::Diagnostics cfg_diagnostics;
  const auto cfg =
      joggle::parse_module(cfg_source, cfg_diagnostics, "cfg.joggle");
  const std::string cfg_canonical = cfg ? joggle::format(*cfg) : std::string{};
  joggle::Diagnostics cfg_roundtrip_diagnostics;
  const auto cfg_roundtrip =
      cfg ? joggle::parse_module(cfg_canonical, cfg_roundtrip_diagnostics,
                                 "cfg-canonical.joggle")
          : std::nullopt;
  ok &= expect(
      cfg && cfg_roundtrip && cfg_diagnostics.ok() &&
          cfg_roundtrip_diagnostics.ok() &&
          joggle::format(*cfg_roundtrip) == cfg_canonical &&
          cfg_canonical.find("branch condition, left(), right();") !=
              std::string::npos &&
          cfg_canonical.find("merge(value: word):") != std::string::npos &&
          cfg_canonical.find("return if condition { identity(lhs) } else { "
                             "identity(rhs) };") != std::string::npos &&
          cfg_canonical.find("if condition {\n") != std::string::npos &&
          cfg_canonical.find("      return identity(lhs);\n") !=
              std::string::npos &&
          cfg_canonical.find("      return identity(rhs);\n"
                             "    }\n"
                             "  }\n"
                             "  fn specialized_return") != std::string::npos,
      "one expression tree round-trips structured and explicit "
      "region-free control flow");

  joggle::Compiler cfg_compiler;
  cfg_compiler.add(cfg_source, "cfg.joggle");
  const bool cfg_linked = cfg_compiler.link();
  const auto cfg_function =
      cfg_linked ? cfg_compiler.materialize("cfg.choose") : std::nullopt;
  const auto cfg_structured =
      cfg_linked ? cfg_compiler.materialize("cfg.structured") : std::nullopt;
  const auto cfg_specialized =
      cfg_linked ? cfg_compiler.materialize("cfg.specialized") : std::nullopt;
  const auto cfg_nested =
      cfg_linked ? cfg_compiler.materialize("cfg.nested") : std::nullopt;
  const auto cfg_materialized =
      cfg_linked ? cfg_compiler.materialize("cfg.materialized") : std::nullopt;
  const auto cfg_statement_branch =
      cfg_linked ? cfg_compiler.materialize("cfg.statement_branch")
                 : std::nullopt;
  const auto cfg_statement_specialized =
      cfg_linked ? cfg_compiler.materialize("cfg.statement_specialized")
                 : std::nullopt;
  const auto cfg_statement_without_else =
      cfg_linked ? cfg_compiler.materialize("cfg.statement_without_else")
                 : std::nullopt;
  const auto cfg_early_return =
      cfg_linked ? cfg_compiler.materialize("cfg.early_return") : std::nullopt;
  const auto cfg_early_return_both =
      cfg_linked ? cfg_compiler.materialize("cfg.early_return_both")
                 : std::nullopt;
  const auto cfg_specialized_return =
      cfg_linked ? cfg_compiler.materialize("cfg.specialized_return")
                 : std::nullopt;
  const auto cfg_loop_return =
      cfg_linked ? cfg_compiler.materialize("cfg.loop_return") : std::nullopt;
  const auto cfg_early_literal =
      cfg_linked ? cfg_compiler.materialize("cfg.early_literal") : std::nullopt;
  const std::string cfg_ir =
      cfg_function ? joggle::format(*cfg_function, "choose") : std::string{};
  const auto materialized_operations = cfg_materialized
                                           ? cfg_materialized->ops()
                                           : std::vector<joggle::Op>{};
  const auto early_literal_operations =
      cfg_early_literal ? cfg_early_literal->ops()
                        : std::vector<joggle::Op>{};
  ok &= expect(
      cfg_function && cfg_structured && cfg_specialized && cfg_nested &&
          cfg_materialized && cfg_compiler.verify(*cfg_function) &&
          cfg_compiler.verify(*cfg_structured) &&
          cfg_compiler.verify(*cfg_specialized) &&
          cfg_compiler.verify(*cfg_nested) &&
          cfg_compiler.verify(*cfg_materialized) &&
          cfg_function->blocks().size() == 4U &&
          cfg_structured->blocks().size() == 4U &&
          cfg_specialized->blocks().size() == 1U &&
          cfg_nested->blocks().size() == 7U &&
          cfg_structured->ops().size() == 2U &&
          cfg_specialized->ops().size() == 1U &&
          cfg_nested->ops().size() == 3U &&
          cfg_specialized->entry().terminator().returned().front() ==
              cfg_specialized->ops().front().result(0) &&
          cfg_function->entry().terminator().kind() ==
              joggle::Terminator::Kind::Branch &&
          cfg_structured->entry().terminator().kind() ==
              joggle::Terminator::Kind::Branch &&
          cfg_function->blocks().back().arguments().size() == 1U &&
          cfg_function->blocks().back().terminator().returned().front() ==
              cfg_function->blocks().back().arguments().front() &&
          cfg_ir.find("branch arg0, block1(), block2();") !=
              std::string::npos &&
          cfg_ir.find("block3(arg3: cfg.word):") != std::string::npos,
      "explicit source blocks instantiate as Function-owned CFG and "
      "format without a nested ownership container");
  ok &= expect(
      cfg_materialized && cfg_materialized->blocks().size() == 4U &&
          cfg_materialized->ops().size() == 2U &&
          std::all_of(materialized_operations.begin(),
                      materialized_operations.end(),
                      [](const joggle::Op& op) {
                        return op.callee().name() == "literal";
                      }) &&
          cfg_materialized->result_types().front() ==
              cfg_materialized->blocks().back().arguments().front().type(),
      "unequal Known branch values use a visible literal function "
      "before crossing Residual edges");
  ok &= expect(
      cfg_statement_branch && cfg_statement_specialized &&
          cfg_statement_without_else &&
          cfg_compiler.verify(*cfg_statement_branch) &&
          cfg_compiler.verify(*cfg_statement_specialized) &&
          cfg_compiler.verify(*cfg_statement_without_else) &&
          cfg_statement_branch->blocks().size() == 4U &&
          cfg_statement_branch->ops().size() == 2U &&
          cfg_statement_branch->blocks().back().arguments().size() == 1U &&
          cfg_statement_specialized->blocks().size() == 1U &&
          cfg_statement_specialized->ops().size() == 1U &&
          cfg_statement_without_else->blocks().size() == 4U &&
          cfg_statement_without_else->ops().size() == 1U &&
          cfg_statement_without_else->blocks().back().arguments().size() == 1U,
      "statement if specializes Known control and automatically "
      "merges outer rebindings under Residual control");
  ok &= expect(
      cfg_early_return && cfg_early_return_both && cfg_specialized_return &&
          cfg_loop_return && cfg_early_literal &&
          cfg_compiler.verify(*cfg_early_return) &&
          cfg_compiler.verify(*cfg_early_return_both) &&
          cfg_compiler.verify(*cfg_specialized_return) &&
          cfg_compiler.verify(*cfg_loop_return) &&
          cfg_compiler.verify(*cfg_early_literal) &&
          cfg_early_return->blocks().size() == 3U &&
          cfg_early_return->ops().size() == 2U &&
          cfg_early_return_both->blocks().size() == 3U &&
          cfg_early_return_both->ops().size() == 2U &&
          cfg_specialized_return->blocks().size() == 1U &&
          cfg_specialized_return->ops().size() == 1U &&
          cfg_loop_return->blocks().size() == 4U &&
          cfg_loop_return->ops().size() == 2U &&
          cfg_early_literal->blocks().size() == 3U &&
          cfg_early_literal->ops().size() == 2U &&
          std::all_of(early_literal_operations.begin(),
                      early_literal_operations.end(),
                      [](const joggle::Op& op) {
                        return op.callee().name() == "literal";
                      }),
      "structured returns terminate only their selected control "
      "paths without a Region or synthetic merge");

  joggle::Diagnostics incomplete_return_diagnostics;
  const auto incomplete_return = joggle::parse_module(
      R"(
joggle 1;
module incomplete_return@1.0.0 {
  type word();
  fn invalid(condition: i1, input: word) -> word {
    if condition {
      return input;
    }
  }
}
)",
      incomplete_return_diagnostics, "incomplete-return.joggle");
  const bool reports_incomplete_return = std::any_of(
      incomplete_return_diagnostics.entries().begin(),
      incomplete_return_diagnostics.entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("path that does not return") !=
               std::string::npos;
      });
  ok &= expect(!incomplete_return && reports_incomplete_return,
               "structured functions reject a fallthrough path");

  joggle::Diagnostics unreachable_statement_diagnostics;
  const auto unreachable_statement = joggle::parse_module(
      R"(
joggle 1;
module unreachable_statement@1.0.0 {
  type word();
  fn invalid(condition: i1, input: word) -> word {
    if condition {
      return input;
      value = input;
    }
    return input;
  }
}
)",
      unreachable_statement_diagnostics, "unreachable-statement.joggle");
  const bool reports_unreachable_statement =
      std::any_of(unreachable_statement_diagnostics.entries().begin(),
                  unreachable_statement_diagnostics.entries().end(),
                  [](const joggle::Diagnostic& diagnostic) {
                    return diagnostic.message.find("unreachable statement") !=
                           std::string::npos;
                  });
  ok &= expect(!unreachable_statement && reports_unreachable_statement,
               "statements after a structured control transfer are rejected");

  joggle::Compiler missing_literal;
  missing_literal.add(R"(
joggle 1;
module missing_literal@1.0.0 {
  fn choose(condition: i1) -> i32 {
    return if condition { 1 } else { 2 };
  }
}
)",
                      "missing-literal.joggle");
  const bool missing_literal_linked = missing_literal.link();
  const auto missing_literal_function =
      missing_literal_linked
          ? missing_literal.materialize("missing_literal.choose")
          : std::optional<joggle::Function>{};
  const bool reports_missing_literal = std::any_of(
      missing_literal.diagnostics().entries().begin(),
      missing_literal.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("no visible literal function") !=
               std::string::npos;
      });
  ok &= expect(missing_literal_linked && !missing_literal_function &&
                   reports_missing_literal,
               "Known values cannot cross dynamic control without an "
               "explicitly visible literal contract");

  joggle::Compiler ambiguous_literal;
  ambiguous_literal.add(R"(
joggle 1;
module literal_a@1.0.0 {
  fn literal<T>(value: int) -> T;
}
)",
                        "literal-a.joggle");
  ambiguous_literal.add(R"(
joggle 1;
module literal_b@1.0.0 {
  fn literal<T>(value: int) -> T;
}
)",
                        "literal-b.joggle");
  ambiguous_literal.add(R"(
joggle 1;
module ambiguous_literal@1.0.0 {
  import literal_a@1.0.0;
  import literal_b@1.0.0;
  fn choose(condition: i1) -> i32 {
    return if condition { 1 } else { 2 };
  }
}
)",
                        "ambiguous-literal.joggle");
  const bool ambiguous_literal_linked = ambiguous_literal.link();
  const auto ambiguous_literal_function =
      ambiguous_literal_linked
          ? ambiguous_literal.materialize("ambiguous_literal.choose")
          : std::optional<joggle::Function>{};
  const bool reports_ambiguous_literal =
      std::any_of(ambiguous_literal.diagnostics().entries().begin(),
                  ambiguous_literal.diagnostics().entries().end(),
                  [](const joggle::Diagnostic& diagnostic) {
                    return diagnostic.message.find(
                               "more than one visible literal function") !=
                           std::string::npos;
                  });
  ok &= expect(ambiguous_literal_linked && !ambiguous_literal_function &&
                   reports_ambiguous_literal,
               "ambiguous literal contracts fail deterministically");

  constexpr std::string_view loop_source = R"(
joggle 1;
module loops@1.0.0 {
  type word(width: int);
  fn source<T>() -> T;
  fn literal<T>(value: int) -> T ;
  fn less(lhs: i32, rhs: i32) -> i1;
  fn (<)(lhs: i32, rhs: i32) -> i1;
  fn (+)(lhs: i32, rhs: i32) -> i32;
  fn next(input: i32) -> i32;

  fn repeat(start: i32, limit: i32) -> i32 {
    current = start;
    while less(current, limit) {
      current = next(current);
    }
    return current;
  }

  fn count_from_zero(limit: i32) -> i32 {
    current: i32 = 0;
    while less(current, limit) {
      current = next(current);
    }
    return current;
  }

  fn huge_counted_loop(input: i32) -> i32 {
    current = input;
    for offset: i32 in @range(1000000000) {
      current = next(current);
      break;
    }
    return current;
  }

  fn specialize() -> word<1> {
    count = 0;
    running = true;
    while running {
      count = @(count + 1);
      running = false;
    }
    value: word<count> = source();
    return value;
  }

  fn controlled(start: i32, limit: i32, skip: i1, stop: i1) -> i32 {
    current = start;
    while less(current, limit) {
      current = next(current);
      if skip {
        continue;
      }
      current = next(current);
      if stop {
        break;
      }
      current = next(current);
    }
    return current;
  }

  fn known_break() -> word<1> {
    running = true;
    count = 0;
    while running {
      count = @(count + 1);
      break;
    }
    value: word<count> = source();
    return value;
  }

  fn known_continue() -> word<1> {
    running = true;
    count = 0;
    while running {
      running = false;
      count = @(count + 1);
      continue;
    }
    value: word<count> = source();
    return value;
  }
}
)";
  joggle::Diagnostics loop_parse_diagnostics;
  const auto loop_module =
      joggle::parse_module(loop_source, loop_parse_diagnostics, "loops.joggle");
  const std::string loop_canonical =
      loop_module ? joggle::format(*loop_module) : std::string{};
  joggle::Diagnostics loop_roundtrip_diagnostics;
  const auto loop_roundtrip =
      loop_module
          ? joggle::parse_module(loop_canonical, loop_roundtrip_diagnostics,
                                 "loops-canonical.joggle")
          : std::nullopt;
  ok &= expect(loop_module && loop_roundtrip && loop_parse_diagnostics.ok() &&
                   loop_roundtrip_diagnostics.ok() &&
                   joggle::format(*loop_roundtrip) == loop_canonical &&
                   loop_canonical.find("while less(current, limit) {") !=
                       std::string::npos &&
                   loop_canonical.find("        continue;\n") !=
                       std::string::npos &&
                   loop_canonical.find("        break;\n") != std::string::npos,
               "structured while syntax round-trips without a Region form");

  joggle::Compiler loop_compiler;
  loop_compiler.add(loop_source, "loops.joggle");
  const bool loops_linked = loop_compiler.link();
  const auto repeat = loops_linked ? loop_compiler.materialize("loops.repeat")
                                   : std::optional<joggle::Function>{};
  const auto specialize = loops_linked
                              ? loop_compiler.materialize("loops.specialize")
                              : std::optional<joggle::Function>{};
  const auto count_from_zero =
      loops_linked ? loop_compiler.materialize("loops.count_from_zero")
                   : std::optional<joggle::Function>{};
  const auto huge_counted_loop =
      loops_linked ? loop_compiler.materialize("loops.huge_counted_loop")
                   : std::optional<joggle::Function>{};
  const auto controlled = loops_linked
                              ? loop_compiler.materialize("loops.controlled")
                              : std::optional<joggle::Function>{};
  const auto known_break = loops_linked
                               ? loop_compiler.materialize("loops.known_break")
                               : std::optional<joggle::Function>{};
  const auto known_continue =
      loops_linked ? loop_compiler.materialize("loops.known_continue")
                   : std::optional<joggle::Function>{};
  ok &= expect(repeat && loop_compiler.verify(*repeat) &&
                   repeat->blocks().size() == 4U &&
                   repeat->ops().size() == 2U &&
                   repeat->entry().terminator().kind() ==
                       joggle::Terminator::Kind::Jump &&
                   repeat->blocks()[1].arguments().size() == 1U &&
                   repeat->blocks()[1].terminator().kind() ==
                       joggle::Terminator::Kind::Branch &&
                   repeat->blocks()[3].arguments().size() == 1U,
               "Residual loops carry rebinding through typed Block "
               "arguments");
  ok &= expect(count_from_zero && loop_compiler.verify(*count_from_zero) &&
                   count_from_zero->blocks().size() == 4U &&
                   count_from_zero->ops().size() == 3U &&
                   count_from_zero->ops().front().callee().name() ==
                       "literal",
               "a typed Known initializer materializes before becoming a "
               "Residual loop-carried value");
  ok &= expect(huge_counted_loop &&
                   loop_compiler.verify(*huge_counted_loop) &&
                   huge_counted_loop->blocks().size() <= 5U &&
                   huge_counted_loop->ops().size() <= 8U,
               "a typed Prelude range materializes a compact counted loop "
               "without allocating its billion-element compiler list");
  ok &= expect(specialize && loop_compiler.verify(*specialize) &&
                   specialize->blocks().size() == 1U &&
                   specialize->ops().size() == 1U &&
                   specialize->result_types().front() ==
                       specialize->ops().front().result(0).type(),
               "Known loops execute during specialization without entering "
               "the residual CFG");
  ok &= expect(
      controlled && loop_compiler.verify(*controlled) &&
          controlled->blocks().size() == 8U &&
          controlled->ops().size() == 4U &&
          controlled->blocks()[1].arguments().size() == 1U &&
          controlled->predecessors(controlled->blocks()[1]).size() == 3U &&
          controlled->predecessors(controlled->blocks()[3]).size() == 2U,
      "Residual break and continue carry current values directly to "
      "the loop exit and header");
  ok &= expect(
      known_break && known_continue && loop_compiler.verify(*known_break) &&
          loop_compiler.verify(*known_continue) &&
          known_break->blocks().size() == 1U &&
          known_continue->blocks().size() == 1U &&
          known_break->result_types().front().get<std::int64_t>("width") ==
              std::optional<std::int64_t>{1} &&
          known_continue->result_types().front().get<std::int64_t>("width") ==
              std::optional<std::int64_t>{1},
      "Known break and continue execute as compiler control without "
      "entering the residual CFG");

  joggle::Diagnostics outside_loop_diagnostics;
  const auto outside_loop =
      joggle::parse_module(R"(
joggle 1;
module outside_loop@1.0.0 {
  fn invalid() {
    break;
  }
}
)",
                           outside_loop_diagnostics, "outside-loop.joggle");
  const bool reports_outside_loop = std::any_of(
      outside_loop_diagnostics.entries().begin(),
      outside_loop_diagnostics.entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("only valid inside a structured loop") !=
               std::string::npos;
      });
  ok &= expect(!outside_loop && reports_outside_loop,
               "loop control outside a structured loop is rejected");

  joggle::Compiler mixed_loop_transfer;
  mixed_loop_transfer.add(R"(
joggle 1;
module mixed_loop_transfer@1.0.0 {
  fn literal<T>(value: int) -> T ;

  fn break_on(stop: i1) -> i32 {
    running = true;
    count = 1;
    while running {
      if stop {
        break;
      }
      count = 2;
      running = false;
    }
    return count;
  }

  fn continue_on(skip: i1) {
    running = true;
    while running {
      running = false;
      if skip {
        continue;
      }
    }
    return;
  }
}
)",
                          "mixed-loop-transfer.joggle");
  const bool mixed_loop_transfer_linked = mixed_loop_transfer.link();
  const auto mixed_break =
      mixed_loop_transfer_linked
          ? mixed_loop_transfer.materialize("mixed_loop_transfer.break_on")
          : std::optional<joggle::Function>{};
  const auto mixed_continue =
      mixed_loop_transfer_linked
          ? mixed_loop_transfer.materialize("mixed_loop_transfer.continue_on")
          : std::optional<joggle::Function>{};
  const auto mixed_control_shape = [](const joggle::Function& function) {
    const auto blocks = function.blocks();
    return blocks.size() == 3U &&
           blocks.front().terminator().kind() ==
               joggle::Terminator::Kind::Branch &&
           blocks[1].terminator().kind() == joggle::Terminator::Kind::Return &&
           blocks[2].terminator().kind() == joggle::Terminator::Kind::Return;
  };
  std::vector<std::int64_t> mixed_break_literals;
  if (mixed_break) {
    for (const auto& op : mixed_break->ops()) {
      if (const auto value = op.property<std::int64_t>("value")) {
        mixed_break_literals.push_back(*value);
      }
    }
    std::sort(mixed_break_literals.begin(), mixed_break_literals.end());
  }
  ok &= expect(mixed_break && mixed_continue &&
                   mixed_loop_transfer.verify(*mixed_break) &&
                   mixed_loop_transfer.verify(*mixed_continue) &&
                   mixed_control_shape(*mixed_break) &&
                   mixed_control_shape(*mixed_continue) &&
                   mixed_break_literals == std::vector<std::int64_t>({1, 2}),
               "Residual break and continue inside finite Known loops retain "
               "separate specialized continuations and materialize Known "
               "state only when the return type requires it");

  joggle::Compiler cyclic_mixed_loop({.steps = 8, .depth = 64});
  cyclic_mixed_loop.add(R"(
joggle 1;
module cyclic_mixed_loop@1.0.0 {
  fn literal<T>(value: bool) -> T ;
  fn literal<T>(value: int) -> T ;

  fn rebuild<Start: int>(phase: Start, skip: i1) -> i32 {
    running = true;
    while running {
      if skip {
        phase = 1;
        continue;
      }
      running = false;
    }
    return phase;
  }
}
)",
                        "cyclic-mixed-loop.joggle");
  const bool cyclic_mixed_loop_linked = cyclic_mixed_loop.link();
  const auto cyclic_mixed_loop_module =
      cyclic_mixed_loop.module("cyclic_mixed_loop");
  const auto cyclic_mixed_loop_declaration =
      cyclic_mixed_loop_module ? cyclic_mixed_loop_module->function("rebuild")
                               : std::nullopt;
  const auto compiler_integer = cyclic_mixed_loop.make("int");
  const auto initial_phase =
      compiler_integer
          ? cyclic_mixed_loop.known(*compiler_integer, std::int64_t{0})
          : std::nullopt;
  const auto cyclic_mixed_loop_function =
      cyclic_mixed_loop_linked && cyclic_mixed_loop_declaration && initial_phase
          ? cyclic_mixed_loop.materialize(*cyclic_mixed_loop_declaration,
                                          {*initial_phase})
          : std::optional<joggle::Function>{};
  if (!cyclic_mixed_loop_function) {
    cyclic_mixed_loop.diagnostics().print(std::cerr);
  }
  std::vector<bool> cyclic_bool_literals;
  std::vector<std::int64_t> cyclic_integer_literals;
  if (cyclic_mixed_loop_function) {
    for (const auto& op : cyclic_mixed_loop_function->ops()) {
      if (const auto value = op.property<bool>("value")) {
        cyclic_bool_literals.push_back(*value);
      }
      if (const auto value = op.property<std::int64_t>("value")) {
        cyclic_integer_literals.push_back(*value);
      }
    }
    std::sort(cyclic_bool_literals.begin(), cyclic_bool_literals.end());
    std::sort(cyclic_integer_literals.begin(), cyclic_integer_literals.end());
  }
  const auto has_backedge = [](const joggle::Function& function) {
    const auto blocks = function.blocks();
    for (std::size_t index = 0; index < blocks.size(); ++index) {
      const auto terminator = blocks[index].terminator();
      for (std::size_t successor = 0; successor < terminator.successor_count();
           ++successor) {
        const auto target = std::find(blocks.begin(), blocks.end(),
                                      terminator.successor(successor));
        if (target != blocks.end() && static_cast<std::size_t>(std::distance(
                                          blocks.begin(), target)) <= index) {
          return true;
        }
      }
    }
    return false;
  };
  ok &= expect(cyclic_mixed_loop_function &&
                   cyclic_mixed_loop.verify(*cyclic_mixed_loop_function) &&
                   has_backedge(*cyclic_mixed_loop_function) &&
                   cyclic_bool_literals.empty() &&
                   std::find(cyclic_integer_literals.begin(),
                             cyclic_integer_literals.end(),
                             0) != cyclic_integer_literals.end() &&
                   std::find(cyclic_integer_literals.begin(),
                             cyclic_integer_literals.end(),
                             1) != cyclic_integer_literals.end(),
               "a repeated mixed-stage state closes a CFG cycle without "
               "materializing its Known control state");

  joggle::Compiler computed_cycle({.steps = 8, .depth = 64});
  computed_cycle.add(R"(
joggle 1;
module computed_cycle@1.0.0 {
  fn condition(value: bool) -> bool {
    return value;
  }

  fn invalid(skip: i1) {
    running = true;
    while @condition(running) {
      if skip {
        continue;
      }
      running = false;
    }
    return;
  }
}
)",
                     "computed-cycle.joggle");
  const bool computed_cycle_linked = computed_cycle.link();
  const auto computed_cycle_function =
      computed_cycle_linked
          ? computed_cycle.materialize("computed_cycle.invalid")
          : std::optional<joggle::Function>{};
  ok &= expect(computed_cycle_linked && computed_cycle_function &&
                   computed_cycle.verify(*computed_cycle_function) &&
                   has_backedge(*computed_cycle_function),
               "a computed Known condition closes a finite specialized CFG "
               "cycle without requiring a bool-to-i1 representation");

  joggle::Compiler unconstrained_cycle({.steps = 8, .depth = 64});
  unconstrained_cycle.add(R"(
joggle 1;
module unconstrained_cycle@1.0.0 {
  fn invalid(skip: i1) {
    running = true;
    token = 0;
    while running {
      if skip {
        token = 1;
        continue;
      }
      running = false;
    }
    return;
  }
}
)",
                          "unconstrained-cycle.joggle");
  const bool unconstrained_cycle_linked = unconstrained_cycle.link();
  const auto unconstrained_cycle_function =
      unconstrained_cycle_linked
          ? unconstrained_cycle.materialize("unconstrained_cycle.invalid")
          : std::optional<joggle::Function>{};
  ok &= expect(unconstrained_cycle_linked && unconstrained_cycle_function &&
                   unconstrained_cycle.verify(*unconstrained_cycle_function) &&
                   has_backedge(*unconstrained_cycle_function) &&
                   unconstrained_cycle_function->ops().empty(),
               "control-state specialization needs no target width for an "
               "unconstrained compiler integer");

  joggle::Compiler bounded_loop({.steps = 2, .depth = 64});
  bounded_loop.add(R"(
joggle 1;
module bounded_loop@1.0.0 {
  fn never_finishes() {
    running = true;
    while running {
      running = true;
    }
    return;
  }
}
)",
                   "bounded-loop.joggle");
  const bool bounded_loop_linked = bounded_loop.link();
  const auto never_finishes =
      bounded_loop_linked
          ? bounded_loop.materialize("bounded_loop.never_finishes")
          : std::optional<joggle::Function>{};
  const bool reports_loop_limit =
      std::any_of(bounded_loop.diagnostics().entries().begin(),
                  bounded_loop.diagnostics().entries().end(),
                  [](const joggle::Diagnostic& diagnostic) {
                    return diagnostic.message.find(
                               "compile-time while iteration limit exceeded") !=
                           std::string::npos;
                  });
  ok &= expect(bounded_loop_linked && !never_finishes && reports_loop_limit,
               "Known loops fail deterministically when their evaluation "
               "budget is exhausted");

  joggle::Diagnostics invalid_cfg_diagnostics;
  const auto invalid_cfg =
      joggle::parse_module(R"(
joggle 1;
module invalid_cfg@1.0.0 {
  type word();
  fn choose(condition: i1, value: word) -> word {
    entry():
      jump merge();
    merge(result: word):
      return result;
  }
}
)",
                           invalid_cfg_diagnostics, "invalid-cfg.joggle");
  ok &= expect(!invalid_cfg && !invalid_cfg_diagnostics.ok() &&
                   std::any_of(invalid_cfg_diagnostics.entries().begin(),
                               invalid_cfg_diagnostics.entries().end(),
                               [](const joggle::Diagnostic& diagnostic) {
                                 return diagnostic.message.find(
                                            "edge provides 0") !=
                                        std::string::npos;
                               }),
               "CFG verification rejects a block-edge arity mismatch");

  joggle::Diagnostics legacy_diagnostics;
  const auto legacy = joggle::parse_module(
      "joggle 1; module legacy@1.0.0 { op old(body: region); }",
      legacy_diagnostics, "legacy.joggle");
  ok &= expect(!legacy && !legacy_diagnostics.ok(),
               "the former body-as-parameter syntax is rejected");

  constexpr std::string_view undefined = R"(
joggle 1;
module logic@1.0.0 {
  type word(width: int);
  fn add<T>(lhs: T, rhs: T) -> T;
  fn main() -> word<8> {
    sum: word<8> = add(missing, missing);
    return sum;
  }
}
)";
  joggle::Compiler invalid;
  const auto invalid_function = load_function(invalid, undefined);
  const auto invalid_diagnostics = invalid.diagnostics().entries();
  ok &= expect(
      !invalid_function && !invalid.ok() && !invalid_diagnostics.empty() &&
          invalid_diagnostics.front().source.has_value() &&
          invalid_diagnostics.front().source->source == "logic.joggle" &&
          invalid_diagnostics.front().source->begin.line == 7U,
      "undefined SSA diagnostics point into the function");

  joggle::Compiler unknown;
  unknown.add(R"(
joggle 1;
module logic@1.0.0 {
  type different();
}
)",
              "unknown.joggle");
  if (!unknown.link()) {
    return EXIT_FAILURE;
  }
  const auto foreign = unknown.materialize("logic.main");
  ok &= expect(!foreign && !unknown.ok(),
               "a named function keeps its module identity");

  joggle::Compiler unqualified;
  unqualified.add(source, "logic.joggle");
  const bool unqualified_linked = unqualified.link();
  const auto unqualified_function = unqualified_linked
                                        ? unqualified.materialize("main")
                                        : std::optional<joggle::Function>{};
  const auto unqualified_diagnostics = unqualified.diagnostics().entries();
  ok &=
      expect(!unqualified_function && !unqualified_diagnostics.empty() &&
                 unqualified_diagnostics.back().message.find("module.member") !=
                     std::string::npos,
             "function lookup requires one unambiguous qualified member name");

  joggle::Compiler mismatch;
  mismatch.add(R"(
joggle 1;
module mismatch@1.0.0 {
  type a();
  type b();
  fn same<T>(lhs: T, rhs: T) -> T;
  fn main(lhs: a, rhs: b) -> a {
    result = same(lhs, rhs);
    return result;
  }
}
)",
               "mismatch.joggle");
  const bool mismatch_linked = mismatch.link();
  const auto mismatch_function = mismatch_linked
                                     ? mismatch.materialize("mismatch.main")
                                     : std::optional<joggle::Function>{};
  ok &= expect(!mismatch_function && !mismatch.ok(),
               "one type variable rejects operands with different types");

  joggle::Compiler return_inferred;
  return_inferred.add(R"(
joggle 1;
module return_inferred@1.0.0 {
  type a();
  fn source<T>() -> T;
  fn main() -> a {
    result = source();
    return result;
  }
}
)",
                      "return-inferred.joggle");
  const bool return_inferred_linked = return_inferred.link();
  const auto return_inferred_function =
      return_inferred_linked
          ? return_inferred.materialize("return_inferred.main")
          : std::optional<joggle::Function>{};
  ok &= expect(
      return_inferred_function &&
          return_inferred_function->entry().terminator().returned().size() ==
              1U,
      "a function result constrains an output-only type variable");

  joggle::Compiler unbound;
  unbound.add(R"(
joggle 1;
module unbound@1.0.0 {
  type a();
  fn source<T>() -> T;
  fn main() {
    result = source();
    return;
  }
}
)",
              "unbound.joggle");
  const bool unbound_linked = unbound.link();
  const auto unbound_function = unbound_linked
                                    ? unbound.materialize("unbound.main")
                                    : std::optional<joggle::Function>{};
  ok &=
      expect(!unbound_function && !unbound.ok(),
             "an unconstrained output-only type variable needs an annotation");

  constexpr std::string_view dependent_source = R"(
joggle 1;
module dependent@1.0.0 {
  type word(width: int);
  fn input<N: int>(width: N) -> word<N>;
  fn default_input<N: int>(width: N = 8) -> word<N>;
  fn align(value: int, multiple: int) -> int {
    return ceildiv(value, multiple) * multiple;
  }
  fn double(value: int) -> int;

  fn inferred() {
    aligned = @align(3, 4);
    width = @(if true { double(aligned) } else { 1 / 0 });
    value = input(width: width);
    return;
  }
}
)";
  joggle::Compiler dependent;
  dependent.add(dependent_source, "dependent.joggle");
  const bool dependent_linked = dependent.link();
  const auto dependent_module = dependent.module("dependent");
  const auto dependent_double =
      dependent_module ? dependent_module->function("double") : std::nullopt;
  if (dependent_double) {
    dependent.bind(*dependent_double,
                   [](std::int64_t value) { return value * 2; });
  }
  auto dependent_function = dependent_linked && dependent_double
                                ? dependent.materialize("dependent.inferred")
                                : std::nullopt;
  const auto dependent_operations = dependent_function
                                        ? dependent_function->ops()
                                        : std::vector<joggle::Op>{};
  const auto dependent_width =
      dependent_operations.empty()
          ? std::optional<std::int64_t>{}
          : dependent_operations.front().result(0).type().get<std::int64_t>(
                "width");
  ok &= expect(
      dependent_module && dependent_function && dependent_width &&
          *dependent_width == 8 &&
          joggle::format(*dependent_module)
                  .find("fn input<N: int>(width: N) -> word<N>;") !=
              std::string::npos &&
          joggle::format(*dependent_module)
                  .find("fn default_input<N: int>(width: N = 8) -> word<N>;") !=
              std::string::npos,
      "a computed Known argument binds a generic and infers a text "
      "function result without another annotation");

  joggle::Compiler inconsistent;
  inconsistent.add(dependent_source, "dependent.joggle");
  const bool inconsistent_linked = inconsistent.link();
  const auto inconsistent_module = inconsistent.module("dependent");
  const auto inconsistent_word =
      inconsistent_module ? inconsistent_module->type("word") : std::nullopt;
  const auto inconsistent_input = inconsistent_module
                                      ? inconsistent_module->function("input")
                                      : std::nullopt;
  const auto default_input =
      inconsistent_module ? inconsistent_module->function("default_input")
                          : std::nullopt;
  const auto word8 =
      inconsistent_word ? inconsistent.make(*inconsistent_word, std::int64_t{8})
                        : std::nullopt;
  const auto int_type = inconsistent.make("int");
  const auto width7 =
      int_type ? inconsistent.known(*int_type, std::int64_t{7}) : std::nullopt;
  auto inconsistent_function = inconsistent.create_function();
  if (!inconsistent_linked || !inconsistent_input || !default_input || !word8 ||
      !width7 || !inconsistent_function) {
    return EXIT_FAILURE;
  }
  bool inconsistent_rejected = false;
  {
    auto edit = inconsistent_function->edit();
    const auto value = edit.append(*inconsistent_input, {*width7}, {*word8});
    edit.ret(inconsistent_function->entry(), {value.result(0)});
    joggle::Diagnostics diagnostics;
    inconsistent_rejected = !edit.commit(diagnostics) && !diagnostics.ok();
  }
  ok &= expect(inconsistent_rejected &&
                   inconsistent_function->arguments().empty() &&
                   inconsistent_function->ops().empty(),
               "commit validates an explicit result against its dependent "
               "known argument and rolls back on mismatch");

  joggle::Compiler defaulted;
  defaulted.add(dependent_source, "dependent.joggle");
  const bool defaulted_linked = defaulted.link();
  const auto defaulted_module = defaulted.module("dependent");
  const auto defaulted_input = defaulted_module
                                   ? defaulted_module->function("default_input")
                                   : std::nullopt;
  const auto named_input =
      defaulted_module ? defaulted_module->function("input") : std::nullopt;
  const auto defaulted_int = defaulted.make("int");
  auto defaulted_function = defaulted.create_function();
  if (!defaulted_linked || !defaulted_input || !named_input || !defaulted_int ||
      !defaulted_function) {
    return EXIT_FAILURE;
  }
  {
    auto edit = defaulted_function->edit();
    const auto value = edit.append(*defaulted_input);
    edit.ret(defaulted_function->entry(), {value.result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  const auto defaulted_width = defaulted_function->entry()
                                   .terminator()
                                   .returned()
                                   .front()
                                   .type()
                                   .get<std::int64_t>("width");
  ok &= expect(defaulted_width && *defaulted_width == 8 &&
                   defaulted.verify(*defaulted_function),
               "C++ append infers a result from a schema-owned default "
               "default without repeating the type");

  auto named_function = defaulted.create_function();
  if (!named_function) {
    return EXIT_FAILURE;
  }
  {
    auto edit = named_function->edit();
    auto width = defaulted.known(*defaulted_int, std::int64_t{12});
    if (!width) {
      return EXIT_FAILURE;
    }
    const auto value = edit.append(*named_input, {*width}).value();
    edit.ret(named_function->entry(), {value});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  const auto named_width = named_function->entry()
                               .terminator()
                               .returned()
                               .front()
                               .type()
                               .get<std::int64_t>("width");
  ok &= expect(named_width && *named_width == 12 &&
                   defaulted.verify(*named_function),
               "a Known C++ argument participates in result inference at "
               "Op creation");

  bool extra_argument_rejected = false;
  try {
    auto edit = named_function->edit();
    auto one = defaulted.known(*defaulted_int, std::int64_t{1});
    auto two = defaulted.known(*defaulted_int, std::int64_t{2});
    edit.append(*named_input, {*one, *two});
  } catch (const std::invalid_argument& error) {
    extra_argument_rejected =
        std::string_view(error.what()).find("too many arguments") !=
        std::string_view::npos;
  }
  ok &= expect(extra_argument_rejected,
               "an extra C++ argument is rejected immediately");

  bool wrong_known_kind_rejected = false;
  try {
    auto edit = named_function->edit();
    const auto string_type = defaulted.make("string");
    const auto wide =
        string_type ? defaulted.known(*string_type, "wide") : std::nullopt;
    if (!wide) {
      return EXIT_FAILURE;
    }
    edit.append(*named_input, {*wide});
  } catch (const std::invalid_argument& error) {
    wrong_known_kind_rejected =
        std::string_view(error.what()).find("compatible Known value") !=
        std::string_view::npos;
  }
  ok &= expect(wrong_known_kind_rejected,
               "a wrong-kind Known C++ argument is rejected immediately");

  constexpr std::string_view computed_source = R"(
joggle 1;
module computed@1.0.0 {
  type word(width: int);

  fn align(value: int, multiple: int) -> int {
    return ceildiv(value, multiple) * multiple;
  }

  fn (//)(lhs: int, rhs: int) -> int {
    return lhs + rhs;
  }
  fn guarded(value: int) -> int {
    return if true { value } else { 1 / 0 };
  }

  fn extend<W: int>(input: word<W>) -> word<W + 1>;
  fn packed<M: int, N: int>(rows: M, columns: N)
    -> word<ceildiv(M * N, 8)>;
  fn aligned<W: int>(input: word<W>) -> word<align(W, 8)>;
  fn (+)<W: int>(lhs: word<W>, rhs: word<W>) -> word<W>;
  fn (//)<W: int>(lhs: word<W>, rhs: word<W>) -> word<W>;
  fn postfix (!)(value: int) -> int;
  fn combined<W: int>(input: word<W>) -> word<@(W // 2)>;
  fn selected<W: int>(input: word<W>) -> word<@(guarded(W + 2))>;
  fn hosted<W: int>(input: word<W>) -> word<@(W!)>;

  fn main(input: word<7>) -> word<8> {
    result = extend(input);
    return result;
  }

  fn pack() -> word<15> {
    result = packed(rows = 10, columns = 12);
    return result;
  }

  fn align_width(input: word<10>) -> word<16> {
    result = aligned(input);
    return result;
  }

  fn sum(lhs: word<8>, rhs: word<8>) -> word<8> {
    result = lhs + rhs;
    return result;
  }

  fn quotient(lhs: word<8>, rhs: word<8>) -> word<8> {
    result = lhs // rhs;
    return result;
  }

  fn compile_time_operator(input: word<6>) -> word<8> {
    result = combined(input);
    return result;
  }

  fn compile_time_branch(input: word<6>) -> word<8> {
    result = selected(input);
    return result;
  }

  fn compile_time_host(input: word<7>) -> word<14> {
    result = hosted(input);
    return result;
  }
}
)";
  joggle::Compiler computed;
  computed.add(computed_source, "computed.joggle");
  const bool computed_linked = computed.link();
  const auto computed_module = computed.module("computed");
  const auto host_double =
      computed_module ? computed_module->function("!") : std::nullopt;
  if (computed_linked && host_double) {
    computed.bind(*host_double, [](std::int64_t value) { return value * 2; });
  }
  const auto computed_function =
      computed_linked ? computed.materialize("computed.main") : std::nullopt;
  const auto packed_function =
      computed_linked ? computed.materialize("computed.pack") : std::nullopt;
  const auto aligned_function =
      computed_linked ? computed.materialize("computed.align_width")
                      : std::nullopt;
  const auto sum_function =
      computed_linked ? computed.materialize("computed.sum") : std::nullopt;
  const auto quotient_function = computed_linked
                                     ? computed.materialize("computed.quotient")
                                     : std::nullopt;
  const auto compile_time_operator_function =
      computed_linked ? computed.materialize("computed.compile_time_operator")
                      : std::nullopt;
  const auto compile_time_branch_function =
      computed_linked ? computed.materialize("computed.compile_time_branch")
                      : std::nullopt;
  const auto compile_time_host_function =
      computed_linked ? computed.materialize("computed.compile_time_host")
                      : std::nullopt;
  const auto main_width =
      computed_function &&
              !computed_function->entry().terminator().returned().empty()
          ? computed_function->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const auto packed_width =
      packed_function &&
              !packed_function->entry().terminator().returned().empty()
          ? packed_function->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const auto aligned_width =
      aligned_function &&
              !aligned_function->entry().terminator().returned().empty()
          ? aligned_function->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const std::string computed_text =
      computed_module ? joggle::format(*computed_module) : std::string{};
  const auto compile_time_operator_width =
      compile_time_operator_function && !compile_time_operator_function->entry()
                                             .terminator()
                                             .returned()
                                             .empty()
          ? compile_time_operator_function->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const auto compile_time_branch_width =
      compile_time_branch_function && !compile_time_branch_function->entry()
                                           .terminator()
                                           .returned()
                                           .empty()
          ? compile_time_branch_function->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const auto compile_time_host_width =
      compile_time_host_function && !compile_time_host_function->entry()
                                         .terminator()
                                         .returned()
                                         .empty()
          ? compile_time_host_function->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  if (!computed_function || !packed_function || !aligned_function ||
      !sum_function || !quotient_function || !compile_time_operator_function ||
      !compile_time_branch_function || !compile_time_host_function ||
      !host_double || !main_width || !packed_width || !aligned_width ||
      !compile_time_operator_width || !compile_time_branch_width ||
      !compile_time_host_width) {
    computed.diagnostics().print(std::cerr);
  }
  ok &= expect(
      computed_function && packed_function && aligned_function &&
          sum_function && quotient_function && compile_time_operator_function &&
          main_width && compile_time_branch_function &&
          compile_time_host_function && host_double && packed_width &&
          aligned_width && compile_time_operator_width &&
          compile_time_branch_width && compile_time_host_width &&
          *main_width == 8 && *packed_width == 15 && *aligned_width == 16 &&
          *compile_time_operator_width == 8 &&
          *compile_time_branch_width == 8 && *compile_time_host_width == 14 &&
          sum_function->ops().size() == 1U &&
          sum_function->ops().front().callee().name() == "+" &&
          quotient_function->ops().size() == 1U &&
          quotient_function->ops().front().callee().name() == "//" &&
          computed_text.find("word<W + 1>") != std::string::npos &&
          computed_text.find("word<ceildiv(M * N, 8)>") != std::string::npos &&
          computed_text.find("word<align(W, 8)>") != std::string::npos &&
          computed_text.find("word<@(W // 2)>") != std::string::npos &&
          computed_text.find("return if true { value } else { 1 / 0 };") !=
              std::string::npos &&
          computed_text.find("word<@(W!)>") != std::string::npos &&
          computed_text.find("result = lhs + rhs;") != std::string::npos &&
          computed_text.find("result = lhs // rhs;") != std::string::npos,
      "checked symbolic arithmetic derives result widths and "
      "typed operator notation round-trip canonically");

  joggle::Compiler projected;
  projected.add(R"(
joggle 1;
module formats@1.0.0 {
  type packed(width: int, signed: bool = false) {
    storage_bits: int = width;
    is_signed: bool = signed;
  }

  fn align(value: int, multiple: int) -> int {
    return ceildiv(value, multiple) * multiple;
  }
}
)",
                "formats.joggle");
  projected.add(R"(
joggle 1;
module projected@1.0.0 {
  import formats@1.0.0 as fmt;

  type word(width: int, signed: bool);
  fn encode<T>(input: T)
    -> word<T.storage_bits, T.is_signed>;
  fn align<T>(input: T)
    -> word<fmt.align(T.storage_bits, 8), T.is_signed>;

  fn encode13(input: fmt.packed<13, true>) -> word<13, true> {
    result = encode(input);
    return result;
  }

  fn align13(input: fmt.packed<13, true>) -> word<16, true> {
    result = align(input);
    return result;
  }
}
)",
                "projected.joggle");
  const bool projected_linked = projected.link();
  const auto encode13 = projected_linked
                            ? projected.materialize("projected.encode13")
                            : std::nullopt;
  const auto align13 = projected_linked
                           ? projected.materialize("projected.align13")
                           : std::nullopt;
  const auto format_module = projected.module("formats");
  const std::string format_text =
      format_module ? joggle::format(*format_module) : std::string{};
  if (!encode13 || !align13) {
    projected.diagnostics().print(std::cerr);
  }
  ok &= expect(
      encode13 && align13 &&
          encode13->entry()
                  .terminator()
                  .returned()
                  .front()
                  .type()
                  .get<std::int64_t>("width") ==
              std::optional<std::int64_t>{13} &&
          encode13->entry().terminator().returned().front().type().get<bool>(
              "signed") == std::optional<bool>{true} &&
          align13->entry()
                  .terminator()
                  .returned()
                  .front()
                  .type()
                  .get<std::int64_t>("width") ==
              std::optional<std::int64_t>{16} &&
          align13->entry().terminator().returned().front().type().get<bool>(
              "signed") == std::optional<bool>{true} &&
          format_text.find("storage_bits: int = width;") != std::string::npos &&
          format_text.find("fn align") != std::string::npos,
      "a type-derived parameter deterministically feeds imported "
      "generic result parameters");

  joggle::Compiler missing_field;
  missing_field.add(R"(
joggle 1;
module missing_field@1.0.0 {
  type opaque();
  type word(width: int);
  fn encode<T>(input: T) -> word<T.storage_bits>;
  fn main(input: opaque) -> word<8> {
    result = encode(input);
    return result;
  }
}
)",
                    "missing-field.joggle");
  const bool missing_field_linked = missing_field.link();
  const auto missing_field_function =
      missing_field_linked
          ? missing_field.materialize("missing_field.main")
          : std::nullopt;
  const bool reports_missing_field = std::any_of(
      missing_field.diagnostics().entries().begin(),
      missing_field.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("has no derived parameter") !=
               std::string::npos;
      });
  ok &= expect(missing_field_linked && !missing_field_function &&
                   reports_missing_field,
               "generic computed fields are checked against the bound type");

  joggle::Compiler ill_typed_field;
  ill_typed_field.add(R"(
joggle 1;
module ill_typed_field@1.0.0 {
  type malformed() {
    storage_bits: int = "wide";
  }
}
)",
                      "ill-typed-field.joggle");
  ok &= expect(!ill_typed_field.link() && !ill_typed_field.diagnostics().ok(),
               "computed fields are checked against their declared domains");

  joggle::Compiler recursive_function;
  recursive_function.add(R"(
joggle 1;
module recursive_function@1.0.0 {
  fn first(value: int) -> int {
    return second(value);
  }
  fn second(value: int) -> int {
    return first(value);
  }
}
)",
                         "recursive-function.joggle");
  const bool recursive_linked = recursive_function.link();
  const bool reports_function_cycle =
      std::any_of(recursive_function.diagnostics().entries().begin(),
                  recursive_function.diagnostics().entries().end(),
                  [](const joggle::Diagnostic& diagnostic) {
                    return diagnostic.message.find("pure function cycle") !=
                           std::string::npos;
                  });
  ok &= expect(!recursive_linked && reports_function_cycle,
               "pure function recursion is rejected during linking");

  joggle::Compiler imported_function;
  imported_function.add(R"(
joggle 1;
module integer_math@1.0.0 {
  fn align(value: int, multiple: int) -> int {
    return ceildiv(value, multiple) * multiple;
  }
}
)",
                        "integer-math.joggle");
  imported_function.add(R"(
joggle 1;
module imported_function@1.0.0 {
  import integer_math@1.0.0 as math;

  type word(width: int);

  fn aligned<W: int>(input: word<W>) -> word<math.align(W, 8)>;

  fn main(input: word<9>) -> word<16> {
    result = aligned(input);
    return result;
  }
}
)",
                        "imported-function.joggle");
  const bool imported_function_linked = imported_function.link();
  const auto imported_function_function =
      imported_function_linked
          ? imported_function.materialize("imported_function.main")
          : std::nullopt;
  const auto imported_width =
      imported_function_function && !imported_function_function->entry()
                                         .terminator()
                                         .returned()
                                         .empty()
          ? imported_function_function->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  if (!imported_function_function || !imported_width) {
    imported_function.diagnostics().print(std::cerr);
  }
  ok &= expect(imported_function_function && imported_width &&
                   *imported_width == 16,
               "imported pure functions resolve in their declaring module");

  joggle::Compiler imported_operator;
  imported_operator.add(R"(
joggle 1;
module native_arith@1.0.0 {
  fn (+)<T>(lhs: T, rhs: T) -> T;
}
)",
                        "native-arith.joggle");
  imported_operator.add(R"(
joggle 1;
module imported_operator@1.0.0 {
  import native_arith@1.0.0;

  fn main(lhs: i32, rhs: i32) -> i32 {
    result = lhs + rhs;
    return result;
  }
}
)",
                        "imported-operator.joggle");
  const bool imported_operator_linked = imported_operator.link();
  const auto imported_operator_function =
      imported_operator_linked
          ? imported_operator.materialize("imported_operator.main")
          : std::nullopt;
  if (!imported_operator_function) {
    imported_operator.diagnostics().print(std::cerr);
  }
  ok &= expect(imported_operator_function &&
                   imported_operator_function->ops().size() == 1U &&
                   imported_operator_function->ops()
                           .front()
                           .callee()
                           .symbol()
                           .qualified_name() == "native_arith.+",
               "imported operator notation resolves for native SSA types");

  joggle::Compiler ambiguous_operator;
  ambiguous_operator.add(R"(
joggle 1;
module ambiguous_operator@1.0.0 {
  type word(width: int);

  fn (+)<W: int>(lhs: word<W>, rhs: word<W>) -> word<W>;
  fn (+)<W: int>(lhs: word<W>, rhs: word<W>) -> word<W>;

  fn main(lhs: word<8>, rhs: word<8>) -> word<8> {
    result = lhs + rhs;
    return result;
  }
}
)",
                         "ambiguous-operator.joggle");
  const bool ambiguous_operator_linked = ambiguous_operator.link();
  const auto ambiguous_operator_function =
      ambiguous_operator_linked
          ? ambiguous_operator.materialize("ambiguous_operator.main")
          : std::nullopt;
  const bool reports_operator_ambiguity = std::any_of(
      ambiguous_operator.diagnostics().entries().begin(),
      ambiguous_operator.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("duplicate function overload '+'") !=
               std::string::npos;
      });
  ok &= expect(!ambiguous_operator_linked && !ambiguous_operator_function &&
                   reports_operator_ambiguity,
               "identical symbolic overloads are rejected at declaration");

  joggle::Compiler unsafe_expression;
  unsafe_expression.add(R"(
joggle 1;
module unsafe_expression@1.0.0 {
  type word(width: int);
  fn invalid<W: int>(input: word<W>) -> word<W / 0>;
  fn main(input: word<8>) {
    result = invalid(input);
    return;
  }
}
)",
                        "unsafe-expression.joggle");
  const bool unsafe_linked = unsafe_expression.link();
  const auto unsafe_function =
      unsafe_linked ? unsafe_expression.materialize("unsafe_expression.main")
                    : std::nullopt;
  const bool reports_division_by_zero = std::any_of(
      unsafe_expression.diagnostics().entries().begin(),
      unsafe_expression.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("division by zero") != std::string::npos;
      });
  ok &= expect(unsafe_linked && !unsafe_function && reports_division_by_zero,
               "non-total compile-time arithmetic is rejected when its "
               "bindings become concrete");

  joggle::Compiler dynamic_at;
  dynamic_at.add(R"(
joggle 1;
module dynamic_at@1.0.0 {
  fn invalid(input: i32) -> i32 {
    forced = @(input);
    return input;
  }
}
)",
                 "dynamic-at.joggle");
  const bool dynamic_at_linked = dynamic_at.link();
  const auto dynamic_at_function =
      dynamic_at_linked ? dynamic_at.materialize("dynamic_at.invalid")
                        : std::nullopt;
  const bool reports_dynamic_at =
      std::any_of(dynamic_at.diagnostics().entries().begin(),
                  dynamic_at.diagnostics().entries().end(),
                  [](const joggle::Diagnostic& diagnostic) {
                    return diagnostic.message.find("compile-time evaluation") !=
                           std::string::npos;
                  });
  ok &= expect(dynamic_at_linked && !dynamic_at_function && reports_dynamic_at,
               "@ rejects a Residual value instead of changing its stage");

  joggle::Compiler guarded_host;
  guarded_host.add(R"(
joggle 1;
module guarded_host@1.0.0 {
  fn observe(value: int) -> int;
  fn invalid(condition: i1) {
    selected = if condition { @observe(1) } else { @observe(2) };
    return;
  }
}
)",
                   "guarded-host.joggle");
  const bool guarded_host_linked = guarded_host.link();
  const auto guarded_host_module = guarded_host.module("guarded_host");
  const auto observe = guarded_host_module
                           ? guarded_host_module->function("observe")
                           : std::nullopt;
  std::int64_t observations = 0;
  if (observe) {
    guarded_host.bind(*observe, [&](std::int64_t value) {
      ++observations;
      return value;
    });
  }
  const auto guarded_host_function =
      guarded_host_linked && observe
          ? guarded_host.materialize("guarded_host.invalid")
          : std::nullopt;
  const bool reports_guarded_host = std::any_of(
      guarded_host.diagnostics().entries().begin(),
      guarded_host.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find(
                   "guarded and cannot execute under Residual control") !=
               std::string::npos;
      });
  ok &= expect(!guarded_host_function && observations == 0 &&
                   reports_guarded_host,
               "Residual branches never speculatively execute host code");

  joggle::Compiler hermetic_host;
  hermetic_host.add(R"(
joggle 1;
module hermetic_host@1.0.0 {
  fn evaluate(value: int) -> int;
  fn valid(condition: i1) {
    if condition {
      selected = @evaluate(1);
    } else {
      selected = @evaluate(2);
    }
    return;
  }
}
)",
                    "hermetic-host.joggle");
  const bool hermetic_host_linked = hermetic_host.link();
  const auto hermetic_host_module = hermetic_host.module("hermetic_host");
  const auto hermetic_evaluate =
      hermetic_host_module ? hermetic_host_module->function("evaluate")
                           : std::nullopt;
  if (hermetic_evaluate) {
    hermetic_host.bind(
        *hermetic_evaluate, [](std::int64_t value) { return value; },
        joggle::HostEvaluation::Hermetic);
  }
  const auto hermetic_host_function =
      hermetic_host_linked && hermetic_evaluate
          ? hermetic_host.materialize("hermetic_host.valid")
          : std::nullopt;
  ok &= expect(hermetic_host_function && hermetic_host.ok(),
               "a binding explicitly promised Hermetic may evaluate beneath "
               "Residual control");

  joggle::Compiler list_evaluation;
  list_evaluation.add(R"(
joggle 1;
module list_evaluation@1.0.0 {
  type word(width: int);

  fn source<T>() -> T;
  fn sum(values: list<int>) -> int;

  fn populated() -> word<6> {
    value: word<@(sum([1, 2, 3]))> = source();
    return value;
  }

  fn empty() -> word<0> {
    value: word<@(sum([]))> = source();
    return value;
  }
}
)",
                      "list-evaluation.joggle");
  const bool list_evaluation_linked = list_evaluation.link();
  const auto list_evaluation_module = list_evaluation.module("list_evaluation");
  const std::string list_evaluation_text =
      list_evaluation_module ? joggle::format(*list_evaluation_module)
                             : std::string{};
  joggle::Diagnostics list_evaluation_roundtrip_diagnostics;
  const auto list_evaluation_roundtrip =
      list_evaluation_module
          ? joggle::parse_module(list_evaluation_text,
                                 list_evaluation_roundtrip_diagnostics,
                                 "list-evaluation-roundtrip.joggle")
          : std::nullopt;
  const auto list_sum = list_evaluation_module
                            ? list_evaluation_module->function("sum")
                            : std::nullopt;
  if (list_sum) {
    list_evaluation.bind(*list_sum,
                         [](const std::vector<std::int64_t>& values) {
                           std::int64_t result = 0;
                           for (const std::int64_t value : values) {
                             result += value;
                           }
                           return result;
                         });
  }
  const auto populated =
      list_evaluation_linked && list_sum
          ? list_evaluation.materialize("list_evaluation.populated")
          : std::nullopt;
  const auto empty = list_evaluation_linked && list_sum
                         ? list_evaluation.materialize("list_evaluation.empty")
                         : std::nullopt;
  if (!populated || !empty) {
    list_evaluation.diagnostics().print(std::cerr);
  }
  const auto populated_width =
      populated ? populated->result_types().front().get<std::int64_t>("width")
                : std::nullopt;
  const auto empty_width =
      empty ? empty->result_types().front().get<std::int64_t>("width")
            : std::nullopt;
  ok &= expect(
      populated_width == std::optional<std::int64_t>{6} &&
          empty_width == std::optional<std::int64_t>{0} &&
          list_evaluation_roundtrip &&
          list_evaluation_roundtrip_diagnostics.ok() &&
          joggle::format(*list_evaluation_roundtrip) == list_evaluation_text &&
          list_evaluation_text.find(
              "value: word<@sum([1, 2, 3])> = source();") != std::string::npos,
      "local type annotations use the full compile-time expression "
      "grammar, including list-valued host functions and empty "
      "lists");

  constexpr std::string_view staged_control_source = R"(
joggle 1;
module staged_control@1.0.0 {
  type word(width: int);

  fn identity<T>(input: T) -> T;
  fn (+)(lhs: int, rhs: int) -> int;
  fn (<)(lhs: int, rhs: int) -> bool;
  fn (>)(lhs: int, rhs: int) -> bool;
  fn literal<T>(value: int) -> T ;
  fn (+)(lhs: index, rhs: index) -> index;
  fn (<)(lhs: index, rhs: index) -> i1;
  fn touch(input: i32, position: index) -> i32;

  fn sum_shape<S: list<int>>(shape: S) -> int {
    total = 0;
    for dimension in S {
      if @(dimension > 1) {
        total = @(total + dimension);
      } else {
        continue;
      }
      if @(total > 10) {
        break;
      }
    }
    return total;
  }

  fn count<N: int>(limit: N) -> int {
    current = 0;
    while @(current < N) {
      current = @(current + 1);
    }
    return current;
  }

  fn specialize<N: int>(width: N, input: word<N>) -> word<N> {
    if @(N > 4) {
      output = identity(input);
      return output;
    }
    return input;
  }

  fn pipeline<S: list<int>>(stages: S, input: word<8>) -> word<8> {
    current = input;
    for stage in S {
      if @(stage > 0) {
        current = identity(current);
      }
    }
    return current;
  }

  fn residual_count<N: int>(count: N, input: i32) -> i32 {
    current = input;
    for position: index in @range(N) {
      current = touch(current, position);
    }
    return current;
  }

  fn residual_control<N: int>(count: N, input: i32, stop: i1, skip: i1)
      -> i32 {
    current = input;
    for position: index in @range(N) {
      if skip {
        continue;
      }
      current = touch(current, position);
      if stop {
        break;
      }
    }
    return current;
  }

  fn materialize_index<N: int>(value: N) -> index {
    result: index = N;
    return result;
  }
}
)";
  joggle::Diagnostics staged_control_parse_diagnostics;
  const auto staged_control_module = joggle::parse_module(
      staged_control_source, staged_control_parse_diagnostics,
      "staged-control.joggle");
  const std::string staged_control_text =
      staged_control_module ? joggle::format(*staged_control_module)
                            : std::string{};
  joggle::Diagnostics staged_control_roundtrip_diagnostics;
  const auto staged_control_roundtrip =
      staged_control_module
          ? joggle::parse_module(staged_control_text,
                                 staged_control_roundtrip_diagnostics,
                                 "staged-control-roundtrip.joggle")
          : std::nullopt;

  joggle::Compiler staged_control;
  staged_control.add(staged_control_source, "staged-control.joggle");
  const bool staged_control_linked = staged_control.link();
  const auto staged_module = staged_control.module("staged_control");
  const auto staged_integer_operator =
      [&](std::string_view symbol)
      -> std::optional<joggle::Module::FunctionDecl> {
    if (!staged_module) {
      return std::nullopt;
    }
    const auto overloads = staged_module->overloads(symbol);
    const auto found = std::find_if(
        overloads.begin(), overloads.end(), [](const auto& candidate) {
          return !candidate.inputs().empty() &&
                 candidate.inputs().front().domain ==
                     joggle::Module::Expression::reference("int");
        });
    return found == overloads.end()
               ? std::optional<joggle::Module::FunctionDecl>{}
               : std::optional<joggle::Module::FunctionDecl>{*found};
  };
  const auto integer_add = staged_integer_operator("+");
  const auto integer_less = staged_integer_operator("<");
  const auto integer_greater = staged_integer_operator(">");
  if (integer_add) {
    staged_control.bind(*integer_add, [](std::int64_t lhs, std::int64_t rhs) {
      return lhs + rhs;
    });
  }
  if (integer_less) {
    staged_control.bind(*integer_less, [](std::int64_t lhs, std::int64_t rhs) {
      return lhs < rhs;
    });
  }
  if (integer_greater) {
    staged_control.bind(
        *integer_greater,
        [](std::int64_t lhs, std::int64_t rhs) { return lhs > rhs; });
  }
  const auto sum_shape = staged_control_linked
                             ? staged_control.run<std::int64_t>(
                                   "staged_control.sum_shape",
                                   std::vector<std::int64_t>{1, 2, 4, 8})
                             : std::nullopt;
  const auto count = staged_control_linked
                         ? staged_control.run<std::int64_t>(
                               "staged_control.count", std::int64_t{3})
                         : std::nullopt;
  const auto specialize_decl =
      staged_module ? staged_module->function("specialize") : std::nullopt;
  const auto pipeline_decl =
      staged_module ? staged_module->function("pipeline") : std::nullopt;
  const auto residual_count_decl =
      staged_module ? staged_module->function("residual_count") : std::nullopt;
  const auto residual_control_decl =
      staged_module ? staged_module->function("residual_control")
                    : std::nullopt;
  const auto materialize_index_decl =
      staged_module ? staged_module->function("materialize_index")
                    : std::nullopt;
  const auto integer_type = staged_control.make("int");
  const auto prelude_module = staged_control.module("prelude");
  const auto list_decl =
      prelude_module ? prelude_module->type("list") : std::nullopt;
  const auto integer_list_type =
      list_decl && integer_type ? staged_control.make(*list_decl, *integer_type)
                                : std::nullopt;
  const auto width = integer_type
                         ? staged_control.known(*integer_type, std::int64_t{8})
                         : std::nullopt;
  const auto stages =
      integer_list_type
          ? staged_control.known(*integer_list_type,
                                 std::vector<std::int64_t>{1, 0, 2})
          : std::nullopt;
  const auto specialized =
      specialize_decl && width
          ? staged_control.materialize(*specialize_decl, {*width})
          : std::nullopt;
  const auto pipeline =
      pipeline_decl && stages
          ? staged_control.materialize(*pipeline_decl, {*stages})
          : std::nullopt;
  const auto residual_count =
      residual_count_decl && width
          ? staged_control.materialize(*residual_count_decl, {*width})
          : std::nullopt;
  const auto zero = integer_type
                        ? staged_control.known(*integer_type, std::int64_t{0})
                        : std::nullopt;
  const auto empty_residual_count =
      residual_count_decl && zero
          ? staged_control.materialize(*residual_count_decl, {*zero})
          : std::nullopt;
  const auto residual_control =
      residual_control_decl && width
          ? staged_control.materialize(*residual_control_decl, {*width})
          : std::nullopt;
  const auto materialized_index =
      materialize_index_decl && width
          ? staged_control.materialize(*materialize_index_decl, {*width})
          : std::nullopt;
  if (!staged_control_linked || !specialized || !pipeline ||
      !residual_count || !empty_residual_count || !residual_control ||
      !materialized_index) {
    staged_control.diagnostics().print(std::cerr);
  }
  ok &= expect(
      staged_control_module && staged_control_roundtrip &&
          staged_control_parse_diagnostics.ok() &&
          staged_control_roundtrip_diagnostics.ok() &&
          joggle::format(*staged_control_roundtrip) == staged_control_text &&
          staged_control_text.find("for dimension in S {") !=
              std::string::npos &&
          staged_control_text.find(
              "for position: index in @range(N) {") != std::string::npos &&
          sum_shape == std::optional<std::int64_t>{14} &&
          count == std::optional<std::int64_t>{3} && specialized && pipeline &&
          residual_count && staged_control.verify(*residual_count) &&
          residual_count->blocks().size() == 5U &&
          residual_count->ops().size() == 6U &&
          empty_residual_count &&
          staged_control.verify(*empty_residual_count) &&
          empty_residual_count->blocks().size() == 1U &&
          empty_residual_count->ops().empty() && residual_control &&
          staged_control.verify(*residual_control) &&
          materialized_index && staged_control.verify(*materialized_index) &&
          materialized_index->ops().size() == 1U &&
          materialized_index->result_types().front().schema().name() ==
              "index" &&
          specialized->arguments().front().type().get<std::int64_t>("width") ==
              std::optional<std::int64_t>{8} &&
          specialized->ops().size() == 1U &&
          pipeline->ops().size() == 2U,
      "generic bindings are ordinary Known locals that drive if, while, for, "
      "dependent types, and deterministic residual expansion");

  joggle::Compiler explicit_staging;
  explicit_staging.add(R"(
joggle 1;
module explicit_staging@1.0.0 {
  fn literal<T>(value: int) -> T;
  fn twice(value: int) -> int;

  fn staged() -> i32 {
    value: i32 = @twice(2);
    return value;
  }

  fn missing_stage() {
    value = twice(2);
    return;
  }
}
)",
                       "explicit-staging.joggle");
  const bool explicit_staging_linked = explicit_staging.link();
  const auto explicit_staging_module =
      explicit_staging.module("explicit_staging");
  const auto twice_decl =
      explicit_staging_module ? explicit_staging_module->function("twice")
                              : std::nullopt;
  std::size_t evaluations = 0;
  if (twice_decl) {
    explicit_staging.bind(*twice_decl, [&](std::int64_t value) {
      ++evaluations;
      return value * 2;
    });
  }
  const auto staged = explicit_staging_linked && twice_decl
                          ? explicit_staging.materialize(
                                "explicit_staging.staged")
                          : std::nullopt;
  if (!staged) {
    explicit_staging.diagnostics().print(std::cerr);
  }
  ok &= expect(explicit_staging_linked && staged &&
                   explicit_staging.verify(*staged) &&
                   staged->ops().size() == 1U && evaluations == 1U,
               "@ explicitly evaluates a compiler-domain call and "
               "materializes its result only at a Residual boundary");

  const auto missing_stage =
      explicit_staging_linked
          ? explicit_staging.materialize("explicit_staging.missing_stage")
          : std::nullopt;
  const bool reports_missing_stage = std::any_of(
      explicit_staging.diagnostics().entries().begin(),
      explicit_staging.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("requires explicit @ evaluation") !=
               std::string::npos;
      });
  if (missing_stage || !reports_missing_stage) {
    explicit_staging.diagnostics().print(std::cerr);
  }
  ok &= expect(!missing_stage && reports_missing_stage && evaluations == 1U,
               "an ordinary compiler-domain call neither evaluates nor "
               "silently changes stage");

  const joggle::Compiler::EvaluationLimits limits{2, 64};
  joggle::Compiler bounded(limits);
  bounded.add(R"(
joggle 1;
module bounded@1.0.0 {
  type word(width: int);
  fn source() -> word<1 + 2>;
  fn main() -> word<3> {
    return source();
  }
}
)",
              "bounded.joggle");
  const bool bounded_linked = bounded.link();
  const auto bounded_function =
      bounded_linked ? bounded.materialize("bounded.main") : std::nullopt;
  const bool reports_step_limit =
      std::any_of(bounded.diagnostics().entries().begin(),
                  bounded.diagnostics().entries().end(),
                  [](const joggle::Diagnostic& diagnostic) {
                    return diagnostic.message.find(
                               "compile-time evaluation step limit exceeded") !=
                           std::string::npos;
                  });
  ok &= expect(bounded.evaluation_limits().steps == limits.steps &&
                   bounded.evaluation_limits().depth == limits.depth &&
                   bounded_linked && !bounded_function && reports_step_limit,
               "compile-time evaluation obeys deterministic resource limits");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
