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

mod logic@1.0.0 {
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
  fn generate<T>(body: (word<8>) -> T) -> tensor<T, [4]>;
  fn reduce<T, S: list<int>, A>(
    input: tensor<T, S>, initial: A, body: (A, T) -> A
  ) -> A;
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
  fn invoke(input: word<8>, body: (word<8>) -> word<16>) -> word<16> {
    return body(input);
  }
  fn callback(input: word<8>) -> word<16>;
  fn apply_fixed(input: word<8>, body: (word<8>) -> word<16>) -> word<16>;
  fn inline_callback(input: word<8>) -> word<16> {
    return apply_fixed(input, (value: word<8>) => callback(value));
  }
  fn generic_inline_callback(input: word<8>) -> word<16> {
    return apply(input, (value: word<8>) => callback(value));
  }
  fn nested_inline_callback(input: word<8>) -> word<16> {
    return apply_fixed(
      input,
      (outer: word<8>) => apply_fixed(
        outer,
        (inner: word<8>) => callback(inner)
      )
    );
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
  fn nested_result_inference(input: word<8>, initial: word<8>) -> word<8> {
    return reduce(
      generate((value: word<8>) -> word<8> => value),
      initial,
      (sum: word<8>, value: word<8>) -> word<8> => sum
    );
  }

}
)";

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

