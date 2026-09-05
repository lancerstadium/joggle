#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

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

static_assert(joggle::detail::valid_fn_input<Target>);
static_assert(joggle::detail::valid_fn_input<const Target&>);
static_assert(!joggle::detail::valid_fn_input<Target&>);
static_assert(!joggle::detail::valid_fn_input<Target&&>);
static_assert(!joggle::detail::valid_fn_input<const Target&&>);
static_assert(joggle::detail::valid_fn_result<Estimate>);
static_assert(!joggle::detail::valid_fn_result<Estimate&>);
static_assert(!joggle::detail::valid_fn_result<Estimate&&>);

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TEST_MOD);
  compiler.add(R"(
joggle 1;
mod pipeline@1.0.0 {
  import test_ir@1;
  type word(width: int);
  type flag(enabled: bool);
  fn read(input: bytes) -> fn;
  fn inspect(input: fn) -> int;
  fn emit(input: fn) -> bytes;
  fn consume(input: bytes);
  fn append(input: bytes) -> bytes;
  fn nonzero(input: int) -> bool;
  fn twice(input: int) -> int;
  fn twice(input: string) -> string;
  fn choose_amount(value: int, amount: int = 2) -> int {
    return amount;
  }
  fn (+)(lhs: int, rhs: int) -> int;
  fn (<)(lhs: int, rhs: int) -> bool;
  fn (<)(lhs: string, rhs: string) -> bool;
  fn (<=)(lhs: int, rhs: int) -> bool;
  fn (>)(lhs: int, rhs: int) -> bool;
  fn (>=)(lhs: int, rhs: int) -> bool;
  fn (==)(lhs: int, rhs: int) -> bool;
  fn (!=)(lhs: int, rhs: int) -> bool;
  fn (!)(input: bool) -> bool;
  fn (&&)(lhs: bool, rhs: bool) -> bool {
    return if lhs { rhs } else { false };
  }
  fn (||)(lhs: bool, rhs: bool) -> bool {
    return if lhs { true } else { rhs };
  }
  fn mod_identity(input: mod) -> mod;
  fn convert_word(input: test_ir.integer<8>) -> test_ir.integer<8>;
  fn convert_word(input: test_ir.integer<16>) -> test_ir.integer<16>;
  fn configured_copy(input: test_ir.integer<8>, tag: int = 7)
      -> test_ir.integer<8>;
  fn choose_first<T>(items: T...) -> T;
  fn compute_width(value: int) -> int;
  fn width_copy(input: test_ir.integer<8>)
      -> test_ir.integer<compute_width(8)>;
  fn residual_overload(input: test_ir.integer<8>) -> test_ir.integer<8> {
    result = convert_word(input);
    return result;
  }
  fn residual_arguments(input: test_ir.integer<8>) -> test_ir.integer<8> {
    first = configured_copy(input);
    second = configured_copy(tag: 9, input: first);
    return second;
  }
  fn residual_variadic(input: test_ir.integer<8>) -> test_ir.integer<8> {
    result = choose_first(input, input);
    return result;
  }
  fn residual_dependent(input: test_ir.integer<8>) -> test_ir.integer<8> {
    result = width_copy(input);
    return result;
  }
  fn clean(input: fn) -> fn {
    return test_ir.canonicalize(input);
  }
  fn compile(input: bytes) -> bytes {
    return emit(clean(read(input)));
  }
  fn select(condition: bool, input: bytes) -> bytes {
    result = input;
    if condition {
      result = append(result);
    } else {
      result = append(append(result));
    }
    return result;
  }
  fn repeat(count: int, input: bytes) -> bytes {
    result = input;
    while nonzero(count) {
      result = append(result);
      count = count - 1;
    }
    return result;
  }
  fn choose(condition: bool, lhs: bytes, rhs: bytes) -> bytes {
    return if condition { lhs } else { rhs };
  }
  fn once(input: bytes) -> bytes {
    while true {
      input = append(input);
      break;
    }
    return input;
  }
  fn last(count: int, input: bytes) -> bytes {
    while nonzero(count) {
      count = count - 1;
      if nonzero(count) {
        continue;
      }
      input = append(input);
    }
    return input;
  }
  fn choose_width(condition: bool) -> int {
    if condition {
      return 7;
    }
    return 9;
  }
  fn typed(input: word<choose_width(true)>) -> word<7> {
    return input;
  }
  fn use_twice(input: int) -> int {
    return twice(input);
  }
  fn use_operator(lhs: int, rhs: int) -> int {
    return lhs + rhs;
  }
  fn earlier(lhs: int, rhs: int) -> int {
    if lhs < rhs {
      return lhs;
    }
    return rhs;
  }
  fn invert(input: bool) -> bool {
    return !input;
  }
  fn ordered_typed(input: word<earlier(9, 7)>) -> word<7> {
    return input;
  }
  fn unequal_order(lhs: int, rhs: int) -> bool {
    return (lhs <= rhs && lhs != rhs) ||
           (lhs > rhs && lhs >= rhs);
  }
  fn relation_typed(input: flag<(9 > 7)>) -> flag<true> {
    return input;
  }
  fn text_relation_typed(input: flag<("a" < "b")>) -> flag<true> {
    return input;
  }
  fn overload_typed(input: word<@twice(4)>) -> word<8> {
    return input;
  }
  fn default_typed(input: word<@choose_amount(value: 3)>) -> word<2> {
    return input;
  }
  fn staged_overload(input: test_ir.integer<8>) -> test_ir.integer<8> {
    doubled = @twice(4);
    return input;
  }
  fn divide(input: int) -> (int, bool);
  fn divide_exact(input: int) -> (int, bool) {
    quotient, exact = divide(input);
    return quotient, exact;
  }
  fn observe(input: int);
  fn observe_once(input: int) {
    observe(input);
    return;
  }
  fn fork(input: test_ir.integer<8>)
      -> (test_ir.integer<8>, test_ir.integer<8>);
  fn relay_fork(input: test_ir.integer<8>)
      -> (test_ir.integer<8>, test_ir.integer<8>) {
    lhs, rhs = fork(input);
    return lhs, rhs;
  }
}
)",
               "pipeline.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto test_ir = compiler.mod("test_ir");
  const auto pipeline = compiler.mod("pipeline");
  if (!test_ir || !compiler.load_native("test_ir", JOGGLE_TEST_NATIVE)) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto integer_decl = test_ir ? test_ir->type("integer") : std::nullopt;
  const auto arith_cast_decl = test_ir ? test_ir->fn("cast") : std::nullopt;
  const auto canonicalize =
      test_ir ? test_ir->fn("canonicalize") : std::nullopt;
  const auto clean = pipeline ? pipeline->fn("clean") : std::nullopt;
  const auto read = pipeline ? pipeline->fn("read") : std::nullopt;
  const auto resolved_read = compiler.lookup("pipeline.read");
  const auto emit = pipeline ? pipeline->fn("emit") : std::nullopt;
  const auto inspect = pipeline ? pipeline->fn("inspect") : std::nullopt;
  const auto compile = pipeline ? pipeline->fn("compile") : std::nullopt;
  const auto consume = pipeline ? pipeline->fn("consume") : std::nullopt;
  const auto mod_identity =
      pipeline ? pipeline->fn("mod_identity") : std::nullopt;
  const auto convert_words = pipeline ? pipeline->overloads("convert_word")
                                      : std::vector<joggle::Mod::FnDecl>{};
  const auto configured_copy =
      pipeline ? pipeline->fn("configured_copy") : std::nullopt;
  const auto compute_width =
      pipeline ? pipeline->fn("compute_width") : std::nullopt;
  const auto width_copy = pipeline ? pipeline->fn("width_copy") : std::nullopt;
  const auto residual_overload =
      pipeline ? pipeline->fn("residual_overload") : std::nullopt;
  const auto residual_arguments =
      pipeline ? pipeline->fn("residual_arguments") : std::nullopt;
  const auto residual_variadic =
      pipeline ? pipeline->fn("residual_variadic") : std::nullopt;
  const auto residual_dependent =
      pipeline ? pipeline->fn("residual_dependent") : std::nullopt;
  const auto append = pipeline ? pipeline->fn("append") : std::nullopt;
  const auto nonzero = pipeline ? pipeline->fn("nonzero") : std::nullopt;
  const auto select = pipeline ? pipeline->fn("select") : std::nullopt;
  const auto repeat = pipeline ? pipeline->fn("repeat") : std::nullopt;
  const auto choose = pipeline ? pipeline->fn("choose") : std::nullopt;
  const auto once = pipeline ? pipeline->fn("once") : std::nullopt;
  const auto last = pipeline ? pipeline->fn("last") : std::nullopt;
  const auto typed = pipeline ? pipeline->fn("typed") : std::nullopt;
  const auto twice = pipeline ? pipeline->overloads("twice")
                              : std::vector<joggle::Mod::FnDecl>{};
  const auto operator_with_input =
      [&](std::string_view symbol,
          std::string_view input) -> std::optional<joggle::Mod::FnDecl> {
    if (!pipeline) {
      return std::nullopt;
    }
    const auto overloads = pipeline->overloads(symbol);
    const auto found = std::find_if(
        overloads.begin(), overloads.end(), [&](const auto& candidate) {
          return !candidate.inputs().empty() &&
                 candidate.inputs().front().domain ==
                     joggle::Mod::Expr::reference(std::string(input));
        });
    return found == overloads.end()
               ? std::optional<joggle::Mod::FnDecl>{}
               : std::optional<joggle::Mod::FnDecl>{*found};
  };
  const auto add_offset = operator_with_input("+", "int");
  const auto less = operator_with_input("<", "int");
  const auto text_less = operator_with_input("<", "string");
  const auto less_equal = operator_with_input("<=", "int");
  const auto greater = operator_with_input(">", "int");
  const auto greater_equal = operator_with_input(">=", "int");
  const auto equal = operator_with_input("==", "int");
  const auto not_equal = operator_with_input("!=", "int");
  const auto logical_not = operator_with_input("!", "bool");
  const auto earlier = pipeline ? pipeline->fn("earlier") : std::nullopt;
  const auto invert = pipeline ? pipeline->fn("invert") : std::nullopt;
  const auto ordered_typed =
      pipeline ? pipeline->fn("ordered_typed") : std::nullopt;
  const auto unequal_order =
      pipeline ? pipeline->fn("unequal_order") : std::nullopt;
  const auto relation_typed =
      pipeline ? pipeline->fn("relation_typed") : std::nullopt;
  const auto text_relation_typed =
      pipeline ? pipeline->fn("text_relation_typed") : std::nullopt;
  const auto overload_typed =
      pipeline ? pipeline->fn("overload_typed") : std::nullopt;
  const auto default_typed =
      pipeline ? pipeline->fn("default_typed") : std::nullopt;
  const auto staged_overload =
      pipeline ? pipeline->fn("staged_overload") : std::nullopt;
  const auto use_twice = pipeline ? pipeline->fn("use_twice") : std::nullopt;
  const auto use_operator =
      pipeline ? pipeline->fn("use_operator") : std::nullopt;
  const auto divide = pipeline ? pipeline->fn("divide") : std::nullopt;
  const auto divide_exact =
      pipeline ? pipeline->fn("divide_exact") : std::nullopt;
  const auto observe = pipeline ? pipeline->fn("observe") : std::nullopt;
  const auto observe_once =
      pipeline ? pipeline->fn("observe_once") : std::nullopt;
  const auto fork = pipeline ? pipeline->fn("fork") : std::nullopt;
  const auto relay_fork = pipeline ? pipeline->fn("relay_fork") : std::nullopt;
  if (!integer_decl || !arith_cast_decl || !canonicalize || !clean || !read ||
      !resolved_read || !emit || !inspect || !compile || !consume ||
      !mod_identity || convert_words.size() != 2U || !configured_copy ||
      !compute_width || !width_copy || !residual_overload ||
      !residual_arguments || !residual_variadic || !residual_dependent ||
      !append || !nonzero || !select || !repeat || !choose || !once || !last ||
      !typed || twice.size() != 2U || !add_offset || !less || !text_less ||
      !less_equal || !greater || !greater_equal || !equal || !not_equal ||
      !logical_not || !earlier || !invert || !ordered_typed || !unequal_order ||
      !relation_typed || !text_relation_typed || !overload_typed ||
      !default_typed || !staged_overload || !use_twice || !use_operator ||
      !divide || !divide_exact || !observe || !observe_once || !fork ||
      !relay_fork) {
    return EXIT_FAILURE;
  }
  const auto integer = compiler.make(*integer_decl, std::int64_t{8});
  auto fn = compiler.create_fn();
  if (!integer || !fn) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  auto edit = fn->edit();
  const auto input = edit.argument(*integer);
  const auto first = edit.call(*arith_cast_decl, {input});
  const auto second = edit.call(*arith_cast_decl, {first.result(0)});
  edit.call(*arith_cast_decl, {second.result(0)});
  joggle::Diag edit_diagnostics;
  if (!edit.commit(edit_diagnostics)) {
    edit_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  ok &= expect(resolved_read->symbol() == read->symbol(),
               "qualified compiler lookup returns the same Fn member "
               "used by typed invocation");
  ok &= expect(read->inputs().size() == 1U &&
                   read->inputs().front().domain ==
                       joggle::Mod::Expr::reference("bytes") &&
                   read->results().size() == 1U &&
                   read->results().front().domain ==
                       joggle::Mod::Expr::reference("fn") &&
                   emit->inputs().front().domain ==
                       joggle::Mod::Expr::reference("fn") &&
                   emit->results().front().domain ==
                       joggle::Mod::Expr::reference("bytes") &&
                   compile->inputs().front().domain ==
                       joggle::Mod::Expr::reference("bytes") &&
                   compile->results().front().domain ==
                       joggle::Mod::Expr::reference("bytes"),
               "compiler fns reflect functional types");

  compiler.bind(*read,
                [](joggle::Compiler& current,
                   const joggle::Bytes& bytes) -> std::optional<joggle::Fn> {
                  return bytes.empty() ? std::nullopt : current.create_fn();
                });
  compiler.bind(*inspect, [](const joggle::Fn& current) -> std::int64_t {
    return static_cast<std::int64_t>(current.ops().size());
  });
  compiler.bind(*emit, [](const joggle::Fn& current) -> joggle::Bytes {
    return {static_cast<std::byte>(current.ops().size())};
  });
  compiler.bind(*canonicalize,
                [arith_cast_decl](joggle::Fn current, joggle::Diag& diagnostics)
                    -> std::optional<joggle::Fn> {
                  auto edit = current.edit();
                  for (const joggle::Op& op : current.ops()) {
                    if (op.callee().referenced_fn() != arith_cast_decl) {
                      continue;
                    }
                    edit.replace(op.result(0), op.arguments().front());
                    edit.erase(op);
                  }
                  if (!edit.commit(diagnostics)) {
                    return std::nullopt;
                  }
                  return current;
                });
  bool consumed = false;
  compiler.bind(*consume, [&](const joggle::Bytes&) { consumed = true; });
  std::size_t append_calls = 0;
  compiler.bind(*append, [&](joggle::Bytes input) {
    ++append_calls;
    input.push_back(std::byte{0x2a});
    return input;
  });
  compiler.bind(*nonzero, [](std::int64_t input) { return input != 0; });
  compiler.bind(*pipeline, "twice",
                [](std::int64_t input) { return input * 2; });
  compiler.bind(*pipeline, "twice",
                [](std::string input) { return input + input; });
  compiler.bind(*add_offset, [](std::int64_t lhs, std::int64_t rhs) {
    return lhs + rhs + 100;
  });
  compiler.bind(*less,
                [](std::int64_t lhs, std::int64_t rhs) { return lhs < rhs; });
  compiler.bind(*text_less,
                [](std::string lhs, std::string rhs) { return lhs < rhs; });
  compiler.bind(*less_equal,
                [](std::int64_t lhs, std::int64_t rhs) { return lhs <= rhs; });
  compiler.bind(*greater,
                [](std::int64_t lhs, std::int64_t rhs) { return lhs > rhs; });
  compiler.bind(*greater_equal,
                [](std::int64_t lhs, std::int64_t rhs) { return lhs >= rhs; });
  compiler.bind(*equal,
                [](std::int64_t lhs, std::int64_t rhs) { return lhs == rhs; });
  compiler.bind(*not_equal,
                [](std::int64_t lhs, std::int64_t rhs) { return lhs != rhs; });
  compiler.bind(*logical_not, [](bool input) { return !input; });
  compiler.bind(*divide, [](std::int64_t input) {
    return std::tuple{input / 2, input % 2 == 0};
  });
  std::optional<std::int64_t> observed;
  compiler.bind(*observe, [&](std::int64_t input) { observed = input; });
  std::size_t width_evaluations = 0;
  compiler.bind(
      *compute_width,
      [&](std::int64_t value) {
        ++width_evaluations;
        return value;
      },
      joggle::HostEval::Hermetic);
  ok &= expect(
      compiler.invocable<joggle::Mod, joggle::Mod>(*mod_identity) &&
          compiler.invocable<joggle::Fn, joggle::Fn>(*clean) &&
          !compiler.invocable<joggle::Mod, joggle::Fn>(*clean) &&
          compiler.invocable<std::tuple<std::int64_t, bool>, std::int64_t>(
              *divide) &&
          !compiler.invocable<std::int64_t, std::int64_t>(*divide) &&
          compiler.invocable<void, std::int64_t>(*observe),
      "typed invocability distinguishes whole-Mod and single-Fn "
      "transforms and checks complete result sequences");
  compiler.bind(*mod_identity, [](joggle::Mod input) { return input; });
  const joggle::Bytes encoded{std::byte{0x42}};
  auto decoded = compiler.run<joggle::Fn>(*read, encoded);
  auto count = decoded ? compiler.run<std::int64_t>(*inspect, *decoded)
                       : std::optional<std::int64_t>{};
  auto direct_encoded = decoded ? compiler.run<joggle::Bytes>(*emit, *decoded)
                                : std::optional<joggle::Bytes>{};
  auto reencoded = compiler.run<joggle::Bytes>(*compile, encoded);
  const bool consume_ok = compiler.run(*consume, encoded);
  const auto selected_once =
      compiler.run<joggle::Bytes>(*select, true, joggle::Bytes{});
  const auto selected_twice =
      compiler.run<joggle::Bytes>(*select, false, joggle::Bytes{});
  const auto repeated =
      compiler.run<joggle::Bytes>(*repeat, std::int64_t{3}, joggle::Bytes{});
  const auto chosen = compiler.run<joggle::Bytes>(
      *choose, false, joggle::Bytes{std::byte{0x01}},
      joggle::Bytes{std::byte{0x02}});
  const auto broken = compiler.run<joggle::Bytes>(*once, joggle::Bytes{});
  const auto continued =
      compiler.run<joggle::Bytes>(*last, std::int64_t{3}, joggle::Bytes{});
  const auto overloaded =
      compiler.run<std::int64_t>(*use_twice, std::int64_t{6});
  const auto named_integer_overload =
      compiler.run<std::int64_t>("pipeline.twice", std::int64_t{6});
  const auto named_string_overload =
      compiler.run<std::string>("pipeline.twice", std::string{"ab"});
  const auto operated = compiler.run<std::int64_t>(
      *use_operator, std::int64_t{2}, std::int64_t{3});
  const auto ordered =
      compiler.run<std::int64_t>(*earlier, std::int64_t{9}, std::int64_t{7});
  const auto inverted = compiler.run<bool>(*invert, true);
  const auto ascending =
      compiler.run<bool>(*unequal_order, std::int64_t{3}, std::int64_t{7});
  const auto descending =
      compiler.run<bool>(*unequal_order, std::int64_t{9}, std::int64_t{7});
  const auto equal_order =
      compiler.run<bool>(*unequal_order, std::int64_t{7}, std::int64_t{7});
  const auto divided = compiler.run<std::tuple<std::int64_t, bool>>(
      *divide_exact, std::int64_t{8});
  const bool observed_once = compiler.run(*observe_once, std::int64_t{13});
  ok &= expect(decoded && count && *count == 0 && direct_encoded && reencoded &&
                   consume_ok && consumed && reencoded->size() == 1U &&
                   reencoded->front() == std::byte{0},
               "read, analysis, transformation, and emission share typed run");
  ok &= expect(
      selected_once && selected_once->size() == 1U && selected_twice &&
          selected_twice->size() == 2U && repeated && repeated->size() == 3U &&
          chosen && chosen->size() == 1U &&
          chosen->front() == std::byte{0x02} && broken &&
          broken->size() == 1U && continued && continued->size() == 1U &&
          append_calls == 8U && overloaded == std::optional<std::int64_t>{12} &&
          named_integer_overload == std::optional<std::int64_t>{12} &&
          named_string_overload == std::optional<std::string>{"abab"} &&
          operated == std::optional<std::int64_t>{105} &&
          ordered == std::optional<std::int64_t>{7} &&
          inverted == std::optional<bool>{false} &&
          ascending == std::optional<bool>{true} &&
          descending == std::optional<bool>{true} &&
          equal_order == std::optional<bool>{false} && divided &&
          *divided == std::tuple<std::int64_t, bool>{4, true} &&
          observed_once && observed == std::optional<std::int64_t>{13},
      "structured compiler fns execute selected branches, "
      "loops, overloads, typed operators, zero-result calls, and "
      "multi-result calls");
  const auto typed_fn = compiler.materialize(*typed);
  const auto ordered_typed_fn = compiler.materialize(*ordered_typed);
  const auto relation_typed_fn = compiler.materialize(*relation_typed);
  const auto text_relation_typed_fn =
      compiler.materialize(*text_relation_typed);
  const auto overload_typed_fn = compiler.materialize(*overload_typed);
  const auto default_typed_fn = compiler.materialize(*default_typed);
  const auto staged_overload_fn = compiler.materialize(*staged_overload);
  const auto residual_overload_fn = compiler.materialize(*residual_overload);
  const auto residual_arguments_fn = compiler.materialize(*residual_arguments);
  const auto residual_variadic_fn = compiler.materialize(*residual_variadic);
  const auto residual_dependent_fn = compiler.materialize(*residual_dependent);
  const auto relay_fork_fn = compiler.materialize(*relay_fork);
  const auto convert_word_8 = std::find_if(
      convert_words.begin(), convert_words.end(), [](const auto& fn) {
        const auto& domain = fn.inputs().front().domain;
        return !domain.arguments.empty() &&
               domain.arguments.front().text == "8";
      });
  ok &= expect(
      typed_fn && typed_fn->arguments().size() == 1U &&
          typed_fn->result_types().size() == 1U &&
          typed_fn->arguments().front().type() ==
              typed_fn->result_types().front() &&
          ordered_typed_fn && ordered_typed_fn->arguments().size() == 1U &&
          ordered_typed_fn->result_types().size() == 1U &&
          ordered_typed_fn->arguments().front().type() ==
              ordered_typed_fn->result_types().front() &&
          relation_typed_fn && relation_typed_fn->arguments().size() == 1U &&
          relation_typed_fn->result_types().size() == 1U &&
          relation_typed_fn->arguments().front().type() ==
              relation_typed_fn->result_types().front() &&
          text_relation_typed_fn &&
          text_relation_typed_fn->arguments().size() == 1U &&
          text_relation_typed_fn->result_types().size() == 1U &&
          text_relation_typed_fn->arguments().front().type() ==
              text_relation_typed_fn->result_types().front() &&
          overload_typed_fn && overload_typed_fn->arguments().size() == 1U &&
          overload_typed_fn->arguments().front().type() ==
              overload_typed_fn->result_types().front() &&
          default_typed_fn && default_typed_fn->arguments().size() == 1U &&
          default_typed_fn->arguments().front().type() ==
              default_typed_fn->result_types().front() &&
          staged_overload_fn && staged_overload_fn->arguments().size() == 1U &&
          staged_overload_fn->arguments().front().type() ==
              staged_overload_fn->result_types().front() &&
          residual_overload_fn && residual_overload_fn->ops().size() == 1U &&
          convert_word_8 != convert_words.end() &&
          residual_overload_fn->ops().front().callee().referenced_fn() ==
              *convert_word_8 &&
          residual_arguments_fn && residual_arguments_fn->ops().size() == 2U &&
          residual_arguments_fn->ops()[0].callee().binding<std::int64_t>(
              "tag") == std::optional<std::int64_t>{7} &&
          residual_arguments_fn->ops()[1].callee().binding<std::int64_t>(
              "tag") == std::optional<std::int64_t>{9} &&
          residual_variadic_fn && residual_variadic_fn->ops().size() == 1U &&
          residual_variadic_fn->ops().front().arguments().size() == 2U &&
          residual_dependent_fn && residual_dependent_fn->ops().size() == 1U &&
          residual_dependent_fn->ops().front().callee().referenced_fn() ==
              width_copy &&
          relay_fork_fn && relay_fork_fn->result_types().size() == 2U &&
          relay_fork_fn->ops().size() == 1U &&
          relay_fork_fn->ops().front().callee().referenced_fn() == fork &&
          relay_fork_fn->ops().front().results().size() == 2U &&
          width_evaluations == 1U,
      "structured compiler fns participate in dependent type "
      "evaluation and residual calls share overloads, named "
      "arguments, defaults, variadics, multi-results, and single "
      "evaluation");
  joggle::Diag signature_diagnostics;
  const std::string signature_text = joggle::format(*pipeline);
  const auto signature_roundtrip = joggle::parse_mod(
      signature_text, signature_diagnostics, "pipeline-canonical.joggle");
  if (!signature_roundtrip) {
    signature_diagnostics.print(std::cerr);
    std::cerr << signature_text;
  } else if (joggle::format(*signature_roundtrip) != signature_text) {
    std::cerr << "canonical signature changed after reparsing:\n"
              << signature_text << "reparsed:\n"
              << joggle::format(*signature_roundtrip);
  }
  ok &= expect(signature_roundtrip &&
                   joggle::format(*signature_roundtrip) == signature_text,
               "typed compiler-fn signatures format and parse "
               "canonically");
  const auto bits = integer->get<std::int64_t>("storage_bits");
  ok &= expect(bits && *bits == 8,
               "derived parameters share the ordinary Type query path");
  ok &= expect(canonicalize->form() == joggle::Mod::FnDecl::Form::External,
               "a native transformation uses an ordinary fn declaration");
  const auto input_revision = fn->revision();
  auto cleaned = compiler.run<joggle::Fn>(*clean, *fn);
  ok &= expect(cleaned && cleaned->ops().empty() &&
                   fn->revision() == input_revision && !fn->ops().empty(),
               "a typed Fn transform returns an isolated COW value");
  auto recomposed = compiler.run<joggle::Fn>(*clean, *fn);
  ok &= expect(recomposed.has_value(),
               "an imported transformation composes through an ordinary fn");
  if (recomposed) {
    fn = std::move(recomposed);
  }
  ok &= expect(fn->ops().empty(),
               "the native transformation removes redundant casts");

  joggle::Mod mod("compiler_pipeline", {1, 0, 0});
  auto mod_main = compiler.create_fn();
  joggle::Diag mod_diagnostics;
  if (!mod_main || !mod.insert("main", std::move(*mod_main), mod_diagnostics)) {
    return EXIT_FAILURE;
  }
  auto copied_mod = compiler.run<joggle::Mod>(*mod_identity, mod);
  const auto source_main = mod.fn("main");
  auto copied_main = copied_mod ? copied_mod->fn("main") : std::nullopt;
  if (!copied_mod || !source_main || !copied_main) {
    return EXIT_FAILURE;
  }
  {
    auto* copied_body = copied_mod->body(*copied_main);
    if (copied_body == nullptr) {
      return EXIT_FAILURE;
    }
    auto edit = copied_body->edit();
    const auto tail = edit.blk();
    edit.jump(copied_body->entry(), tail);
    edit.ret(tail);
    if (!edit.commit(mod_diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  copied_main = copied_mod->fn("main");
  ok &= expect(mod.fns().size() == 1U && copied_mod->fns().size() == 1U &&
                   source_main->body() != nullptr &&
                   source_main->body()->blks().size() == 1U && copied_main &&
                   copied_main->body() != nullptr &&
                   copied_main->body()->blks().size() == 2U &&
                   compiler.verify(*copied_mod),
               "the builtin mod value flows through an ordinary fn "
               "with deep-copy isolation");

  const auto i32 = compiler.make("i32");
  const auto attached_main = mod.fn("main");
  auto* attached_body = attached_main ? mod.body(*attached_main) : nullptr;
  const auto attached_revision =
      attached_body ? std::optional{attached_body->revision()} : std::nullopt;
  joggle::Diag attached_signature_diagnostics;
  bool changed_signature = false;
  if (attached_body && i32) {
    auto edit = attached_body->edit();
    edit.argument(*i32);
    changed_signature = edit.commit(attached_signature_diagnostics);
  }
  const auto unchanged_main = mod.fn("main");
  ok &= expect(i32 && attached_revision && !changed_signature &&
                   !attached_signature_diagnostics.ok() && unchanged_main &&
                   unchanged_main->body() &&
                   unchanged_main->body()->revision() == *attached_revision &&
                   compiler.verify(mod),
               "inserting a Fn fixes the member signature while failed "
               "body edits remain transactional");

  joggle::Compiler mod_materialization;
  mod_materialization.add(R"(
joggle 1;
mod source_model@1.0.0 {
  type word();
  fn keep(input: word) -> word;
  fn main(input: word) -> word {
    return keep(input);
  }
  fn count(input: word) -> int {
    return 1;
  }
}
)",
                          "source-model.joggle");
  const bool source_model_linked = mod_materialization.link();
  const auto source_model = mod_materialization.mod("source_model");
  const auto source_model_main =
      source_model ? source_model->fn("main") : std::nullopt;
  const auto materialized_model =
      source_model ? mod_materialization.materialize(*source_model)
                   : std::nullopt;
  const auto materialized_main =
      materialized_model ? materialized_model->fn("main") : std::nullopt;
  const auto materialized_count =
      materialized_model ? materialized_model->fn("count") : std::nullopt;
  const auto materialized_keep =
      materialized_model ? materialized_model->fn("keep") : std::nullopt;
  const auto materialized_calls = materialized_main && materialized_main->body()
                                      ? materialized_main->body()->ops()
                                      : std::vector<joggle::Op>{};
  ok &=
      expect(source_model_linked && source_model_main &&
                 source_model_main->body() == nullptr && materialized_main &&
                 materialized_main->body() != nullptr &&
                 materialized_main->body()->ops().size() == 1U &&
                 materialized_keep && materialized_calls.size() == 1U &&
                 materialized_calls.front().callee().referenced_fn() ==
                     materialized_keep &&
                 materialized_model->declaration_digest() ==
                     source_model->declaration_digest() &&
                 materialized_model->digest() != source_model->digest() &&
                 materialized_count && materialized_count->body() == nullptr &&
                 source_model_main->body() == nullptr,
             "a linked source Mod materializes into an isolated "
             "whole-Mod IR value with stable self references and without "
             "forcing compiler results into SSA");

  constexpr std::string_view guarded_source = R"(
    joggle 1;
    mod guarded@1.0.0 {
      type a();
      fn identity<T>(input: T) -> T;
      fn touch(input: fn) -> fn;
    }
  )";
  joggle::Compiler guarded_compiler;
  guarded_compiler.add(guarded_source, "guarded.joggle");
  if (!guarded_compiler.link()) {
    return EXIT_FAILURE;
  }
  const auto guarded = guarded_compiler.mod("guarded");
  const auto guarded_a_decl = guarded ? guarded->type("a") : std::nullopt;
  const auto guarded_identity =
      guarded ? guarded->fn("identity") : std::nullopt;
  const auto guarded_a =
      guarded_a_decl ? guarded_compiler.make(*guarded_a_decl) : std::nullopt;
  auto guarded_fn = guarded_compiler.create_fn();
  if (!guarded || !guarded_identity || !guarded_a || !guarded_fn) {
    return EXIT_FAILURE;
  }
  {
    auto edit = guarded_fn->edit();
    const auto input = edit.argument(*guarded_a);
    edit.call(*guarded_identity, {input});
    joggle::Diag diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  guarded_compiler.verify(
      *guarded_identity, [](const joggle::Op&, joggle::Diag& diagnostics) {
        diagnostics.report("guarded compiler-fn input rejected");
        return false;
      });
  bool transform_called = false;
  const auto guarded_touch = guarded->fn("touch");
  if (!guarded_touch) {
    return EXIT_FAILURE;
  }
  guarded_compiler.bind(*guarded_touch,
                        [&](joggle::Compiler&, joggle::Fn fn, joggle::Diag&) {
                          transform_called = true;
                          return fn;
                        });
  const auto guarded_result =
      guarded_compiler.run<joggle::Fn>("guarded.touch", *guarded_fn);
  ok &= expect(!guarded_result && !transform_called && !guarded_compiler.ok(),
               "a compiler fn does not execute on a Fn rejected "
               "by bound domain semantics");

  joggle::Compiler named_compiler;
  named_compiler.add("joggle 1; mod named@1.0.0 { "
                     "fn noop(input: fn) -> fn; }",
                     "named.joggle");
  const bool named_linked = named_compiler.link();
  const auto named_mod = named_compiler.mod("named");
  auto named_fn = named_compiler.create_fn();
  bool named_called = false;
  if (named_mod) {
    const auto noop = named_mod->fn("noop");
    if (!noop) {
      return EXIT_FAILURE;
    }
    named_compiler.bind(*noop,
                        [&](joggle::Compiler&, joggle::Fn fn, joggle::Diag&) {
                          named_called = true;
                          return fn;
                        });
  }
  const auto unqualified_result =
      named_linked && named_fn
          ? named_compiler.run<joggle::Fn>("noop", *named_fn)
          : std::optional<joggle::Fn>{};
  const auto named_diagnostics = named_compiler.diag().issues();
  ok &= expect(!unqualified_result && !named_called &&
                   !named_diagnostics.empty() &&
                   named_diagnostics.back().message.find("mod.member") !=
                       std::string::npos,
               "compiler-fn lookup requires one unambiguous qualified "
               "member name");

  joggle::Compiler mismatched_overload;
  mismatched_overload.add(R"(
joggle 1;
mod mismatched_overload@1.0.0 {
  fn choose(input: int) -> int;
  fn choose(input: string) -> string;
}
)",
                          "mismatched-overload.joggle");
  const bool mismatched_linked = mismatched_overload.link();
  const auto mismatched_mod = mismatched_overload.mod("mismatched_overload");
  bool mismatched_called = false;
  if (mismatched_mod) {
    mismatched_overload.bind(*mismatched_mod, "choose",
                             [&](std::int64_t input) {
                               mismatched_called = true;
                               return input;
                             });
    mismatched_overload.bind(*mismatched_mod, "choose", [&](std::string input) {
      mismatched_called = true;
      return input;
    });
  }
  const auto mismatched_result =
      mismatched_linked ? mismatched_overload.run<bool>(
                              "mismatched_overload.choose", std::int64_t{1})
                        : std::optional<bool>{};
  const bool reports_mismatched_invocation = std::any_of(
      mismatched_overload.diag().issues().begin(),
      mismatched_overload.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("matches the C++ invocation") !=
               std::string::npos;
      });
  ok &= expect(!mismatched_result && !mismatched_called &&
                   reports_mismatched_invocation,
               "named invocation selects an overload by its complete input "
               "and result signature");

  joggle::Compiler incompatible;
  incompatible.add(R"(
joggle 1;
mod incompatible@1.0.0 {
  fn read(input: bytes) -> fn;
  fn inspect(input: fn) -> int;
  fn broken(input: bytes) -> fn {
    return inspect(read(input));
  }
}
)",
                   "incompatible.joggle");
  ok &= expect(!incompatible.link() && !incompatible.ok(),
               "compiler-fn composition is checked by type");

  joggle::Compiler transactional;
  transactional.add(R"(
joggle 1;
mod transactional@1.0.0 {
  fn token() -> i32;
  fn mutate(input: fn) -> fn;
  fn reject(input: fn) -> bytes;
  fn pipeline(input: fn) -> bytes {
    return reject(mutate(input));
  }
}
)",
                    "transactional.joggle");
  const bool transactional_linked = transactional.link();
  const auto transactional_mod = transactional.mod("transactional");
  const auto token =
      transactional_mod ? transactional_mod->fn("token") : std::nullopt;
  const auto mutate =
      transactional_mod ? transactional_mod->fn("mutate") : std::nullopt;
  const auto reject =
      transactional_mod ? transactional_mod->fn("reject") : std::nullopt;
  const auto transaction =
      transactional_mod ? transactional_mod->fn("pipeline") : std::nullopt;
  auto transactional_fn = transactional.create_fn();
  if (!transactional_linked || !token || !mutate || !reject || !transaction ||
      !transactional_fn) {
    return EXIT_FAILURE;
  }
  transactional.bind(
      *mutate,
      [token = *token](joggle::Fn current,
                       joggle::Diag& diagnostics) -> std::optional<joggle::Fn> {
        auto edit = current.edit();
        edit.call(token);
        if (!edit.commit(diagnostics)) {
          return std::nullopt;
        }
        return current;
      });
  transactional.bind(*reject,
                     [](const joggle::Fn&) -> std::optional<joggle::Bytes> {
                       return std::nullopt;
                     });
  const auto rejected =
      transactional.run<joggle::Bytes>(*transaction, *transactional_fn);
  ok &= expect(!rejected && transactional_fn->ops().empty(),
               "typed sequence failure restores its existing Fn input");

  joggle::Compiler short_circuit;
  short_circuit.add(R"(
joggle 1;
mod short_circuit@1.0.0 {
  fn report(input: bytes) -> bytes;
  fn observe(input: bytes) -> bytes;
  fn pipeline(input: bytes) -> bytes {
    reported = report(input);
    return observe(reported);
  }
}
)",
                    "short-circuit.joggle");
  const bool short_circuit_linked = short_circuit.link();
  const auto short_circuit_mod = short_circuit.mod("short_circuit");
  const auto report =
      short_circuit_mod ? short_circuit_mod->fn("report") : std::nullopt;
  const auto short_observe =
      short_circuit_mod ? short_circuit_mod->fn("observe") : std::nullopt;
  const auto short_pipeline =
      short_circuit_mod ? short_circuit_mod->fn("pipeline") : std::nullopt;
  if (!short_circuit_linked || !report || !short_observe || !short_pipeline) {
    short_circuit.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  bool observed_after_failure = false;
  short_circuit.bind(*report,
                     [](joggle::Bytes input, joggle::Diag& diagnostics) {
                       diagnostics.report("native fn rejected its input");
                       return input;
                     });
  short_circuit.bind(*short_observe, [&](joggle::Bytes input) {
    observed_after_failure = true;
    return input;
  });
  const auto short_result =
      short_circuit.run<joggle::Bytes>(*short_pipeline, joggle::Bytes(3));
  const bool reports_native_failure = std::any_of(
      short_circuit.diag().issues().begin(),
      short_circuit.diag().issues().end(), [](const joggle::Issue& diagnostic) {
        return diagnostic.message == "native fn rejected its input" &&
               !diagnostic.notes.empty() &&
               diagnostic.notes.back().find(
                   "while calling 'short_circuit.report'") != std::string::npos;
      });
  ok &=
      expect(!short_result && !observed_after_failure && reports_native_failure,
             "a diagnostic from a native fn immediately stops its "
             "enclosing typed sequence");

  joggle::Compiler binding_mismatch;
  binding_mismatch.add("joggle 1; mod binding_mismatch@1.0.0 { "
                       "fn count(input: fn) -> int; }",
                       "binding-mismatch.joggle");
  const bool mismatch_linked = binding_mismatch.link();
  const auto binding_mod = binding_mismatch.mod("binding_mismatch");
  const auto count_fn = binding_mod ? binding_mod->fn("count") : std::nullopt;
  if (!mismatch_linked || !count_fn) {
    return EXIT_FAILURE;
  }
  binding_mismatch.bind(*count_fn,
                        [](const joggle::Fn&) { return std::string{"bad"}; });
  ok &= expect(!binding_mismatch.ok(),
               "C++ compiler-fn binding is checked against its "
               "declared type");

  joggle::Compiler legacy_transform;
  legacy_transform.add("joggle 1; mod legacy_transform@1.0.0 { "
                       "fn rewrite(input: fn) -> fn; }",
                       "legacy-transform.joggle");
  const bool legacy_linked = legacy_transform.link();
  const auto legacy_mod = legacy_transform.mod("legacy_transform");
  const auto legacy_rewrite =
      legacy_mod ? legacy_mod->fn("rewrite") : std::nullopt;
  if (!legacy_linked || !legacy_rewrite) {
    return EXIT_FAILURE;
  }
  legacy_transform.bind(*legacy_rewrite,
                        [](const joggle::Fn&) { return true; });
  ok &= expect(!legacy_transform.ok(),
               "a bool-returning callback cannot impersonate a declared "
               "fn -> fn result");

  joggle::Compiler represented;
  represented.add(R"(
joggle 1;
mod represented@1.0.0 {
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
  const auto represented_mod = represented.mod("represented");
  const auto target_type =
      represented_mod ? represented_mod->type("target") : std::nullopt;
  const auto estimate_type =
      represented_mod ? represented_mod->type("estimate") : std::nullopt;
  const auto measure =
      represented_mod ? represented_mod->fn("measure") : std::nullopt;
  const auto analyze =
      represented_mod ? represented_mod->fn("analyze") : std::nullopt;
  if (!represented_linked || !target_type || !estimate_type || !measure ||
      !analyze || !represented.represent<Target>(*target_type) ||
      !represented.represent<Estimate>(*estimate_type)) {
    represented.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  represented.bind(*measure, [](const Target& target) {
    return Estimate{.cycles = 1024 / target.lanes};
  });
  const auto estimate = represented.run<Estimate>(*analyze, Target{32});
  if (!estimate || !represented.ok()) {
    represented.diag().print(std::cerr);
  }
  ok &= expect(estimate && estimate->cycles == 32 && represented.ok(),
               "Mod types can use ordinary registered C++ values through "
               "composed compiler fns");

  joggle::Compiler missing_projection;
  missing_projection.add("joggle 1; mod parameterized_host@1.0.0 { "
                         "type target(lanes: int); }",
                         "parameterized-host.joggle");
  const bool missing_projection_linked = missing_projection.link();
  const auto missing_projection_mod =
      missing_projection.mod("parameterized_host");
  const auto missing_projection_type =
      missing_projection_mod ? missing_projection_mod->type("target")
                             : std::nullopt;
  const bool parameterized_rejected =
      missing_projection_linked && missing_projection_type &&
      !missing_projection.represent<Target>(*missing_projection_type);
  const bool reports_projection =
      std::any_of(missing_projection.diag().issues().begin(),
                  missing_projection.diag().issues().end(),
                  [](const joggle::Issue& diagnostic) {
                    return diagnostic.message.find("needs a projection") !=
                           std::string::npos;
                  });
  ok &= expect(parameterized_rejected && reports_projection,
               "host registration rejects parameterized schemas instead of "
               "discarding their type arguments");

  joggle::Compiler parameterized_host;
  parameterized_host.add(R"(
joggle 1;
mod parameterized_host@1.0.0 {
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
  const auto parameterized_mod = parameterized_host.mod("parameterized_host");
  const auto parameterized_target =
      parameterized_mod ? parameterized_mod->type("target") : std::nullopt;
  const auto parameterized_estimate =
      parameterized_mod ? parameterized_mod->type("estimate") : std::nullopt;
  const auto parameterized_measure =
      parameterized_mod ? parameterized_mod->fn("measure") : std::nullopt;
  const auto parameterized_analyze =
      parameterized_mod ? parameterized_mod->fn("analyze") : std::nullopt;
  const auto fixed =
      parameterized_mod ? parameterized_mod->fn("fixed") : std::nullopt;
  const auto wrong =
      parameterized_mod ? parameterized_mod->fn("wrong") : std::nullopt;
  std::size_t target_projections = 0;
  std::size_t estimate_projections = 0;
  if (!parameterized_linked || !parameterized_target ||
      !parameterized_estimate || !parameterized_measure ||
      !parameterized_analyze || !fixed || !wrong ||
      !parameterized_host.represent<Target>(*parameterized_target,
                                            [&](const Target& target) {
                                              ++target_projections;
                                              return std::tuple{target.lanes};
                                            }) ||
      !parameterized_host.represent<Estimate>(
          *parameterized_estimate, [&](const Estimate& estimate) {
            ++estimate_projections;
            return std::tuple{estimate.cycles};
          })) {
    parameterized_host.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  parameterized_host.bind(*parameterized_measure, [](const Target& target) {
    return Estimate{.cycles = target.lanes};
  });
  std::int64_t fixed_invocations = 0;
  parameterized_host.bind(*fixed, [&](const Target& target) {
    ++fixed_invocations;
    return Estimate{.cycles = target.lanes};
  });
  parameterized_host.bind(*wrong,
                          [](const Target&) { return Estimate{.cycles = 64}; });
  const auto parameterized_result =
      parameterized_host.run<Estimate>(*parameterized_analyze, Target{32});
  ok &= expect(parameterized_result && parameterized_result->cycles == 32 &&
                   target_projections == 1 && estimate_projections == 1,
               "a host projection preserves concrete type parameters through "
               "a composed generic compiler fn without repeating it");
  const auto rejected_input =
      parameterized_host.run<Estimate>(*fixed, Target{16});
  ok &= expect(!rejected_input && fixed_invocations == 0 &&
                   target_projections == 2 && estimate_projections == 1,
               "concrete projected input types are checked before native "
               "compiler code executes");
  const auto rejected_result =
      parameterized_host.run<Estimate>(*wrong, Target{32});
  ok &= expect(!rejected_result && target_projections == 3 &&
                   estimate_projections == 2,
               "a native compiler fn cannot return the wrong "
               "parameterized type instance or repeat a projection");

  joggle::Compiler lists;
  lists.add(R"(
joggle 1;
mod lists@1.0.0 {
  fn reverse(values: list<int>) -> list<int>;
  fn sum(values: list<int>) -> int;
}
)",
            "lists.joggle");
  const bool lists_linked = lists.link();
  const auto lists_mod = lists.mod("lists");
  const auto reverse = lists_mod ? lists_mod->fn("reverse") : std::nullopt;
  const auto sum = lists_mod ? lists_mod->fn("sum") : std::nullopt;
  if (!lists_linked || !reverse || !sum) {
    lists.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  lists.bind(*reverse, [](std::vector<std::int64_t> values) {
    std::reverse(values.begin(), values.end());
    return values;
  });
  lists.bind(*sum, [](const std::vector<std::int64_t>& values) {
    std::int64_t result = 0;
    for (const std::int64_t value : values) {
      result += value;
    }
    return result;
  });
  const auto reversed = lists.run<std::vector<std::int64_t>>(
      *reverse, std::vector<std::int64_t>{1, 2, 3});
  const auto empty_sum =
      lists.run<std::int64_t>(*sum, std::vector<std::int64_t>{});
  ok &= expect(reversed && *reversed == std::vector<std::int64_t>({3, 2, 1}) &&
                   empty_sum == std::optional<std::int64_t>{0},
               "list domains bind to ordinary std::vector values, including "
               "empty lists");

  joggle::Compiler mod_validation;
  mod_validation.add(R"(
joggle 1;
mod mod_validation@1.0.0 {
  type word();
  fn forbidden(input: word) -> word;
  fn identity(input: mod) -> mod;
  fn produce() -> mod;
}
)",
                     "mod-validation.joggle");
  const bool mod_validation_linked = mod_validation.link();
  const auto validation_mod = mod_validation.mod("mod_validation");
  const auto validation_word =
      validation_mod ? validation_mod->type("word") : std::nullopt;
  const auto forbidden =
      validation_mod ? validation_mod->fn("forbidden") : std::nullopt;
  const auto validation_identity =
      validation_mod ? validation_mod->fn("identity") : std::nullopt;
  const auto mod_produce =
      validation_mod ? validation_mod->fn("produce") : std::nullopt;
  auto invalid_body = mod_validation.create_fn();
  if (!mod_validation_linked || !validation_word || !forbidden ||
      !validation_identity || !mod_produce || !invalid_body) {
    mod_validation.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto word = mod_validation.make(*validation_word);
  joggle::Diag invalid_body_diagnostics;
  if (!word) {
    return EXIT_FAILURE;
  }
  {
    auto edit = invalid_body->edit();
    const auto input = edit.argument(*word);
    const auto call = edit.call(*forbidden, {input});
    edit.ret(invalid_body->entry(), {call.value()});
    if (!edit.commit(invalid_body_diagnostics)) {
      invalid_body_diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Mod invalid_mod("invalid_artifact", {1, 0, 0});
  if (!invalid_mod.insert("main", std::move(*invalid_body),
                          invalid_body_diagnostics)) {
    invalid_body_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  mod_validation.verify(
      *forbidden, [](const joggle::Op&, joggle::Diag& diagnostics) {
        diagnostics.report("forbidden call reached a Mod boundary");
        return false;
      });
  const bool public_mod_valid = mod_validation.verify(invalid_mod);
  bool identity_called = false;
  mod_validation.bind(*validation_identity, [&](joggle::Mod input) {
    identity_called = true;
    return input;
  });
  mod_validation.bind(*mod_produce, [invalid_mod] { return invalid_mod; });
  const auto rejected_mod_input =
      mod_validation.run<joggle::Mod>(*validation_identity, invalid_mod);
  const auto rejected_mod_output =
      mod_validation.run<joggle::Mod>(*mod_produce);
  const bool reports_invalid_member =
      std::any_of(mod_validation.diag().issues().begin(),
                  mod_validation.diag().issues().end(),
                  [](const joggle::Issue& diagnostic) {
                    return diagnostic.message.find(
                               "Mod fn 'main' is invalid") != std::string::npos;
                  });
  ok &=
      expect(!public_mod_valid && !rejected_mod_input && !rejected_mod_output &&
                 !identity_called && reports_invalid_member,
             "typed Mod boundaries verify every materialized Fn");

  joggle::Compiler bounded({16U, 8U});
  bounded.add(R"(
joggle 1;
mod bounded@1.0.0 {
  fn spin(input: bytes) -> bytes {
    while true {
    }
    return input;
  }
}
)",
              "bounded.joggle");
  const bool bounded_linked = bounded.link();
  const auto bounded_mod = bounded.mod("bounded");
  const auto spin = bounded_mod ? bounded_mod->fn("spin") : std::nullopt;
  const auto spinning = bounded_linked && spin
                            ? bounded.run<joggle::Bytes>(*spin, joggle::Bytes{})
                            : std::optional<joggle::Bytes>{};
  const bool reports_budget = std::any_of(
      bounded.diag().issues().begin(), bounded.diag().issues().end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("step limit") != std::string::npos;
      });
  ok &= expect(!spinning && reports_budget,
               "structured compiler execution is bounded deterministically");

  joggle::Compiler unchecked_arm;
  unchecked_arm.add(R"(
joggle 1;
mod unchecked_arm@1.0.0 {
  fn broken(condition: bool, input: bytes) -> bytes {
    if condition {
      return input;
    } else {
      return missing(input);
    }
  }
}
)",
                    "unchecked-arm.joggle");
  const bool unchecked_linked = unchecked_arm.link();
  const bool reports_unselected_call = std::any_of(
      unchecked_arm.diag().issues().begin(),
      unchecked_arm.diag().issues().end(), [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("no visible overload of 'missing'") !=
               std::string::npos;
      });
  ok &= expect(!unchecked_linked && reports_unselected_call,
               "linking verifies calls in every structured branch before "
               "Known execution selects one");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
