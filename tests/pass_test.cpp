#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

struct Target {
  std::int64_t lanes = 0;
};

struct Estimate {
  std::int64_t cycles = 0;
};

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TEST_MODULE);
  compiler.add(R"(
joggle 1;
module pipeline@1.0.0 {
  import test_ir@1;
  fn read(input: bytes) -> function;
  fn inspect(input: function) -> int;
  fn emit(input: function) -> bytes;
  fn consume(input: bytes);
  fn clean(input: function) -> function {
    return test_ir.canonicalize(input);
  }
  fn compile(input: bytes) -> bytes {
    return emit(clean(read(input)));
  }
}
)",
               "pipeline.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto test_ir = compiler.module("test_ir");
  const auto pipeline = compiler.module("pipeline");
  if (!test_ir ||
      !compiler.load_behavior("test_ir", JOGGLE_TEST_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto integer_decl =
      test_ir ? test_ir->type("integer") : std::nullopt;
  const auto arith_cast_decl =
      test_ir ? test_ir->function("cast") : std::nullopt;
  const auto format_decl =
      test_ir ? test_ir->interface("numeric_format") : std::nullopt;
  const auto canonicalize =
      test_ir ? test_ir->function("canonicalize") : std::nullopt;
  const auto clean = pipeline ? pipeline->function("clean") : std::nullopt;
  const auto read = pipeline ? pipeline->function("read") : std::nullopt;
  const auto emit = pipeline ? pipeline->function("emit") : std::nullopt;
  const auto inspect = pipeline ? pipeline->function("inspect") : std::nullopt;
  const auto compile = pipeline ? pipeline->function("compile") : std::nullopt;
  const auto consume = pipeline ? pipeline->function("consume") : std::nullopt;
  if (!integer_decl || !arith_cast_decl || !format_decl || !canonicalize ||
      !clean || !read || !emit || !inspect || !compile || !consume) {
    return EXIT_FAILURE;
  }
  const auto integer = compiler.make(*integer_decl, std::int64_t{8});
  auto function = compiler.function();
  if (!integer || !function) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  auto edit = function->edit();
  const auto input = edit.argument(*integer);
  const auto first = edit.append(*arith_cast_decl, {input});
  const auto second = edit.append(*arith_cast_decl, {first.result(0)});
  edit.append(*arith_cast_decl, {second.result(0)});
  joggle::Diagnostics edit_diagnostics;
  if (!edit.commit(edit_diagnostics)) {
    edit_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  ok &= expect(read->inputs().size() == 1U &&
                   read->inputs().front().domain ==
                       joggle::Module::Expression::reference("bytes") &&
                   read->results().size() == 1U &&
                   read->results().front().domain ==
                       joggle::Module::Expression::reference("function") &&
                   emit->inputs().front().domain ==
                       joggle::Module::Expression::reference("function") &&
                   emit->results().front().domain ==
                       joggle::Module::Expression::reference("bytes") &&
                   compile->inputs().front().domain ==
                       joggle::Module::Expression::reference("bytes") &&
                   compile->results().front().domain ==
                       joggle::Module::Expression::reference("bytes"),
               "pass declarations reflect functional types");

  compiler.bind(*read,
                [](joggle::Compiler& current,
                   const joggle::Bytes& bytes) -> std::optional<joggle::Function> {
                  return bytes.empty() ? std::nullopt : current.function();
                });
  compiler.bind(*inspect, [](const joggle::Function& current) -> std::int64_t {
    return static_cast<std::int64_t>(current.instructions().size());
  });
  compiler.bind(*emit, [](const joggle::Function& current) -> joggle::Bytes {
    return {static_cast<std::byte>(current.instructions().size())};
  });
  bool consumed = false;
  compiler.bind(*consume, [&](const joggle::Bytes&) { consumed = true; });
  const joggle::Bytes encoded{std::byte{0x42}};
  auto decoded = compiler.run<joggle::Function>(*read, encoded);
  auto count = decoded
                   ? compiler.run<std::int64_t>(*inspect, *decoded)
                   : std::optional<std::int64_t>{};
  auto direct_encoded = decoded
                            ? compiler.run<joggle::Bytes>(*emit, *decoded)
                            : std::optional<joggle::Bytes>{};
  auto reencoded = compiler.run<joggle::Bytes>(*compile, encoded);
  const bool consume_ok = compiler.run(*consume, encoded);
  ok &= expect(decoded && count && *count == 0 && direct_encoded &&
                   reencoded && consume_ok && consumed &&
                   reencoded->size() == 1U &&
                   reencoded->front() == std::byte{0},
               "read, analysis, transformation, and emission share typed run");
  joggle::Diagnostics signature_diagnostics;
  const std::string signature_text = joggle::format(*pipeline);
  const auto signature_roundtrip = joggle::parse_module(
      signature_text, signature_diagnostics, "pipeline-canonical.joggle");
  ok &= expect(signature_roundtrip &&
                   joggle::format(*signature_roundtrip) == signature_text,
               "typed pass signatures format and parse canonically");
  const auto bits = integer->get<std::int64_t>("storage_bits");
  ok &= expect(bits && *bits == 8,
               "derived parameters share the ordinary Type query path");
  ok &= expect(canonicalize->form() == joggle::Module::FunctionDecl::Form::Body,
               "a rewrite uses the same function body form");
  ok &= expect(compiler.run(*function, *clean),
               "an imported rule pass composes without a C++ binding");
  ok &= expect(function->instructions().empty(),
               "greedy contraction removes redundant same-type casts");

  constexpr std::string_view repeated_source = R"(
joggle 1;
module repeated@1.0.0 {
  type value();
  fn pair<T: type>(lhs: T, rhs: T) -> T;
  fn deduplicate(input: function) -> function {
    return rewrite(input) {
      pair($x, $x) => $x;
    };
  }
}
)";
  joggle::Compiler repeated_compiler;
  repeated_compiler.add(repeated_source, "repeated.joggle");
  if (!repeated_compiler.link()) {
    return EXIT_FAILURE;
  }
  const auto repeated = repeated_compiler.module("repeated");
  const auto value_decl = repeated ? repeated->type("value") : std::nullopt;
  const auto pair_decl = repeated ? repeated->function("pair") : std::nullopt;
  const auto deduplicate =
      repeated ? repeated->function("deduplicate") : std::nullopt;
  const auto value = value_decl ? repeated_compiler.make(*value_decl)
                                : std::optional<joggle::Type>{};
  auto repeated_graph = repeated_compiler.function();
  if (!pair_decl || !deduplicate || !value || !repeated_graph) {
    return EXIT_FAILURE;
  }
  auto repeated_edit = repeated_graph->edit();
  const auto lhs = repeated_edit.argument(*value);
  const auto rhs = repeated_edit.argument(*value);
  repeated_edit.append(*pair_decl, {lhs, lhs});
  repeated_edit.append(*pair_decl, {lhs, rhs});
  joggle::Diagnostics repeated_diagnostics;
  if (!repeated_edit.commit(repeated_diagnostics)) {
    return EXIT_FAILURE;
  }
  ok &= expect(repeated_compiler.run(*repeated_graph, *deduplicate) &&
                   repeated_graph->instructions().size() == 1U,
               "repeated pass variables require the same SSA value");

  constexpr std::string_view mismatch_source = R"(
    joggle 1;
    module mismatch@1.0.0 {
      type a();
      type b();
      fn cast<A: type, B: type>(input: A) -> B;
      fn simplify(input: function) -> function {
        return rewrite(input) {
          cast($input) => $input;
        };
      }
    }
  )";
  joggle::Compiler mismatch_compiler;
  mismatch_compiler.add(mismatch_source, "mismatch.joggle");
  if (!mismatch_compiler.link()) {
    mismatch_compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto mismatch_module = mismatch_compiler.module("mismatch");
  const auto a_decl =
      mismatch_module ? mismatch_module->type("a") : std::nullopt;
  const auto b_decl =
      mismatch_module ? mismatch_module->type("b") : std::nullopt;
  const auto cast_decl =
      mismatch_module ? mismatch_module->function("cast") : std::nullopt;
  const auto simplify =
      mismatch_module ? mismatch_module->function("simplify") : std::nullopt;
  if (!a_decl || !b_decl || !cast_decl || !simplify) {
    return EXIT_FAILURE;
  }
  const auto a = mismatch_compiler.make(*a_decl);
  const auto b = mismatch_compiler.make(*b_decl);
  auto mismatch_graph = mismatch_compiler.function();
  if (!a || !b || !mismatch_graph) {
    return EXIT_FAILURE;
  }
  auto mismatch_edit = mismatch_graph->edit();
  const auto mismatch_input = mismatch_edit.argument(*a);
  mismatch_edit.append(*cast_decl, {mismatch_input}, {*b});
  joggle::Diagnostics mismatch_edit_diagnostics;
  if (!mismatch_edit.commit(mismatch_edit_diagnostics)) {
    return EXIT_FAILURE;
  }
  ok &= expect(
      !mismatch_compiler.run(*mismatch_graph, *simplify) &&
          mismatch_graph->instructions().size() == 1U &&
          mismatch_graph->instructions().front().callee().name() ==
              "cast" &&
          mismatch_graph->instructions().front().result(0).type() == *b,
      "type-incompatible rule fails and restores the whole Function");

  constexpr std::string_view guarded_source = R"(
    joggle 1;
    module guarded@1.0.0 {
      type a();
      fn identity<T: type>(input: T) -> T;
      fn touch(input: function) -> function;
    }
  )";
  joggle::Compiler guarded_compiler;
  guarded_compiler.add(guarded_source, "guarded.joggle");
  if (!guarded_compiler.link()) {
    return EXIT_FAILURE;
  }
  const auto guarded = guarded_compiler.module("guarded");
  const auto guarded_a_decl = guarded ? guarded->type("a") : std::nullopt;
  const auto guarded_identity =
      guarded ? guarded->function("identity") : std::nullopt;
  const auto guarded_a =
      guarded_a_decl ? guarded_compiler.make(*guarded_a_decl) : std::nullopt;
  auto guarded_graph = guarded_compiler.function();
  if (!guarded || !guarded_identity || !guarded_a || !guarded_graph) {
    return EXIT_FAILURE;
  }
  {
    auto edit = guarded_graph->edit();
    const auto input = edit.argument(*guarded_a);
    edit.append(*guarded_identity, {input});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  guarded_compiler.bind(
      *guarded_identity,
      [](const joggle::Instruction&, joggle::Diagnostics& diagnostics) {
        diagnostics.report("guarded pass input rejected");
        return false;
      });
  bool pass_called = false;
  const auto guarded_touch = guarded->function("touch");
  if (!guarded_touch) {
    return EXIT_FAILURE;
  }
  guarded_compiler.bind(
      *guarded_touch,
      [&](joggle::Compiler&, joggle::Function&, joggle::Diagnostics&) {
        pass_called = true;
        return true;
      });
  ok &= expect(!guarded_compiler.run(*guarded_graph, "guarded.touch") &&
                   !pass_called && !guarded_compiler.ok(),
               "a pass does not execute on a Function rejected by bound domain "
               "semantics");

  joggle::Compiler named_compiler;
  named_compiler.add("joggle 1; module named@1.0.0 { "
                     "fn noop(input: function) -> function; }",
                     "named.joggle");
  const bool named_linked = named_compiler.link();
  const auto named_module = named_compiler.module("named");
  auto named_graph = named_compiler.function();
  bool named_called = false;
  if (named_module) {
    const auto noop = named_module->function("noop");
    if (!noop) {
      return EXIT_FAILURE;
    }
    named_compiler.bind(*noop, [&](joggle::Compiler&, joggle::Function&,
                                   joggle::Diagnostics&) {
      named_called = true;
      return true;
    });
  }
  const bool unqualified_run =
      named_linked && named_graph && named_compiler.run(*named_graph, "noop");
  const auto named_diagnostics = named_compiler.diagnostics().entries();
  ok &=
      expect(!unqualified_run && !named_called && !named_diagnostics.empty() &&
                 named_diagnostics.back().message.find("module.member") !=
                     std::string::npos,
               "pass lookup requires one unambiguous qualified member name");

  joggle::Compiler incompatible;
  incompatible.add(R"(
joggle 1;
module incompatible@1.0.0 {
  fn read(input: bytes) -> function;
  fn inspect(input: function) -> int;
  fn broken(input: bytes) -> function {
    return inspect(read(input));
  }
}
)",
                   "incompatible.joggle");
  ok &= expect(!incompatible.link() && !incompatible.ok(),
               "pass sequence composition is checked by type");

  joggle::Compiler transactional;
  transactional.add(R"(
joggle 1;
module transactional@1.0.0 {
  fn token() -> i32;
  fn mutate(input: function) -> function;
  fn reject(input: function) -> bytes;
  fn pipeline(input: function) -> bytes {
    return reject(mutate(input));
  }
}
)",
                    "transactional.joggle");
  const bool transactional_linked = transactional.link();
  const auto transactional_module = transactional.module("transactional");
  const auto token = transactional_module
                         ? transactional_module->function("token")
                         : std::nullopt;
  const auto mutate = transactional_module
                          ? transactional_module->function("mutate")
                          : std::nullopt;
  const auto reject = transactional_module
                          ? transactional_module->function("reject")
                          : std::nullopt;
  const auto transaction = transactional_module
                               ? transactional_module->function("pipeline")
                               : std::nullopt;
  auto transactional_graph = transactional.function();
  if (!transactional_linked || !token || !mutate || !reject || !transaction ||
      !transactional_graph) {
    return EXIT_FAILURE;
  }
  transactional.bind(
      *mutate, [token = *token](joggle::Function& current,
                               joggle::Diagnostics& diagnostics) {
        auto edit = current.edit();
        edit.append(token);
        return edit.commit(diagnostics);
      });
  transactional.bind(
      *reject,
      [](const joggle::Function&) -> std::optional<joggle::Bytes> {
        return std::nullopt;
      });
  const auto rejected =
      transactional.run<joggle::Bytes>(*transaction, *transactional_graph);
  ok &= expect(!rejected && transactional_graph->instructions().empty(),
               "typed sequence failure restores its existing Function input");

  joggle::Compiler binding_mismatch;
  binding_mismatch.add(
      "joggle 1; module binding_mismatch@1.0.0 { "
      "fn count(input: function) -> int; }",
      "binding-mismatch.joggle");
  const bool mismatch_linked = binding_mismatch.link();
  const auto binding_module = binding_mismatch.module("binding_mismatch");
  const auto count_pass =
      binding_module ? binding_module->function("count") : std::nullopt;
  if (!mismatch_linked || !count_pass) {
    return EXIT_FAILURE;
  }
  binding_mismatch.bind(*count_pass,
                        [](const joggle::Function&) { return std::string{"bad"}; });
  ok &= expect(!binding_mismatch.ok(),
               "C++ pass binding is checked against its declared type");

  joggle::Compiler represented;
  represented.add(R"(
joggle 1;
module represented@1.0.0 {
  type target();
  type estimate();

  fn measure(input: target) -> estimate;
  fn analyze(input: target) -> estimate {
    return measure(input);
  }
}
)",
                  "represented.joggle");
  const bool represented_linked = represented.link();
  const auto represented_module = represented.module("represented");
  const auto target_type = represented_module
                               ? represented_module->type("target")
                               : std::nullopt;
  const auto estimate_type = represented_module
                                 ? represented_module->type("estimate")
                                 : std::nullopt;
  const auto measure = represented_module
                           ? represented_module->function("measure")
                           : std::nullopt;
  const auto analyze = represented_module
                           ? represented_module->function("analyze")
                           : std::nullopt;
  if (!represented_linked || !target_type || !estimate_type || !measure ||
      !analyze || !represented.represent<Target>(*target_type) ||
      !represented.represent<Estimate>(*estimate_type)) {
    represented.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  represented.bind(*measure, [](const Target& target) {
    return Estimate{.cycles = 1024 / target.lanes};
  });
  const auto estimate = represented.run<Estimate>(*analyze, Target{32});
  if (!estimate || !represented.ok()) {
    represented.diagnostics().print(std::cerr);
  }
  ok &= expect(estimate && estimate->cycles == 32 && represented.ok(),
               "Module types can use ordinary registered C++ values through "
               "composed compiler functions");

  joggle::Compiler missing_projection;
  missing_projection.add(
      "joggle 1; module parameterized_host@1.0.0 { "
      "type target(lanes: int); }",
      "parameterized-host.joggle");
  const bool missing_projection_linked = missing_projection.link();
  const auto missing_projection_module =
      missing_projection.module("parameterized_host");
  const auto missing_projection_type =
      missing_projection_module
          ? missing_projection_module->type("target")
          : std::nullopt;
  const bool parameterized_rejected =
      missing_projection_linked && missing_projection_type &&
      !missing_projection.represent<Target>(*missing_projection_type);
  const bool reports_projection = std::any_of(
      missing_projection.diagnostics().entries().begin(),
      missing_projection.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("needs a projection") !=
               std::string::npos;
      });
  ok &= expect(parameterized_rejected && reports_projection,
               "host registration rejects parameterized schemas instead of "
               "discarding their type arguments");

  joggle::Compiler parameterized_host;
  parameterized_host.add(R"(
joggle 1;
module parameterized_host@1.0.0 {
  type target(lanes: int);
  type estimate(lanes: int);

  fn measure<N: int>(input: target<N>) -> estimate<N>;
  fn analyze<N: int>(input: target<N>) -> estimate<N> {
    return measure(input);
  }
  fn fixed(input: target<32>) -> estimate<32>;
  fn wrong(input: target<32>) -> estimate<32>;
}
)",
                         "parameterized-host.joggle");
  const bool parameterized_linked = parameterized_host.link();
  const auto parameterized_module =
      parameterized_host.module("parameterized_host");
  const auto parameterized_target =
      parameterized_module ? parameterized_module->type("target")
                           : std::nullopt;
  const auto parameterized_estimate =
      parameterized_module ? parameterized_module->type("estimate")
                           : std::nullopt;
  const auto parameterized_measure =
      parameterized_module ? parameterized_module->function("measure")
                           : std::nullopt;
  const auto parameterized_analyze =
      parameterized_module ? parameterized_module->function("analyze")
                           : std::nullopt;
  const auto fixed = parameterized_module
                         ? parameterized_module->function("fixed")
                         : std::nullopt;
  const auto wrong = parameterized_module
                         ? parameterized_module->function("wrong")
                         : std::nullopt;
  if (!parameterized_linked || !parameterized_target ||
      !parameterized_estimate || !parameterized_measure ||
      !parameterized_analyze || !fixed || !wrong ||
      !parameterized_host.represent<Target>(
          *parameterized_target,
          [](const Target& target) { return std::tuple{target.lanes}; }) ||
      !parameterized_host.represent<Estimate>(
          *parameterized_estimate,
          [](const Estimate& estimate) { return std::tuple{estimate.cycles}; })) {
    parameterized_host.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  parameterized_host.bind(*parameterized_measure,
                          [](const Target& target) {
                            return Estimate{.cycles = target.lanes};
                          });
  std::int64_t fixed_invocations = 0;
  parameterized_host.bind(*fixed, [&](const Target& target) {
    ++fixed_invocations;
    return Estimate{.cycles = target.lanes};
  });
  parameterized_host.bind(*wrong, [](const Target&) {
    return Estimate{.cycles = 64};
  });
  const auto parameterized_result =
      parameterized_host.run<Estimate>(*parameterized_analyze, Target{32});
  ok &= expect(parameterized_result && parameterized_result->cycles == 32,
               "a host projection preserves concrete type parameters through "
               "a composed generic compiler function");
  const auto rejected_input =
      parameterized_host.run<Estimate>(*fixed, Target{16});
  ok &= expect(!rejected_input && fixed_invocations == 0,
               "concrete projected input types are checked before native "
               "compiler code executes");
  const auto rejected_result =
      parameterized_host.run<Estimate>(*wrong, Target{32});
  ok &= expect(!rejected_result,
               "a native compiler function cannot return the wrong "
               "parameterized type instance");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