std::optional<joggle::Fn> load_fn(joggle::Compiler& compiler,
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
  const auto fn = load_fn(compiler, source);
  if (!fn) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto mod = compiler.mod("logic");
  const auto operations = fn->ops();
  bool ok = true;
  ok &= expect(mod.has_value(), "the fn owner remains available");
  const auto configured_decl = mod ? mod->fn("configured") : std::nullopt;
  const auto default_configured_decl =
      mod ? mod->fn("default_configured") : std::nullopt;
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
  const auto invoke = compiler.materialize("logic.invoke");
  const auto inline_callback = compiler.materialize("logic.inline_callback");
  const auto generic_inline_callback =
      compiler.materialize("logic.generic_inline_callback");
  const auto nested_inline_callback =
      compiler.materialize("logic.nested_inline_callback");
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
  const auto nested_result_inference =
      compiler.materialize("logic.nested_result_inference");
  const auto callback_arguments =
      callback_user ? callback_user->arguments() : std::vector<joggle::Val>{};
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
      "a fn signature has one public parameter sequence while "
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
               "fn type syntax constructs a reflected callable type "
               "and participates in generic call inference");
  ok &= expect(invoke && invoke->ops().size() == 1U &&
                   invoke->ops().front().callee() == invoke->arguments()[1] &&
                   !invoke->ops().front().callee().referenced_fn() &&
                   invoke->ops().front().arguments() ==
                       std::vector<joggle::Val>{invoke->arguments()[0]} &&
                   joggle::format(*invoke, "invoke").find("arg1(arg0)") !=
                       std::string::npos,
               "a fn parameter is called through the same Call/callee Val "
               "representation and source form as a declared fn");
  const auto inline_ops =
      inline_callback ? inline_callback->ops() : std::vector<joggle::Op>{};
  const auto inline_arguments = inline_ops.size() == 1U
                                    ? inline_ops.front().arguments()
                                    : std::vector<joggle::Val>{};
  const auto inline_body = inline_arguments.size() == 2U
                               ? inline_arguments.back().inline_fn()
                               : std::optional<joggle::Fn>{};
  ok &=
      expect(inline_callback && inline_ops.size() == 1U &&
                 inline_arguments.size() == 2U && inline_body &&
                 !inline_arguments.back().referenced_fn() &&
                 inline_body->arguments().size() == 1U &&
                 inline_body->ops().size() == 1U &&
                 inline_body->ops().front().callee().referenced_fn()->name() ==
                     "callback",
             "a typed lambda materializes through the ordinary expression "
             "and callable Val path");
  ok &= expect(
      generic_inline_callback && generic_inline_callback->ops().size() == 1U &&
          generic_inline_callback->ops().front().arguments().back().inline_fn(),
      "lambda annotations and the surrounding result infer a "
      "generic higher-order call");
  const auto nested_result_ops = nested_result_inference
                                     ? nested_result_inference->ops()
                                     : std::vector<joggle::Op>{};
  ok &= expect(
      nested_result_ops.size() == 2U &&
          nested_result_ops[0].callee().referenced_fn() &&
          nested_result_ops[0].callee().referenced_fn()->name() == "generate" &&
          nested_result_ops[1].callee().referenced_fn() &&
          nested_result_ops[1].callee().referenced_fn()->name() == "reduce" &&
          compiler.verify(*nested_result_inference),
      "an annotated lambda lets a nested call contribute its result Type "
      "to the enclosing generic call");
  const auto overloaded_inline_arguments =
      overloaded_inline_callback &&
              overloaded_inline_callback->ops().size() == 1U
          ? overloaded_inline_callback->ops().front().arguments()
          : std::vector<joggle::Val>{};
  const auto overloaded_inline_body =
      overloaded_inline_arguments.size() == 2U
          ? overloaded_inline_arguments.back().inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(
      overloaded_inline_callback && overloaded_inline_body &&
          overloaded_inline_body->arguments().front().type().get<std::int64_t>(
              "width") == std::optional<std::int64_t>{8},
      "lambda parameter annotations select a higher-order overload");
  const auto nested_arguments =
      nested_inline_callback && nested_inline_callback->ops().size() == 1U
          ? nested_inline_callback->ops().front().arguments()
          : std::vector<joggle::Val>{};
  const auto nested_body = nested_arguments.size() == 2U
                               ? nested_arguments.back().inline_fn()
                               : std::optional<joggle::Fn>{};
  const auto nested_body_arguments =
      nested_body && nested_body->ops().size() == 1U
          ? nested_body->ops().front().arguments()
          : std::vector<joggle::Val>{};
  ok &= expect(nested_body && nested_body_arguments.size() == 2U &&
                   nested_body_arguments.back().inline_fn(),
               "an inline Fn may contain another typed callable value");
  const std::string inline_text =
      nested_inline_callback
          ? joggle::format(*nested_inline_callback, "compiled_inline")
          : std::string{};
  joggle::Compiler inline_roundtrip;
  inline_roundtrip.add(source, "logic.joggle");
  inline_roundtrip.add("joggle 1;\nmod inline_artifact@1.0.0 {\n"
                       "  import logic@1;\n" +
                           inline_text + "}\n",
                       "inline-artifact.joggle");
  const bool inline_roundtrip_linked = inline_roundtrip.link();
  const auto replayed_inline =
      inline_roundtrip_linked
          ? inline_roundtrip.materialize("inline_artifact.compiled_inline")
          : std::nullopt;
  if (!replayed_inline) {
    inline_roundtrip.diag().print(std::cerr);
  }
  ok &= expect(inline_text.find("=>") != std::string::npos && replayed_inline &&
                   inline_roundtrip.verify(*replayed_inline),
               "typed lambda formatting is canonical and materializes after "
               "a source round trip");

  joggle::Compiler guarded_inference;
  guarded_inference.add(R"(
joggle 1;
mod guarded_inference@1.0.0 {
  type word();
  type tensor(element: type, shape: list<int>);

  fn shape() -> list<int>;
  fn generate<T, S: list<int>>(
    shape: S, body: (word) -> T
  ) -> tensor<T, S>;
  fn reduce<T, S: list<int>, A>(
    input: tensor<T, S>, initial: A, body: (A, T) -> A
  ) -> A;

  fn staged(input: word, initial: word) -> word {
    shape = @shape();
    return reduce(
      generate(shape, (value: word) -> word => value),
      initial,
      (sum: word, value: word) -> word => sum
    );
  }

  fn direct(input: word, initial: word) -> word {
    return reduce(
      generate(@shape(), (value: word) -> word => value),
      initial,
      (sum: word, value: word) -> word => sum
    );
  }
}
)",
                        "guarded-inference.joggle");
  const bool guarded_linked = guarded_inference.link();
  const auto guarded_mod = guarded_inference.mod("guarded_inference");
  const auto shape_decl = guarded_mod ? guarded_mod->fn("shape") : std::nullopt;
  std::size_t shape_calls = 0;
  if (shape_decl) {
    guarded_inference.bind(*shape_decl, [&] {
      ++shape_calls;
      return std::vector<std::int64_t>{4};
    });
  }
  const auto staged_nested =
      guarded_linked && shape_decl
          ? guarded_inference.materialize("guarded_inference.staged")
          : std::nullopt;
  const auto direct_nested =
      staged_nested ? guarded_inference.materialize("guarded_inference.direct")
                    : std::nullopt;
  ok &= expect(
      staged_nested && staged_nested->ops().size() == 2U && !direct_nested &&
          shape_calls == 1U,
      "nested Type inference uses an explicitly bound Known value but never "
      "speculatively executes a guarded native compiler fn");

  joggle::Compiler invalid_lambda;
  invalid_lambda.add(R"(
joggle 1;
mod invalid_lambda@1.0.0 {
  type word(width: int);
  fn apply(input: word<8>, body: (word<8>) -> word<16>) -> word<16>;
  fn callback(input: word<8>) -> word<16>;
  fn identity(input: word<8>) -> word<8>;

  fn captures(input: word<8>) -> word<16> {
    return apply(input, (value: word<8>) => callback(input));
  }

  fn captures_local(input: word<8>) -> word<16> {
    local = identity(input);
    return apply(input, (value: word<8>) => callback(local));
  }

  fn mismatched(input: word<8>) -> word<16> {
    return apply(input, (value: word<16>) => callback(value));
  }
}
)",
                     "invalid-lambda.joggle");
  const bool invalid_lambda_linked = invalid_lambda.link();
  const auto captured =
      invalid_lambda_linked
          ? invalid_lambda.materialize("invalid_lambda.captures")
          : std::nullopt;
  const auto mismatched =
      invalid_lambda_linked
          ? invalid_lambda.materialize("invalid_lambda.mismatched")
          : std::nullopt;
  const auto captured_local =
      invalid_lambda_linked
          ? invalid_lambda.materialize("invalid_lambda.captures_local")
          : std::nullopt;
  const bool reports_mismatch = std::any_of(
      invalid_lambda.diag().issues().begin(),
      invalid_lambda.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("inline fn") != std::string::npos &&
               diagnostic.message.find("does not match") != std::string::npos;
      });
  const auto capture_call =
      captured && captured->ops().size() == 1U
          ? std::optional<joggle::Op>{captured->ops().front()}
          : std::nullopt;
  const auto capture_arguments =
      capture_call ? capture_call->arguments() : std::vector<joggle::Val>{};
  const auto closure = capture_arguments.size() == 2U
                           ? std::optional<joggle::Val>{capture_arguments[1]}
                           : std::nullopt;
  const auto closure_captures =
      closure ? closure->captures() : std::vector<joggle::Val>{};
  const auto closure_body =
      closure ? closure->inline_fn() : std::optional<joggle::Fn>{};
  if (!captured || mismatched || !reports_mismatch) {
    invalid_lambda.diag().print(std::cerr);
  }
  ok &= expect(
      invalid_lambda_linked && captured && !mismatched && reports_mismatch &&
          closure && closure_captures.size() == 1U &&
          closure_captures.front() == captured->arguments().front() &&
          closure_body && closure_body->arguments().size() == 2U &&
          closure_body->ops().size() == 1U &&
          closure_body->ops().front().arguments() ==
              std::vector<joggle::Val>{closure_body->arguments()[1]},
      "typed lambdas close over Residual values with explicit capture edges "
      "and still reject mismatched parameter annotations");
  const auto local_ops =
      captured_local ? captured_local->ops() : std::vector<joggle::Op>{};
  const auto local_closure =
      local_ops.size() == 2U && local_ops.back().arguments().size() == 2U
          ? std::optional<joggle::Val>{local_ops.back().arguments().back()}
          : std::nullopt;
  ok &= expect(local_closure && local_closure->captures().size() == 1U &&
                   local_closure->captures().front().defining_op() ==
                       std::optional<joggle::Op>{local_ops.front()} &&
                   captured_local->users(local_ops.front().value()) ==
                       std::vector<joggle::Op>{local_ops.back()},
               "a closure may capture a dominating local result without "
               "turning it into hidden syntax");
  const std::string capture_text =
      captured ? joggle::format(*captured, "captured") : std::string{};
  const std::string local_capture_text =
      captured_local ? joggle::format(*captured_local, "captured_local")
                     : std::string{};
  joggle::Compiler capture_replay;
  capture_replay.add(R"(
joggle 1;
mod invalid_lambda@1.0.0 {
  type word(width: int);
  fn callback(input: word<8>) -> word<16>;
  fn apply(input: word<8>, body: (word<8>) -> word<16>) -> word<16>;
  fn identity(input: word<8>) -> word<8>;
}
)",
                     "capture-defs.joggle");
  capture_replay.add("joggle 1;\nmod capture_artifact@1.0.0 {\n"
                     "  import invalid_lambda@1;\n" +
                         capture_text + local_capture_text + "}\n",
                     "capture-artifact.joggle");
  const bool capture_replay_linked = capture_replay.link();
  const auto replayed_capture =
      capture_replay_linked
          ? capture_replay.materialize("capture_artifact.captured")
          : std::optional<joggle::Fn>{};
  const auto replayed_local_capture =
      capture_replay_linked
          ? capture_replay.materialize("capture_artifact.captured_local")
          : std::optional<joggle::Fn>{};
  const auto replayed_arguments =
      replayed_capture && replayed_capture->ops().size() == 1U
          ? replayed_capture->ops().front().arguments()
          : std::vector<joggle::Val>{};
  const auto replayed_closure =
      replayed_arguments.size() == 2U
          ? std::optional<joggle::Val>{replayed_arguments[1]}
          : std::nullopt;
  if (!replayed_capture) {
    capture_replay.diag().print(std::cerr);
  }
  ok &= expect(capture_text.find("=>") != std::string::npos &&
                   replayed_capture && replayed_closure &&
                   replayed_closure->captures().size() == 1U &&
                   replayed_local_capture &&
                   replayed_local_capture->ops().size() == 2U &&
                   capture_replay.verify(*replayed_capture),
               "capturing lambdas preserve argument and local-result closure "
               "edges through canonical source round trips");

  joggle::Compiler ambiguous_lambda;
  ambiguous_lambda.add(R"(
joggle 1;
mod ambiguous_lambda@1.0.0 {
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
  const bool reports_lambda_ambiguity =
      std::any_of(ambiguous_lambda.diag().issues().begin(),
                  ambiguous_lambda.diag().issues().end(),
                  [](const joggle::Issue& diagnostic) {
                    return diagnostic.message.find("ambiguous between") !=
                           std::string::npos;
                  });
  ok &= expect(!ambiguous_lambda_body && reports_lambda_ambiguity,
               "a lambda remains ambiguous when annotations and surrounding "
               "result context cannot select one overload");

  joggle::Diag duplicate_lambda_diagnostics;
  const auto duplicate_lambda = joggle::parse_mod(R"(
joggle 1;
mod duplicate_lambda@1.0.0 {
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
  ok &=
      expect(generic_body_user && generic_body_call && generic_body &&
                 compiler.verify(*generic_body) &&
                 generic_body->arguments().size() == 1U &&
                 generic_body->result_types().size() == 1U &&
                 generic_body->arguments().front().type() ==
                     generic_body->result_types().front() &&
                 generic_body->arguments().front().type().get<std::int64_t>(
                     "width") == std::optional<std::int64_t>{8} &&
                 generic_body->ops().size() == 1U &&
                 generic_body->ops().front().callee().referenced_fn()->name() ==
                     "identity",
             "a concrete typed call recovers generic bindings and "
             "materializes its source-defined callee body");
  const auto callback_operations =
      callback_value ? callback_value->ops() : std::vector<joggle::Op>{};
  const auto applied_arguments = callback_operations.size() == 1U
                                     ? callback_operations.front().arguments()
                                     : std::vector<joggle::Val>{};
  const auto referenced = applied_arguments.size() == 2U
                              ? applied_arguments[1].referenced_fn()
                              : std::optional<joggle::Mod::FnDecl>{};
  ok &=
      expect(callback_value && callback_operations.size() == 1U && referenced &&
                 referenced->symbol().qualified_name() == "logic.callback" &&
                 applied_arguments[1].type().schema().name() == "callable",
             "a named fn is a typed mod value without a region "
             "or wrapper op");
  const std::string callback_text =
      callback_value ? joggle::format(*callback_value, "compiled_callback")
                     : "";
  joggle::Compiler callback_compiler;
  callback_compiler.add(source, "logic.joggle");
  callback_compiler.add("joggle 1;\nmod callback_artifact@1.0.0 {\n"
                        "  import logic@1;\n" +
                            callback_text + "}\n",
                        "callback-artifact.joggle");
  const bool callback_linked = callback_compiler.link();
  const auto replayed_callback =
      callback_linked
          ? callback_compiler.materialize("callback_artifact.compiled_callback")
          : std::optional<joggle::Fn>{};
  ok &= expect(callback_text.find("logic.callback") != std::string::npos &&
                   replayed_callback &&
                   joggle::format(*replayed_callback, "compiled_callback") ==
                       callback_text,
               "named fn values format and instantiate canonically");
  const auto generic_arguments =
      generic_callback_value && !generic_callback_value->ops().empty()
          ? generic_callback_value->ops().front().arguments()
          : std::vector<joggle::Val>{};
  const auto overloaded_arguments =
      overloaded_callback_value && !overloaded_callback_value->ops().empty()
          ? overloaded_callback_value->ops().front().arguments()
          : std::vector<joggle::Val>{};
  const auto generic_reference = generic_arguments.size() == 2U
                                     ? generic_arguments[1].referenced_fn()
                                     : std::optional<joggle::Mod::FnDecl>{};
  const auto overloaded_reference =
      overloaded_arguments.size() == 2U
          ? overloaded_arguments[1].referenced_fn()
          : std::optional<joggle::Mod::FnDecl>{};
  ok &= expect(generic_reference &&
                   generic_reference->name() == "generic_callback" &&
                   overloaded_reference &&
                   overloaded_reference->signature().find("word<8>") !=
                       std::string::npos,
               "a callable annotation resolves generic and overloaded "
               "fn values contextually");
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
  contextual_compiler.add("joggle 1;\nmod contextual_artifact@1.0.0 {\n"
                          "  import logic@1;\n" +
                              generic_text + overloaded_text + "}\n",
                          "contextual-artifact.joggle");
  const bool contextual_linked = contextual_compiler.link();
  const auto replayed_generic =
      contextual_linked
          ? contextual_compiler.materialize("contextual_artifact.generic_value")
          : std::optional<joggle::Fn>{};
  const auto replayed_overloaded =
      contextual_linked ? contextual_compiler.materialize(
                              "contextual_artifact.overloaded_value")
                        : std::optional<joggle::Fn>{};
  ok &= expect(replayed_generic && replayed_overloaded &&
                   joggle::format(*replayed_generic, "generic_value") ==
                       generic_text &&
                   joggle::format(*replayed_overloaded, "overloaded_value") ==
                       overloaded_text,
               "context-selected fn values preserve their callable "
               "type across canonical serialization");
  const auto direct_generic_arguments =
      direct_generic_callback_value &&
              direct_generic_callback_value->ops().size() == 1U
          ? direct_generic_callback_value->ops().front().arguments()
          : std::vector<joggle::Val>{};
  const auto direct_overloaded_arguments =
      direct_overloaded_callback_value &&
              direct_overloaded_callback_value->ops().size() == 1U
          ? direct_overloaded_callback_value->ops().front().arguments()
          : std::vector<joggle::Val>{};
  ok &= expect(
      direct_generic_arguments.size() == 2U &&
          direct_generic_arguments[1].referenced_fn() &&
          direct_overloaded_arguments.size() == 2U &&
          direct_overloaded_arguments[1].referenced_fn() &&
          direct_overloaded_arguments[1].referenced_fn()->signature().find(
              "word<8>") != std::string::npos,
      "a higher-order call propagates its inferred callable type "
      "into generic and overloaded fn arguments");
  const std::string mod_text = mod ? joggle::format(*mod) : "";
  joggle::Diag mod_roundtrip_diagnostics;
  const auto mod_roundtrip = joggle::parse_mod(
      mod_text, mod_roundtrip_diagnostics, "logic-roundtrip.joggle");
  ok &= expect(mod_roundtrip &&
                   mod_text.find("body: (T) -> U") != std::string::npos &&
                   mod_text.find("callback_factory<T, U>() "
                                 "-> (T) -> U;") != std::string::npos &&
                   joggle::format(*mod_roundtrip) == mod_text,
               "fn types format and parse canonically");
  joggle::Diag invalid_callback_diagnostics;
  const auto invalid_callback = joggle::parse_mod(
      R"(
joggle 1;
mod invalid_callback@1.0.0 {
  fn apply(body: (missing) -> i32) -> i32;
}
)",
      invalid_callback_diagnostics, "invalid-callback.joggle");
  ok &= expect(!invalid_callback && !invalid_callback_diagnostics.ok(),
               "types nested in callable signatures are name-resolved");
  const auto main_symbol =
      mod ? mod->symbol(joggle::Mod::Symbol::Kind::Fn, "main") : std::nullopt;
  const auto reflected_fn =
      main_symbol ? compiler.materialize(*main_symbol) : std::nullopt;
  ok &= expect(reflected_fn && reflected_fn->ops().size() == 2U,
               "a reflected fn symbol opens without rebuilding its name");
  ok &= expect(fn->declaration() && main_symbol &&
                   fn->declaration()->symbol() == *main_symbol &&
                   fn->result_types().size() == 1U &&
                   fn->arguments().size() == 1U && operations.size() == 2U &&
                   fn->entry().terminator().returned().size() == 1U &&
                   fn->entry().terminator().returned().front() ==
                       operations.back().result(0) &&
                   operations.back().result(0).type() ==
                       fn->arguments().front().type() &&
                   fn->result_types().front() ==
                       operations.back().result(0).type(),
               "a concrete fn signature and its SSA boundary agree");
  ok &= expect(operations.front().callee().binding<std::string>("name") ==
                   "input } // still a string",
               "fn boundaries are parsed by the real string grammar");

  const std::string emitted = joggle::format(*fn, "compiled");
  joggle::Compiler emitted_compiler;
  emitted_compiler.add(source, "logic.joggle");
  emitted_compiler.add("joggle 1;\nmod artifact@1.0.0 {\n"
                       "  import logic@1;\n" +
                           emitted + "}\n",
                       "artifact.joggle");
  const bool emitted_linked = emitted_compiler.link();
  const auto emitted_fn =
      emitted_linked ? emitted_compiler.materialize("artifact.compiled")
                     : std::optional<joggle::Fn>{};
  ok &= expect(emitted_fn && joggle::format(*emitted_fn, "compiled") == emitted,
               "a committed Fn formats to round-trippable canonical DSL");
  bool rejected_fn_name = false;
  try {
    static_cast<void>(joggle::format(*fn, "not.a.name"));
  } catch (const std::invalid_argument&) {
    rejected_fn_name = true;
  }
  ok &= expect(rejected_fn_name, "Fn formatting rejects a non-DSL member name");

  joggle::Diag roundtrip_diagnostics;
  const std::string canonical = joggle::format(*mod);
  const auto roundtrip =
      joggle::parse_mod(canonical, roundtrip_diagnostics, "canonical.joggle");
  ok &= expect(roundtrip && joggle::format(*roundtrip) == canonical,
               "one mod formatter owns schema and fn syntax");

  constexpr std::string_view cfg_source = R"(
joggle 1;
mod cfg@1.0.0 {
  type word();
  type memory();
  fn identity(input: word) -> word;
  fn advance(token: effect<memory>) -> effect<memory>;
  fn literal<T>(value: int) -> T ;
  fn (<)(lhs: i32, rhs: i32) -> i1;
  fn (>)(lhs: i32, rhs: i32) -> i1;
  fn (+)(lhs: i32, rhs: i32) -> i32;
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
  fn effect_branch(condition: i1, token: effect<memory>) -> effect<memory> {
    if condition {
      token = advance(token);
    } else {
      token = advance(token);
    }
    return token;
  }
  fn effect_loop(condition: i1, token: effect<memory>) -> effect<memory> {
    while condition {
      token = advance(token);
    }
    return token;
  }
  fn effect_return(condition: i1, token: effect<memory>) -> effect<memory> {
    if condition {
      return advance(token);
    } else {
      return advance(token);
    }
  }
  fn effect_for(token: effect<memory>) -> effect<memory> {
    for item: i32 in @range(4) {
      token = advance(token);
    }
    return token;
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
  joggle::Diag cfg_diagnostics;
  const auto cfg = joggle::parse_mod(cfg_source, cfg_diagnostics, "cfg.joggle");
  const std::string cfg_canonical = cfg ? joggle::format(*cfg) : std::string{};
  joggle::Diag cfg_roundtrip_diagnostics;
  const auto cfg_roundtrip =
      cfg ? joggle::parse_mod(cfg_canonical, cfg_roundtrip_diagnostics,
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
  const auto cfg_fn =
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
  const auto cfg_effect_branch =
      cfg_linked ? cfg_compiler.materialize("cfg.effect_branch") : std::nullopt;
  const auto cfg_effect_loop =
      cfg_linked ? cfg_compiler.materialize("cfg.effect_loop") : std::nullopt;
  const auto cfg_effect_return =
      cfg_linked ? cfg_compiler.materialize("cfg.effect_return") : std::nullopt;
  const auto cfg_effect_for =
      cfg_linked ? cfg_compiler.materialize("cfg.effect_for") : std::nullopt;
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
      cfg_fn ? joggle::format(*cfg_fn, "choose") : std::string{};
  const auto materialized_operations =
      cfg_materialized ? cfg_materialized->ops() : std::vector<joggle::Op>{};
  const auto early_literal_operations =
      cfg_early_literal ? cfg_early_literal->ops() : std::vector<joggle::Op>{};
  ok &= expect(
      cfg_fn && cfg_structured && cfg_specialized && cfg_nested &&
          cfg_materialized && cfg_compiler.verify(*cfg_fn) &&
          cfg_compiler.verify(*cfg_structured) &&
          cfg_compiler.verify(*cfg_specialized) &&
          cfg_compiler.verify(*cfg_nested) &&
          cfg_compiler.verify(*cfg_materialized) &&
          cfg_fn->blks().size() == 4U && cfg_structured->blks().size() == 4U &&
          cfg_specialized->blks().size() == 1U &&
          cfg_nested->blks().size() == 7U &&
          cfg_structured->ops().size() == 2U &&
          cfg_specialized->ops().size() == 1U &&
          cfg_nested->ops().size() == 3U &&
          cfg_specialized->entry().terminator().returned().front() ==
              cfg_specialized->ops().front().result(0) &&
          cfg_fn->entry().terminator().kind() == joggle::Term::Kind::Branch &&
          cfg_structured->entry().terminator().kind() ==
              joggle::Term::Kind::Branch &&
          cfg_fn->blks().back().arguments().size() == 1U &&
          cfg_fn->blks().back().terminator().returned().front() ==
              cfg_fn->blks().back().arguments().front() &&
          cfg_ir.find("branch arg0, block1(), block2();") !=
              std::string::npos &&
          cfg_ir.find("block3(arg3: cfg.word):") != std::string::npos,
      "explicit source blocks instantiate as Fn-owned CFG and "
      "format without a nested ownership container");
  ok &= expect(
      cfg_materialized && cfg_materialized->blks().size() == 4U &&
          cfg_materialized->ops().size() == 2U &&
          std::all_of(materialized_operations.begin(),
                      materialized_operations.end(),
                      [](const joggle::Op& op) {
                        return op.callee().referenced_fn()->name() == "literal";
                      }) &&
          cfg_materialized->result_types().front() ==
              cfg_materialized->blks().back().arguments().front().type(),
      "unequal Known branch values use a visible literal fn "
      "before crossing Residual edges");
  ok &= expect(
      cfg_statement_branch && cfg_statement_specialized &&
          cfg_statement_without_else && cfg_effect_branch && cfg_effect_loop &&
          cfg_effect_return && cfg_effect_for &&
          cfg_compiler.verify(*cfg_statement_branch) &&
          cfg_compiler.verify(*cfg_statement_specialized) &&
          cfg_compiler.verify(*cfg_statement_without_else) &&
          cfg_compiler.verify(*cfg_effect_branch) &&
          cfg_statement_branch->blks().size() == 4U &&
          cfg_statement_branch->ops().size() == 2U &&
          cfg_statement_branch->blks().back().arguments().size() == 1U &&
          cfg_statement_specialized->blks().size() == 1U &&
          cfg_statement_specialized->ops().size() == 1U &&
          cfg_statement_without_else->blks().size() == 4U &&
          cfg_statement_without_else->ops().size() == 1U &&
          cfg_statement_without_else->blks().back().arguments().size() == 1U,
      "statement if specializes Known control and automatically "
      "merges outer rebindings under Residual control");
  if (cfg_effect_branch) {
    const auto blocks = cfg_effect_branch->blks();
    const auto branch = cfg_effect_branch->entry().terminator();
    const auto operations = cfg_effect_branch->ops();
    ok &= expect(
        blocks.size() == 4U && operations.size() == 2U &&
            branch.kind() == joggle::Term::Kind::Branch &&
            branch.arguments(0) ==
                std::vector<joggle::Val>{cfg_effect_branch->arguments()[1]} &&
            branch.arguments(1) ==
                std::vector<joggle::Val>{cfg_effect_branch->arguments()[1]} &&
            blocks[1].arguments().size() == 1U &&
            blocks[2].arguments().size() == 1U &&
            operations[0].arguments() == blocks[1].arguments() &&
            operations[1].arguments() == blocks[2].arguments() &&
            blocks.back().arguments().size() == 1U &&
            blocks.back().terminator().returned() == blocks.back().arguments(),
        "Residual if transfers an effect token through each exclusive arm "
        "and merges the successor tokens explicitly");
  }
  if (cfg_effect_loop) {
    const auto blocks = cfg_effect_loop->blks();
    bool valid_effect_loop = cfg_compiler.verify(*cfg_effect_loop) &&
                             blocks.size() == 4U &&
                             cfg_effect_loop->ops().size() == 1U;
    if (valid_effect_loop) {
      const auto header = blocks[1];
      const auto body = blocks[2];
      const auto exit = blocks[3];
      const auto branch = header.terminator();
      valid_effect_loop =
          branch.kind() == joggle::Term::Kind::Branch &&
          body.arguments().size() == 1U &&
          branch.arguments(0) == header.arguments() &&
          branch.arguments(1) == header.arguments() &&
          cfg_effect_loop->ops().front().arguments() == body.arguments() &&
          exit.arguments().size() == 1U &&
          exit.terminator().returned() == exit.arguments();
    }
    ok &= expect(valid_effect_loop,
                 "Residual while carries effect tokens through its header, "
                 "body, backedge, and exit as explicit SSA arguments");
  }
  if (cfg_effect_return) {
    const auto blocks = cfg_effect_return->blks();
    bool valid_effect_return = cfg_compiler.verify(*cfg_effect_return) &&
                               blocks.size() == 3U &&
                               cfg_effect_return->ops().size() == 2U;
    if (valid_effect_return) {
      const auto branch = blocks.front().terminator();
      valid_effect_return =
          branch.arguments(0) ==
              std::vector<joggle::Val>{cfg_effect_return->arguments()[1]} &&
          branch.arguments(1) ==
              std::vector<joggle::Val>{cfg_effect_return->arguments()[1]} &&
          cfg_effect_return->ops()[0].arguments() == blocks[1].arguments() &&
          cfg_effect_return->ops()[1].arguments() == blocks[2].arguments();
    }
    ok &= expect(valid_effect_return,
                 "Residual branches carry visible effect state even when "
                 "both arms return directly without rebinding a local");
  }
  ok &= expect(cfg_effect_for && cfg_compiler.verify(*cfg_effect_for),
               "typed for carries visible effect state through its residual "
               "header, body, latch, and exit");
  ok &= expect(
      cfg_early_return && cfg_early_return_both && cfg_specialized_return &&
          cfg_loop_return && cfg_early_literal &&
          cfg_compiler.verify(*cfg_early_return) &&
          cfg_compiler.verify(*cfg_early_return_both) &&
          cfg_compiler.verify(*cfg_specialized_return) &&
          cfg_compiler.verify(*cfg_loop_return) &&
          cfg_compiler.verify(*cfg_early_literal) &&
          cfg_early_return->blks().size() == 3U &&
          cfg_early_return->ops().size() == 2U &&
          cfg_early_return_both->blks().size() == 3U &&
          cfg_early_return_both->ops().size() == 2U &&
          cfg_specialized_return->blks().size() == 1U &&
          cfg_specialized_return->ops().size() == 1U &&
          cfg_loop_return->blks().size() == 4U &&
          cfg_loop_return->ops().size() == 2U &&
          cfg_early_literal->blks().size() == 3U &&
          cfg_early_literal->ops().size() == 2U &&
          std::all_of(early_literal_operations.begin(),
                      early_literal_operations.end(),
                      [](const joggle::Op& op) {
                        return op.callee().referenced_fn()->name() == "literal";
                      }),
      "structured returns terminate only their selected control "
      "paths without a Region or synthetic merge");

  joggle::Compiler invalid_effect_source;
  invalid_effect_source.add(R"(
joggle 1;
mod invalid_effect_source@1.0.0 {
  type memory();
  fn advance(token: effect<memory>) -> effect<memory>;
  fn invalid(token: effect<memory>) -> effect<memory> {
    first = advance(token);
    second = advance(token);
    return first;
  }
}
)",
                            "invalid-effect-source.joggle");
  const bool invalid_effect_linked = invalid_effect_source.link();
  const auto invalid_effect_fn =
      invalid_effect_linked
          ? invalid_effect_source.materialize("invalid_effect_source.invalid")
          : std::nullopt;
  const bool reports_effect_reuse = std::any_of(
      invalid_effect_source.diag().issues().begin(),
      invalid_effect_source.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("more than one consuming use") !=
               std::string::npos;
      });
  ok &= expect(invalid_effect_linked && !invalid_effect_fn &&
                   reports_effect_reuse,
               "source materialization rejects reusing one effect token on "
               "the same control-flow path");

  joggle::Diag incomplete_return_diagnostics;
  const auto incomplete_return = joggle::parse_mod(
      R"(
joggle 1;
mod incomplete_return@1.0.0 {
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
      incomplete_return_diagnostics.issues().begin(),
      incomplete_return_diagnostics.issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("path that does not return") !=
               std::string::npos;
      });
  ok &= expect(!incomplete_return && reports_incomplete_return,
               "structured fns reject a fallthrough path");

  joggle::Diag unreachable_statement_diagnostics;
  const auto unreachable_statement = joggle::parse_mod(
      R"(
joggle 1;
mod unreachable_statement@1.0.0 {
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
      std::any_of(unreachable_statement_diagnostics.issues().begin(),
                  unreachable_statement_diagnostics.issues().end(),
                  [](const joggle::Issue& diagnostic) {
                    return diagnostic.message.find("unreachable statement") !=
                           std::string::npos;
                  });
  ok &= expect(!unreachable_statement && reports_unreachable_statement,
               "statements after a structured control transfer are rejected");

  joggle::Compiler missing_literal;
  missing_literal.add(R"(
joggle 1;
mod missing_literal@1.0.0 {
  fn choose(condition: i1) -> i32 {
    return if condition { 1 } else { 2 };
  }
}
)",
                      "missing-literal.joggle");
  const bool missing_literal_linked = missing_literal.link();
  const auto missing_literal_fn =
      missing_literal_linked
          ? missing_literal.materialize("missing_literal.choose")
          : std::optional<joggle::Fn>{};
  const bool reports_missing_literal =
      std::any_of(missing_literal.diag().issues().begin(),
                  missing_literal.diag().issues().end(),
                  [](const joggle::Issue& diagnostic) {
                    return diagnostic.message.find("no visible literal fn") !=
                           std::string::npos;
                  });
  ok &= expect(missing_literal_linked && !missing_literal_fn &&
                   reports_missing_literal,
               "Known values cannot cross dynamic control without an "
               "explicitly visible literal contract");

  joggle::Compiler ambiguous_literal;
  ambiguous_literal.add(R"(
joggle 1;
mod literal_a@1.0.0 {
  fn literal<T>(value: int) -> T;
}
)",
                        "literal-a.joggle");
  ambiguous_literal.add(R"(
joggle 1;
mod literal_b@1.0.0 {
  fn literal<T>(value: int) -> T;
}
)",
                        "literal-b.joggle");
  ambiguous_literal.add(R"(
joggle 1;
mod ambiguous_literal@1.0.0 {
  import literal_a@1.0.0;
  import literal_b@1.0.0;
  fn choose(condition: i1) -> i32 {
    return if condition { 1 } else { 2 };
  }
}
)",
                        "ambiguous-literal.joggle");
  const bool ambiguous_literal_linked = ambiguous_literal.link();
  const auto ambiguous_literal_fn =
      ambiguous_literal_linked
          ? ambiguous_literal.materialize("ambiguous_literal.choose")
          : std::optional<joggle::Fn>{};
  const bool reports_ambiguous_literal = std::any_of(
      ambiguous_literal.diag().issues().begin(),
      ambiguous_literal.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("more than one visible literal fn") !=
               std::string::npos;
      });
  ok &= expect(ambiguous_literal_linked && !ambiguous_literal_fn &&
                   reports_ambiguous_literal,
               "ambiguous literal contracts fail deterministically");

  constexpr std::string_view loop_source = R"(
joggle 1;
mod loops@1.0.0 {
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
  joggle::Diag loop_parse_diagnostics;
  const auto loop_mod =
      joggle::parse_mod(loop_source, loop_parse_diagnostics, "loops.joggle");
  const std::string loop_canonical =
      loop_mod ? joggle::format(*loop_mod) : std::string{};
  joggle::Diag loop_roundtrip_diagnostics;
  const auto loop_roundtrip =
      loop_mod ? joggle::parse_mod(loop_canonical, loop_roundtrip_diagnostics,
                                   "loops-canonical.joggle")
               : std::nullopt;
  ok &= expect(loop_mod && loop_roundtrip && loop_parse_diagnostics.ok() &&
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
                                   : std::optional<joggle::Fn>{};
  const auto specialize = loops_linked
                              ? loop_compiler.materialize("loops.specialize")
                              : std::optional<joggle::Fn>{};
  const auto count_from_zero =
      loops_linked ? loop_compiler.materialize("loops.count_from_zero")
                   : std::optional<joggle::Fn>{};
  const auto huge_counted_loop =
      loops_linked ? loop_compiler.materialize("loops.huge_counted_loop")
                   : std::optional<joggle::Fn>{};
  const auto controlled = loops_linked
                              ? loop_compiler.materialize("loops.controlled")
                              : std::optional<joggle::Fn>{};
  const auto known_break = loops_linked
                               ? loop_compiler.materialize("loops.known_break")
                               : std::optional<joggle::Fn>{};
  const auto known_continue =
      loops_linked ? loop_compiler.materialize("loops.known_continue")
                   : std::optional<joggle::Fn>{};
  ok &= expect(
      repeat && loop_compiler.verify(*repeat) && repeat->blks().size() == 4U &&
          repeat->ops().size() == 2U &&
          repeat->entry().terminator().kind() == joggle::Term::Kind::Jump &&
          repeat->blks()[1].arguments().size() == 1U &&
          repeat->blks()[1].terminator().kind() == joggle::Term::Kind::Branch &&
          repeat->blks()[3].arguments().size() == 1U,
      "Residual loops carry rebinding through typed Blk "
      "arguments");
  ok &= expect(
      count_from_zero && loop_compiler.verify(*count_from_zero) &&
          count_from_zero->blks().size() == 4U &&
          count_from_zero->ops().size() == 3U &&
          count_from_zero->ops().front().callee().referenced_fn()->name() ==
              "literal",
      "a typed Known initializer materializes before becoming a "
      "Residual loop-carried value");
  ok &= expect(huge_counted_loop && loop_compiler.verify(*huge_counted_loop) &&
                   huge_counted_loop->blks().size() <= 5U &&
                   huge_counted_loop->ops().size() <= 8U,
               "a typed Prelude range materializes a compact counted loop "
               "without allocating its billion-element compiler list");
  ok &= expect(specialize && loop_compiler.verify(*specialize) &&
                   specialize->blks().size() == 1U &&
                   specialize->ops().size() == 1U &&
                   specialize->result_types().front() ==
                       specialize->ops().front().result(0).type(),
               "Known loops execute during specialization without entering "
               "the residual CFG");
  ok &= expect(
      controlled && loop_compiler.verify(*controlled) &&
          controlled->blks().size() == 8U && controlled->ops().size() == 4U &&
          controlled->blks()[1].arguments().size() == 1U &&
          controlled->predecessors(controlled->blks()[1]).size() == 3U &&
          controlled->predecessors(controlled->blks()[3]).size() == 2U,
      "Residual break and continue carry current values directly to "
      "the loop exit and header");
  ok &= expect(
      known_break && known_continue && loop_compiler.verify(*known_break) &&
          loop_compiler.verify(*known_continue) &&
          known_break->blks().size() == 1U &&
          known_continue->blks().size() == 1U &&
          known_break->result_types().front().get<std::int64_t>("width") ==
              std::optional<std::int64_t>{1} &&
          known_continue->result_types().front().get<std::int64_t>("width") ==
              std::optional<std::int64_t>{1},
      "Known break and continue execute as compiler control without "
      "entering the residual CFG");

  joggle::Diag outside_loop_diagnostics;
  const auto outside_loop =
      joggle::parse_mod(R"(
joggle 1;
mod outside_loop@1.0.0 {
  fn invalid() {
    break;
  }
}
)",
                        outside_loop_diagnostics, "outside-loop.joggle");
  const bool reports_outside_loop = std::any_of(
      outside_loop_diagnostics.issues().begin(),
      outside_loop_diagnostics.issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("only valid inside a structured loop") !=
               std::string::npos;
      });
  ok &= expect(!outside_loop && reports_outside_loop,
               "loop control outside a structured loop is rejected");

  joggle::Compiler mixed_loop_transfer;
  mixed_loop_transfer.add(R"(
joggle 1;
mod mixed_loop_transfer@1.0.0 {
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
          : std::optional<joggle::Fn>{};
  const auto mixed_continue =
      mixed_loop_transfer_linked
          ? mixed_loop_transfer.materialize("mixed_loop_transfer.continue_on")
          : std::optional<joggle::Fn>{};
  const auto mixed_control_shape = [](const joggle::Fn& fn) {
    const auto blocks = fn.blks();
    return blocks.size() == 3U &&
           blocks.front().terminator().kind() == joggle::Term::Kind::Branch &&
           blocks[1].terminator().kind() == joggle::Term::Kind::Return &&
           blocks[2].terminator().kind() == joggle::Term::Kind::Return;
  };
  std::vector<std::int64_t> mixed_break_literals;
  if (mixed_break) {
    for (const auto& op : mixed_break->ops()) {
      if (const auto value = op.callee().binding<std::int64_t>("value")) {
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
mod cyclic_mixed_loop@1.0.0 {
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
  const auto cyclic_mixed_loop_mod = cyclic_mixed_loop.mod("cyclic_mixed_loop");
  const auto cyclic_mixed_loop_declaration =
      cyclic_mixed_loop_mod ? cyclic_mixed_loop_mod->fn("rebuild")
                            : std::nullopt;
  const auto compiler_integer = cyclic_mixed_loop.make("int");
  const auto initial_phase =
      compiler_integer
          ? cyclic_mixed_loop.known(*compiler_integer, std::int64_t{0})
          : std::nullopt;
  const auto cyclic_mixed_loop_fn =
      cyclic_mixed_loop_linked && cyclic_mixed_loop_declaration && initial_phase
          ? cyclic_mixed_loop.materialize(*cyclic_mixed_loop_declaration,
                                          {*initial_phase})
          : std::optional<joggle::Fn>{};
  if (!cyclic_mixed_loop_fn) {
    cyclic_mixed_loop.diag().print(std::cerr);
  }
  std::vector<bool> cyclic_bool_literals;
  std::vector<std::int64_t> cyclic_integer_literals;
  if (cyclic_mixed_loop_fn) {
    for (const auto& op : cyclic_mixed_loop_fn->ops()) {
      if (const auto value = op.callee().binding<bool>("value")) {
        cyclic_bool_literals.push_back(*value);
      }
      if (const auto value = op.callee().binding<std::int64_t>("value")) {
        cyclic_integer_literals.push_back(*value);
      }
    }
    std::sort(cyclic_bool_literals.begin(), cyclic_bool_literals.end());
    std::sort(cyclic_integer_literals.begin(), cyclic_integer_literals.end());
  }
  const auto has_backedge = [](const joggle::Fn& fn) {
    const auto blocks = fn.blks();
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
  ok &= expect(
      cyclic_mixed_loop_fn && cyclic_mixed_loop.verify(*cyclic_mixed_loop_fn) &&
          has_backedge(*cyclic_mixed_loop_fn) && cyclic_bool_literals.empty() &&
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
mod computed_cycle@1.0.0 {
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
  const auto computed_cycle_fn =
      computed_cycle_linked
          ? computed_cycle.materialize("computed_cycle.invalid")
          : std::optional<joggle::Fn>{};
  ok &= expect(computed_cycle_linked && computed_cycle_fn &&
                   computed_cycle.verify(*computed_cycle_fn) &&
                   has_backedge(*computed_cycle_fn),
               "a computed Known condition closes a finite specialized CFG "
               "cycle without requiring a bool-to-i1 representation");

  joggle::Compiler unconstrained_cycle({.steps = 8, .depth = 64});
  unconstrained_cycle.add(R"(
joggle 1;
mod unconstrained_cycle@1.0.0 {
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
  const auto unconstrained_cycle_fn =
      unconstrained_cycle_linked
          ? unconstrained_cycle.materialize("unconstrained_cycle.invalid")
          : std::optional<joggle::Fn>{};
  ok &= expect(unconstrained_cycle_linked && unconstrained_cycle_fn &&
                   unconstrained_cycle.verify(*unconstrained_cycle_fn) &&
                   has_backedge(*unconstrained_cycle_fn) &&
                   unconstrained_cycle_fn->ops().empty(),
               "control-state specialization needs no target width for an "
               "unconstrained compiler integer");

  joggle::Compiler bounded_loop({.steps = 2, .depth = 64});
  bounded_loop.add(R"(
joggle 1;
mod bounded_loop@1.0.0 {
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
          : std::optional<joggle::Fn>{};
  const bool reports_loop_limit = std::any_of(
      bounded_loop.diag().issues().begin(), bounded_loop.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find(
                   "compile-time while iteration limit exceeded") !=
               std::string::npos;
      });
  ok &= expect(bounded_loop_linked && !never_finishes && reports_loop_limit,
               "Known loops fail deterministically when their evaluation "
               "budget is exhausted");

  joggle::Diag invalid_cfg_diagnostics;
  const auto invalid_cfg =
      joggle::parse_mod(R"(
joggle 1;
mod invalid_cfg@1.0.0 {
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
                   std::any_of(invalid_cfg_diagnostics.issues().begin(),
                               invalid_cfg_diagnostics.issues().end(),
                               [](const joggle::Issue& diagnostic) {
                                 return diagnostic.message.find(
                                            "edge provides 0") !=
                                        std::string::npos;
                               }),
               "CFG verification rejects a block-edge arity mismatch");

  joggle::Diag legacy_diagnostics;
  const auto legacy =
      joggle::parse_mod("joggle 1; mod legacy@1.0.0 { op old(body: region); }",
                        legacy_diagnostics, "legacy.joggle");
  ok &= expect(!legacy && !legacy_diagnostics.ok(),
               "the former body-as-parameter syntax is rejected");

  constexpr std::string_view undefined = R"(
joggle 1;
mod logic@1.0.0 {
  type word(width: int);
  fn add<T>(lhs: T, rhs: T) -> T;
  fn main() -> word<8> {
    sum: word<8> = add(missing, missing);
    return sum;
  }
}
)";
  joggle::Compiler invalid;
  const auto invalid_fn = load_fn(invalid, undefined);
  const auto invalid_diagnostics = invalid.diag().issues();
  ok &=
      expect(!invalid_fn && !invalid.ok() && !invalid_diagnostics.empty() &&
                 invalid_diagnostics.front().source.has_value() &&
                 invalid_diagnostics.front().source->source == "logic.joggle" &&
                 invalid_diagnostics.front().source->begin.line == 7U,
             "undefined SSA diagnostics point into the fn");

  joggle::Compiler unknown;
  unknown.add(R"(
joggle 1;
mod logic@1.0.0 {
  type different();
}
)",
              "unknown.joggle");
  if (!unknown.link()) {
    return EXIT_FAILURE;
  }
  const auto foreign = unknown.materialize("logic.main");
  ok &= expect(!foreign && !unknown.ok(), "a named fn keeps its mod identity");

  joggle::Compiler unqualified;
  unqualified.add(source, "logic.joggle");
  const bool unqualified_linked = unqualified.link();
  const auto unqualified_fn = unqualified_linked
                                  ? unqualified.materialize("main")
                                  : std::optional<joggle::Fn>{};
  const auto unqualified_diagnostics = unqualified.diag().issues();
  ok &= expect(!unqualified_fn && !unqualified_diagnostics.empty() &&
                   unqualified_diagnostics.back().message.find("mod.member") !=
                       std::string::npos,
               "fn lookup requires one unambiguous qualified member name");

  joggle::Compiler mismatch;
  mismatch.add(R"(
joggle 1;
mod mismatch@1.0.0 {
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
  const auto mismatch_fn = mismatch_linked
                               ? mismatch.materialize("mismatch.main")
                               : std::optional<joggle::Fn>{};
  ok &= expect(!mismatch_fn && !mismatch.ok(),
               "one type variable rejects operands with different types");

  joggle::Compiler return_inferred;
  return_inferred.add(R"(
joggle 1;
mod return_inferred@1.0.0 {
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
  const auto return_inferred_fn =
      return_inferred_linked
          ? return_inferred.materialize("return_inferred.main")
          : std::optional<joggle::Fn>{};
  ok &= expect(return_inferred_fn &&
                   return_inferred_fn->entry().terminator().returned().size() ==
                       1U,
               "a fn result constrains an output-only type variable");

  joggle::Compiler unbound;
  unbound.add(R"(
joggle 1;
mod unbound@1.0.0 {
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
  const auto unbound_fn = unbound_linked ? unbound.materialize("unbound.main")
                                         : std::optional<joggle::Fn>{};
  ok &=
      expect(!unbound_fn && !unbound.ok(),
             "an unconstrained output-only type variable needs an annotation");

  constexpr std::string_view dependent_source = R"(
joggle 1;
mod dependent@1.0.0 {
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
  const auto dependent_mod = dependent.mod("dependent");
  const auto dependent_double =
      dependent_mod ? dependent_mod->fn("double") : std::nullopt;
  if (dependent_double) {
    dependent.bind(*dependent_double,
                   [](std::int64_t value) { return value * 2; });
  }
  auto dependent_fn = dependent_linked && dependent_double
                          ? dependent.materialize("dependent.inferred")
                          : std::nullopt;
  const auto dependent_operations =
      dependent_fn ? dependent_fn->ops() : std::vector<joggle::Op>{};
  const auto dependent_width =
      dependent_operations.empty()
          ? std::optional<std::int64_t>{}
          : dependent_operations.front().result(0).type().get<std::int64_t>(
                "width");
  ok &= expect(
      dependent_mod && dependent_fn && dependent_width &&
          *dependent_width == 8 &&
          joggle::format(*dependent_mod)
                  .find("fn input<N: int>(width: N) -> word<N>;") !=
              std::string::npos &&
          joggle::format(*dependent_mod)
                  .find("fn default_input<N: int>(width: N = 8) -> word<N>;") !=
              std::string::npos,
      "a computed Known argument binds a generic and infers a text "
      "fn result without another annotation");

  joggle::Compiler inconsistent;
  inconsistent.add(dependent_source, "dependent.joggle");
  const bool inconsistent_linked = inconsistent.link();
  const auto inconsistent_mod = inconsistent.mod("dependent");
  const auto inconsistent_word =
      inconsistent_mod ? inconsistent_mod->type("word") : std::nullopt;
  const auto inconsistent_input =
      inconsistent_mod ? inconsistent_mod->fn("input") : std::nullopt;
  const auto default_input =
      inconsistent_mod ? inconsistent_mod->fn("default_input") : std::nullopt;
  const auto word8 =
      inconsistent_word ? inconsistent.make(*inconsistent_word, std::int64_t{8})
                        : std::nullopt;
  const auto int_type = inconsistent.make("int");
  const auto width7 =
      int_type ? inconsistent.known(*int_type, std::int64_t{7}) : std::nullopt;
  auto inconsistent_fn = inconsistent.create_fn();
  if (!inconsistent_linked || !inconsistent_input || !default_input || !word8 ||
      !width7 || !inconsistent_fn) {
    return EXIT_FAILURE;
  }
  bool inconsistent_rejected = false;
  {
    auto edit = inconsistent_fn->edit();
    const auto value = edit.call(*inconsistent_input, {*width7}, {*word8});
    edit.ret(inconsistent_fn->entry(), {value.result(0)});
    joggle::Diag diagnostics;
    inconsistent_rejected = !edit.commit(diagnostics) && !diagnostics.ok();
  }
  ok &= expect(inconsistent_rejected && inconsistent_fn->arguments().empty() &&
                   inconsistent_fn->ops().empty(),
               "commit validates an explicit result against its dependent "
               "known argument and rolls back on mismatch");

  joggle::Compiler defaulted;
  defaulted.add(dependent_source, "dependent.joggle");
  const bool defaulted_linked = defaulted.link();
  const auto defaulted_mod = defaulted.mod("dependent");
  const auto defaulted_input =
      defaulted_mod ? defaulted_mod->fn("default_input") : std::nullopt;
  const auto named_input =
      defaulted_mod ? defaulted_mod->fn("input") : std::nullopt;
  const auto defaulted_int = defaulted.make("int");
  auto defaulted_fn = defaulted.create_fn();
  if (!defaulted_linked || !defaulted_input || !named_input || !defaulted_int ||
      !defaulted_fn) {
    return EXIT_FAILURE;
  }
  {
    auto edit = defaulted_fn->edit();
    const auto value = edit.call(*defaulted_input);
    edit.ret(defaulted_fn->entry(), {value.result(0)});
    joggle::Diag diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  const auto defaulted_width = defaulted_fn->entry()
                                   .terminator()
                                   .returned()
                                   .front()
                                   .type()
                                   .get<std::int64_t>("width");
  ok &= expect(defaulted_width && *defaulted_width == 8 &&
                   defaulted.verify(*defaulted_fn),
               "C++ append infers a result from a schema-owned default "
               "default without repeating the type");

  auto named_fn = defaulted.create_fn();
  if (!named_fn) {
    return EXIT_FAILURE;
  }
  {
    auto edit = named_fn->edit();
    auto width = defaulted.known(*defaulted_int, std::int64_t{12});
    if (!width) {
      return EXIT_FAILURE;
    }
    const auto value = edit.call(*named_input, {*width}).value();
    edit.ret(named_fn->entry(), {value});
    joggle::Diag diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  const auto named_width = named_fn->entry()
                               .terminator()
                               .returned()
                               .front()
                               .type()
                               .get<std::int64_t>("width");
  ok &= expect(named_width && *named_width == 12 && defaulted.verify(*named_fn),
               "a Known C++ argument participates in result inference at "
               "Op creation");

  bool extra_argument_rejected = false;
  try {
    auto edit = named_fn->edit();
    auto one = defaulted.known(*defaulted_int, std::int64_t{1});
    auto two = defaulted.known(*defaulted_int, std::int64_t{2});
    edit.call(*named_input, {*one, *two});
  } catch (const std::invalid_argument& error) {
    extra_argument_rejected =
        std::string_view(error.what()).find("too many arguments") !=
        std::string_view::npos;
  }
  ok &= expect(extra_argument_rejected,
               "an extra C++ argument is rejected immediately");

  bool wrong_known_kind_rejected = false;
  try {
    auto edit = named_fn->edit();
    const auto string_type = defaulted.make("string");
    const auto wide =
        string_type ? defaulted.known(*string_type, "wide") : std::nullopt;
    if (!wide) {
      return EXIT_FAILURE;
    }
    edit.call(*named_input, {*wide});
  } catch (const std::invalid_argument& error) {
    wrong_known_kind_rejected =
        std::string_view(error.what()).find("compatible Known value") !=
        std::string_view::npos;
  }
  ok &= expect(wrong_known_kind_rejected,
               "a wrong-kind Known C++ argument is rejected immediately");

  constexpr std::string_view computed_source = R"(
joggle 1;
mod computed@1.0.0 {
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
  const auto computed_mod = computed.mod("computed");
  const auto host_double = computed_mod ? computed_mod->fn("!") : std::nullopt;
  if (computed_linked && host_double) {
    computed.bind(*host_double, [](std::int64_t value) { return value * 2; });
  }
  const auto computed_fn =
      computed_linked ? computed.materialize("computed.main") : std::nullopt;
  const auto packed_fn =
      computed_linked ? computed.materialize("computed.pack") : std::nullopt;
  const auto aligned_fn = computed_linked
                              ? computed.materialize("computed.align_width")
                              : std::nullopt;
  const auto sum_fn =
      computed_linked ? computed.materialize("computed.sum") : std::nullopt;
  const auto quotient_fn = computed_linked
                               ? computed.materialize("computed.quotient")
                               : std::nullopt;
  const auto compile_time_operator_fn =
      computed_linked ? computed.materialize("computed.compile_time_operator")
                      : std::nullopt;
  const auto compile_time_branch_fn =
      computed_linked ? computed.materialize("computed.compile_time_branch")
                      : std::nullopt;
  const auto compile_time_host_fn =
      computed_linked ? computed.materialize("computed.compile_time_host")
                      : std::nullopt;
  const auto main_width =
      computed_fn && !computed_fn->entry().terminator().returned().empty()
          ? computed_fn->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const auto packed_width =
      packed_fn && !packed_fn->entry().terminator().returned().empty()
          ? packed_fn->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const auto aligned_width =
      aligned_fn && !aligned_fn->entry().terminator().returned().empty()
          ? aligned_fn->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const std::string computed_text =
      computed_mod ? joggle::format(*computed_mod) : std::string{};
  const auto compile_time_operator_width =
      compile_time_operator_fn &&
              !compile_time_operator_fn->entry().terminator().returned().empty()
          ? compile_time_operator_fn->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const auto compile_time_branch_width =
      compile_time_branch_fn &&
              !compile_time_branch_fn->entry().terminator().returned().empty()
          ? compile_time_branch_fn->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  const auto compile_time_host_width =
      compile_time_host_fn &&
              !compile_time_host_fn->entry().terminator().returned().empty()
          ? compile_time_host_fn->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  if (!computed_fn || !packed_fn || !aligned_fn || !sum_fn || !quotient_fn ||
      !compile_time_operator_fn || !compile_time_branch_fn ||
      !compile_time_host_fn || !host_double || !main_width || !packed_width ||
      !aligned_width || !compile_time_operator_width ||
      !compile_time_branch_width || !compile_time_host_width) {
    computed.diag().print(std::cerr);
  }
  ok &= expect(
      computed_fn && packed_fn && aligned_fn && sum_fn && quotient_fn &&
          compile_time_operator_fn && main_width && compile_time_branch_fn &&
          compile_time_host_fn && host_double && packed_width &&
          aligned_width && compile_time_operator_width &&
          compile_time_branch_width && compile_time_host_width &&
          *main_width == 8 && *packed_width == 15 && *aligned_width == 16 &&
          *compile_time_operator_width == 8 &&
          *compile_time_branch_width == 8 && *compile_time_host_width == 14 &&
          sum_fn->ops().size() == 1U &&
          sum_fn->ops().front().callee().referenced_fn()->name() == "+" &&
          quotient_fn->ops().size() == 1U &&
          quotient_fn->ops().front().callee().referenced_fn()->name() == "//" &&
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
mod formats@1.0.0 {
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
mod projected@1.0.0 {
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
  const auto format_mod = projected.mod("formats");
  const std::string format_text =
      format_mod ? joggle::format(*format_mod) : std::string{};
  if (!encode13 || !align13) {
    projected.diag().print(std::cerr);
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
mod missing_field@1.0.0 {
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
  const auto missing_field_fn =
      missing_field_linked ? missing_field.materialize("missing_field.main")
                           : std::nullopt;
  const bool reports_missing_field = std::any_of(
      missing_field.diag().issues().begin(),
      missing_field.diag().issues().end(), [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("has no derived parameter") !=
               std::string::npos;
      });
  ok &=
      expect(missing_field_linked && !missing_field_fn && reports_missing_field,
             "generic computed fields are checked against the bound type");

  joggle::Compiler ill_typed_field;
  ill_typed_field.add(R"(
joggle 1;
mod ill_typed_field@1.0.0 {
  type malformed() {
    storage_bits: int = "wide";
  }
}
)",
                      "ill-typed-field.joggle");
  ok &= expect(!ill_typed_field.link() && !ill_typed_field.diag().ok(),
               "computed fields are checked against their declared domains");

  joggle::Compiler recursive_fn;
  recursive_fn.add(R"(
joggle 1;
mod recursive_fn@1.0.0 {
  fn first(value: int) -> int {
    return second(value);
  }
  fn second(value: int) -> int {
    return first(value);
  }
}
)",
                   "recursive-fn.joggle");
  const bool recursive_linked = recursive_fn.link();
  const bool reports_fn_cycle = std::any_of(
      recursive_fn.diag().issues().begin(), recursive_fn.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("pure fn cycle") != std::string::npos;
      });
  ok &= expect(!recursive_linked && reports_fn_cycle,
               "pure fn recursion is rejected during linking");

  joggle::Compiler imported_fn;
  imported_fn.add(R"(
joggle 1;
mod integer_math@1.0.0 {
  fn align(value: int, multiple: int) -> int {
    return ceildiv(value, multiple) * multiple;
  }
}
)",
                  "integer-math.joggle");
  imported_fn.add(R"(
joggle 1;
mod imported_fn@1.0.0 {
  import integer_math@1.0.0 as math;

  type word(width: int);

  fn aligned<W: int>(input: word<W>) -> word<math.align(W, 8)>;

  fn main(input: word<9>) -> word<16> {
    result = aligned(input);
    return result;
  }
}
)",
                  "imported-fn.joggle");
  const bool imported_fn_linked = imported_fn.link();
  const auto imported_fn_fn = imported_fn_linked
                                  ? imported_fn.materialize("imported_fn.main")
                                  : std::nullopt;
  const auto imported_width =
      imported_fn_fn && !imported_fn_fn->entry().terminator().returned().empty()
          ? imported_fn_fn->entry()
                .terminator()
                .returned()
                .front()
                .type()
                .get<std::int64_t>("width")
          : std::optional<std::int64_t>{};
  if (!imported_fn_fn || !imported_width) {
    imported_fn.diag().print(std::cerr);
  }
  ok &= expect(imported_fn_fn && imported_width && *imported_width == 16,
               "imported pure fns resolve in their declaring mod");

  joggle::Compiler imported_operator;
  imported_operator.add(R"(
joggle 1;
mod native_arith@1.0.0 {
  fn (+)<T>(lhs: T, rhs: T) -> T;
}
)",
                        "native-arith.joggle");
  imported_operator.add(R"(
joggle 1;
mod imported_operator@1.0.0 {
  import native_arith@1.0.0;

  fn main(lhs: i32, rhs: i32) -> i32 {
    result = lhs + rhs;
    return result;
  }
}
)",
                        "imported-operator.joggle");
  const bool imported_operator_linked = imported_operator.link();
  const auto imported_operator_fn =
      imported_operator_linked
          ? imported_operator.materialize("imported_operator.main")
          : std::nullopt;
  if (!imported_operator_fn) {
    imported_operator.diag().print(std::cerr);
  }
  ok &=
      expect(imported_operator_fn && imported_operator_fn->ops().size() == 1U &&
                 imported_operator_fn->ops()
                         .front()
                         .callee()
                         .referenced_fn()
                         ->symbol()
                         .qualified_name() == "native_arith.+",
             "imported operator notation resolves for native SSA types");

  joggle::Compiler ambiguous_operator;
  ambiguous_operator.add(R"(
joggle 1;
mod ambiguous_operator@1.0.0 {
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
  const auto ambiguous_operator_fn =
      ambiguous_operator_linked
          ? ambiguous_operator.materialize("ambiguous_operator.main")
          : std::nullopt;
  const bool reports_operator_ambiguity = std::any_of(
      ambiguous_operator.diag().issues().begin(),
      ambiguous_operator.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("duplicate fn overload '+'") !=
               std::string::npos;
      });
  ok &= expect(!ambiguous_operator_linked && !ambiguous_operator_fn &&
                   reports_operator_ambiguity,
               "identical symbolic overloads are rejected at declaration");

  joggle::Compiler unsafe_expression;
  unsafe_expression.add(R"(
joggle 1;
mod unsafe_expression@1.0.0 {
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
  const auto unsafe_fn =
      unsafe_linked ? unsafe_expression.materialize("unsafe_expression.main")
                    : std::nullopt;
  const bool reports_division_by_zero = std::any_of(
      unsafe_expression.diag().issues().begin(),
      unsafe_expression.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("division by zero") != std::string::npos;
      });
  ok &= expect(unsafe_linked && !unsafe_fn && reports_division_by_zero,
               "non-total compile-time arithmetic is rejected when its "
               "bindings become concrete");

  joggle::Compiler dynamic_at;
  dynamic_at.add(R"(
joggle 1;
mod dynamic_at@1.0.0 {
  fn invalid(input: i32) -> i32 {
    forced = @(input);
    return input;
  }
}
)",
                 "dynamic-at.joggle");
  const bool dynamic_at_linked = dynamic_at.link();
  const auto dynamic_at_fn = dynamic_at_linked
                                 ? dynamic_at.materialize("dynamic_at.invalid")
                                 : std::nullopt;
  const bool reports_dynamic_at = std::any_of(
      dynamic_at.diag().issues().begin(), dynamic_at.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("compile-time evaluation") !=
               std::string::npos;
      });
  ok &= expect(dynamic_at_linked && !dynamic_at_fn && reports_dynamic_at,
               "@ rejects a Residual value instead of changing its stage");

  joggle::Compiler guarded_host;
  guarded_host.add(R"(
joggle 1;
mod guarded_host@1.0.0 {
  fn observe(value: int) -> int;
  fn invalid(condition: i1) {
    selected = if condition { @observe(1) } else { @observe(2) };
    return;
  }
}
)",
                   "guarded-host.joggle");
  const bool guarded_host_linked = guarded_host.link();
  const auto guarded_host_mod = guarded_host.mod("guarded_host");
  const auto observe =
      guarded_host_mod ? guarded_host_mod->fn("observe") : std::nullopt;
  std::int64_t observations = 0;
  if (observe) {
    guarded_host.bind(*observe, [&](std::int64_t value) {
      ++observations;
      return value;
    });
  }
  const auto guarded_host_fn =
      guarded_host_linked && observe
          ? guarded_host.materialize("guarded_host.invalid")
          : std::nullopt;
  const bool reports_guarded_host = std::any_of(
      guarded_host.diag().issues().begin(), guarded_host.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find(
                   "guarded and cannot execute under Residual control") !=
               std::string::npos;
      });
  ok &= expect(!guarded_host_fn && observations == 0 && reports_guarded_host,
               "Residual branches never speculatively execute host code");

  joggle::Compiler hermetic_host;
  hermetic_host.add(R"(
joggle 1;
mod hermetic_host@1.0.0 {
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
  const auto hermetic_host_mod = hermetic_host.mod("hermetic_host");
  const auto hermetic_evaluate =
      hermetic_host_mod ? hermetic_host_mod->fn("evaluate") : std::nullopt;
  if (hermetic_evaluate) {
    hermetic_host.bind(
        *hermetic_evaluate, [](std::int64_t value) { return value; },
        joggle::HostEval::Hermetic);
  }
  const auto hermetic_host_fn =
      hermetic_host_linked && hermetic_evaluate
          ? hermetic_host.materialize("hermetic_host.valid")
          : std::nullopt;
  ok &= expect(hermetic_host_fn && hermetic_host.ok(),
               "a binding explicitly promised Hermetic may evaluate beneath "
               "Residual control");

  joggle::Compiler list_evaluation;
  list_evaluation.add(R"(
joggle 1;
mod list_evaluation@1.0.0 {
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
  const auto list_evaluation_mod = list_evaluation.mod("list_evaluation");
  const std::string list_evaluation_text =
      list_evaluation_mod ? joggle::format(*list_evaluation_mod)
                          : std::string{};
  joggle::Diag list_evaluation_roundtrip_diagnostics;
  const auto list_evaluation_roundtrip =
      list_evaluation_mod
          ? joggle::parse_mod(list_evaluation_text,
                              list_evaluation_roundtrip_diagnostics,
                              "list-evaluation-roundtrip.joggle")
          : std::nullopt;
  const auto list_sum =
      list_evaluation_mod ? list_evaluation_mod->fn("sum") : std::nullopt;
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
    list_evaluation.diag().print(std::cerr);
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
      "grammar, including list-valued host fns and empty "
      "lists");

  constexpr std::string_view staged_control_source = R"(
joggle 1;
mod staged_control@1.0.0 {
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

  fn residual_grid<M: int, N: int>(rows: M, columns: N, input: i32) -> i32 {
    current = input;
    for row, column in M, N {
      current = touch(current, row);
      current = touch(current, column);
    }
    return current;
  }

  fn residual_grid_nested<M: int, N: int>(
    rows: M,
    columns: N,
    input: i32
  ) -> i32 {
    current = input;
    for row in M {
      for column in N {
        current = touch(current, row);
        current = touch(current, column);
      }
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
  joggle::Diag staged_control_parse_diagnostics;
  const auto staged_control_mod =
      joggle::parse_mod(staged_control_source, staged_control_parse_diagnostics,
                        "staged-control.joggle");
  const std::string staged_control_text =
      staged_control_mod ? joggle::format(*staged_control_mod) : std::string{};
  joggle::Diag staged_control_roundtrip_diagnostics;
  const auto staged_control_roundtrip =
      staged_control_mod
          ? joggle::parse_mod(staged_control_text,
                              staged_control_roundtrip_diagnostics,
                              "staged-control-roundtrip.joggle")
          : std::nullopt;

  joggle::Compiler staged_control;
  staged_control.add(staged_control_source, "staged-control.joggle");
  const bool staged_control_linked = staged_control.link();
  const auto staged_mod = staged_control.mod("staged_control");
  const auto staged_integer_operator =
      [&](std::string_view symbol) -> std::optional<joggle::Mod::FnDecl> {
    if (!staged_mod) {
      return std::nullopt;
    }
    const auto overloads = staged_mod->overloads(symbol);
    const auto found = std::find_if(
        overloads.begin(), overloads.end(), [](const auto& candidate) {
          return !candidate.inputs().empty() &&
                 candidate.inputs().front().domain ==
                     joggle::Mod::Expr::reference("int");
        });
    return found == overloads.end()
               ? std::optional<joggle::Mod::FnDecl>{}
               : std::optional<joggle::Mod::FnDecl>{*found};
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
      staged_mod ? staged_mod->fn("specialize") : std::nullopt;
  const auto pipeline_decl =
      staged_mod ? staged_mod->fn("pipeline") : std::nullopt;
  const auto residual_count_decl =
      staged_mod ? staged_mod->fn("residual_count") : std::nullopt;
  const auto residual_grid_decl =
      staged_mod ? staged_mod->fn("residual_grid") : std::nullopt;
  const auto residual_grid_nested_decl =
      staged_mod ? staged_mod->fn("residual_grid_nested") : std::nullopt;
  const auto residual_control_decl =
      staged_mod ? staged_mod->fn("residual_control") : std::nullopt;
  const auto materialize_index_decl =
      staged_mod ? staged_mod->fn("materialize_index") : std::nullopt;
  const auto integer_type = staged_control.make("int");
  const auto prelude_mod = staged_control.mod("prelude");
  const auto list_decl = prelude_mod ? prelude_mod->type("list") : std::nullopt;
  const auto integer_list_type =
      list_decl && integer_type ? staged_control.make(*list_decl, *integer_type)
                                : std::nullopt;
  const auto width = integer_type
                         ? staged_control.known(*integer_type, std::int64_t{8})
                         : std::nullopt;
  const auto rows = integer_type
                        ? staged_control.known(*integer_type, std::int64_t{2})
                        : std::nullopt;
  const auto columns =
      integer_type ? staged_control.known(*integer_type, std::int64_t{3})
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
  const auto residual_grid =
      residual_grid_decl && rows && columns
          ? staged_control.materialize(*residual_grid_decl, {*rows, *columns})
          : std::nullopt;
  const auto residual_grid_nested =
      residual_grid_nested_decl && rows && columns
          ? staged_control.materialize(*residual_grid_nested_decl,
                                       {*rows, *columns})
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
  if (!staged_control_linked || !specialized || !pipeline || !residual_count ||
      !residual_grid || !residual_grid_nested || !empty_residual_count ||
      !residual_control || !materialized_index) {
    staged_control.diag().print(std::cerr);
  }
  ok &= expect(
      staged_control_mod && staged_control_roundtrip &&
          staged_control_parse_diagnostics.ok() &&
          staged_control_roundtrip_diagnostics.ok() &&
          joggle::format(*staged_control_roundtrip) == staged_control_text &&
          staged_control_text.find("for dimension in S {") !=
              std::string::npos &&
          staged_control_text.find("for position: index in @range(N) {") !=
              std::string::npos &&
          staged_control_text.find("for row, column in M, N {") !=
              std::string::npos &&
          sum_shape == std::optional<std::int64_t>{14} &&
          count == std::optional<std::int64_t>{3} && specialized && pipeline &&
          residual_count && staged_control.verify(*residual_count) &&
          residual_count->blks().size() == 5U &&
          residual_count->ops().size() == 6U && empty_residual_count &&
          residual_grid && residual_grid_nested &&
          staged_control.verify(*residual_grid) &&
          staged_control.verify(*residual_grid_nested) &&
          residual_grid->blks().size() == 9U &&
          joggle::format(*residual_grid, "grid") ==
              joggle::format(*residual_grid_nested, "grid") &&
          staged_control.verify(*empty_residual_count) &&
          empty_residual_count->blks().size() == 1U &&
          empty_residual_count->ops().empty() && residual_control &&
          staged_control.verify(*residual_control) && materialized_index &&
          staged_control.verify(*materialized_index) &&
          materialized_index->ops().size() == 1U &&
          materialized_index->result_types().front().schema().name() ==
              "index" &&
          specialized->arguments().front().type().get<std::int64_t>("width") ==
              std::optional<std::int64_t>{8} &&
          specialized->ops().size() == 1U && pipeline->ops().size() == 2U,
      "generic bindings are ordinary Known locals that drive if, while, for, "
      "dependent types, and deterministic residual expansion");

  joggle::Compiler explicit_staging;
  explicit_staging.add(R"(
joggle 1;
mod explicit_staging@1.0.0 {
  type word();
  fn literal<T>(value: int) -> T;
  fn twice(value: int) -> int;
  fn identity(input: word) -> word;
  fn make_i32() -> i32;
  fn apply(input: word, body: (word) -> word) -> word;
  fn inspect(body: fn) -> int;
  fn keep_fn(body: fn) -> fn;

  fn staged() -> i32 {
    value: i32 = @twice(2);
    return value;
  }

  fn missing_stage() {
    value = twice(2);
    return;
  }

  fn staged_lambda(input: word) -> word {
    count = @inspect((value: word) -> word => identity(value));
    return identity(input);
  }

  fn staged_lambda_chain(input: word) -> word {
    body = @keep_fn((value: word) => identity(value));
    count = @inspect(body);
    return identity(input);
  }

  fn staged_lambda_capture(input: word) -> word {
    count = @inspect((value: word) => identity(input));
    return identity(input);
  }

  fn wrong_lambda_context(input: word) -> word {
    return apply(input, (value: word) -> i32 => make_i32());
  }

  fn wrong_lambda_body(input: word) -> word {
    count = @inspect((value: word) -> i32 => identity(value));
    return identity(input);
  }
}
)",
                       "explicit-staging.joggle");
  const bool explicit_staging_linked = explicit_staging.link();
  const auto explicit_staging_mod = explicit_staging.mod("explicit_staging");
  const auto twice_decl =
      explicit_staging_mod ? explicit_staging_mod->fn("twice") : std::nullopt;
  const auto inspect_decl =
      explicit_staging_mod ? explicit_staging_mod->fn("inspect") : std::nullopt;
  const auto keep_fn_decl =
      explicit_staging_mod ? explicit_staging_mod->fn("keep_fn") : std::nullopt;
  std::size_t evaluations = 0;
  if (twice_decl) {
    explicit_staging.bind(*twice_decl, [&](std::int64_t value) {
      ++evaluations;
      return value * 2;
    });
  }
  std::size_t inspected_lambda_ops = 0;
  std::size_t lambda_inspections = 0;
  if (inspect_decl) {
    explicit_staging.bind(*inspect_decl,
                          [&](const joggle::Fn& body) -> std::int64_t {
                            ++lambda_inspections;
                            inspected_lambda_ops = body.ops().size();
                            return static_cast<std::int64_t>(body.ops().size());
                          });
  }
  if (keep_fn_decl) {
    explicit_staging.bind(
        *keep_fn_decl,
        [](const joggle::Fn& body) -> joggle::Fn { return body; });
  }
  const auto staged =
      explicit_staging_linked && twice_decl
          ? explicit_staging.materialize("explicit_staging.staged")
          : std::nullopt;
  if (!staged) {
    explicit_staging.diag().print(std::cerr);
  }
  ok &= expect(explicit_staging_linked && staged &&
                   explicit_staging.verify(*staged) &&
                   staged->ops().size() == 1U && evaluations == 1U,
               "@ explicitly evaluates a compiler-domain call and "
               "materializes its result only at a Residual boundary");

  const auto staged_lambda =
      explicit_staging_linked && inspect_decl
          ? explicit_staging.materialize("explicit_staging.staged_lambda")
          : std::nullopt;
  if (!staged_lambda || inspected_lambda_ops != 1U) {
    explicit_staging.diag().print(std::cerr);
  }
  ok &= expect(staged_lambda && explicit_staging.verify(*staged_lambda) &&
                   staged_lambda->ops().size() == 1U &&
                   inspected_lambda_ops == 1U && lambda_inspections == 1U,
               "@ passes a result-annotated lambda as a verified Fn "
               "execution value without scalar serialization");
  ok &= expect(explicit_staging_mod &&
                   joggle::format(*explicit_staging_mod)
                           .find("(value: word) -> word => identity(value)") !=
                       std::string::npos,
               "lambda result annotations have one canonical source form");

  const auto staged_lambda_chain =
      explicit_staging_linked && inspect_decl && keep_fn_decl
          ? explicit_staging.materialize("explicit_staging.staged_lambda_chain")
          : std::nullopt;
  ok &= expect(staged_lambda_chain &&
                   explicit_staging.verify(*staged_lambda_chain) &&
                   staged_lambda_chain->ops().size() == 1U &&
                   inspected_lambda_ops == 1U && lambda_inspections == 2U,
               "@ results retain Fn identity for a later explicit "
               "compiler call");

  const auto staged_lambda_capture =
      explicit_staging_linked && inspect_decl
          ? explicit_staging.materialize(
                "explicit_staging.staged_lambda_capture")
          : std::nullopt;
  const bool reports_staged_capture = std::any_of(
      explicit_staging.diag().issues().begin(),
      explicit_staging.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("undefined local value 'input'") !=
               std::string::npos;
      });
  ok &= expect(!staged_lambda_capture && reports_staged_capture &&
                   lambda_inspections == 2U,
               "a compiler-time lambda cannot capture an outer Residual "
               "value");

  const auto missing_stage =
      explicit_staging_linked
          ? explicit_staging.materialize("explicit_staging.missing_stage")
          : std::nullopt;
  const bool reports_missing_stage = std::any_of(
      explicit_staging.diag().issues().begin(),
      explicit_staging.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("requires explicit @ evaluation") !=
               std::string::npos;
      });
  if (missing_stage || !reports_missing_stage) {
    explicit_staging.diag().print(std::cerr);
  }
  ok &= expect(!missing_stage && reports_missing_stage && evaluations == 1U,
               "an ordinary compiler-domain call neither evaluates nor "
               "silently changes stage");

  const auto wrong_lambda_context =
      explicit_staging_linked ? explicit_staging.materialize(
                                    "explicit_staging.wrong_lambda_context")
                              : std::nullopt;
  const bool reports_wrong_lambda_context = std::any_of(
      explicit_staging.diag().issues().begin(),
      explicit_staging.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find(
                   "inline fn result does not match its callable context") !=
               std::string::npos;
      });
  ok &= expect(!wrong_lambda_context && reports_wrong_lambda_context,
               "an explicit lambda result cannot contradict its callable "
               "context");

  const auto wrong_lambda_body =
      explicit_staging_linked && inspect_decl
          ? explicit_staging.materialize("explicit_staging.wrong_lambda_body")
          : std::nullopt;
  ok &= expect(!wrong_lambda_body && lambda_inspections == 2U,
               "an explicit lambda result is checked against its body before "
               "a compiler fn can observe it");

  const joggle::Compiler::Limits limits{2, 64};
  joggle::Compiler bounded(limits);
  bounded.add(R"(
joggle 1;
mod bounded@1.0.0 {
  type word(width: int);
  fn source() -> word<1 + 2>;
  fn main() -> word<3> {
    return source();
  }
}
)",
              "bounded.joggle");
  const bool bounded_linked = bounded.link();
  const auto bounded_fn =
      bounded_linked ? bounded.materialize("bounded.main") : std::nullopt;
  const bool reports_step_limit = std::any_of(
      bounded.diag().issues().begin(), bounded.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find(
                   "compile-time evaluation step limit exceeded") !=
               std::string::npos;
      });
  ok &= expect(bounded.evaluation_limits().steps == limits.steps &&
                   bounded.evaluation_limits().depth == limits.depth &&
                   bounded_linked && !bounded_fn && reports_step_limit,
               "compile-time evaluation obeys deterministic resource limits");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
