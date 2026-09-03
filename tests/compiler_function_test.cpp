#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
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

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TEST_MODULE);
  compiler.add(R"(
joggle 1;
module pipeline@1.0.0 {
  import test_ir@1;
  type word(width: int);
  type flag(enabled: bool);
  fn read(input: bytes) -> function;
  fn inspect(input: function) -> int;
  fn emit(input: function) -> bytes;
  fn consume(input: bytes);
  fn append(input: bytes) -> bytes;
  fn nonzero(input: int) -> bool;
  fn twice(input: int) -> int;
  fn twice(input: string) -> string;
  fn choose_amount(value: int, amount: int = 2) -> int {
    return amount;
  }
  fn add_offset(lhs: int, rhs: int) -> int as +;
  fn less(lhs: int, rhs: int) -> bool as <;
  fn text_less(lhs: string, rhs: string) -> bool as <;
  fn less_equal(lhs: int, rhs: int) -> bool as <=;
  fn greater(lhs: int, rhs: int) -> bool as >;
  fn greater_equal(lhs: int, rhs: int) -> bool as >=;
  fn equal(lhs: int, rhs: int) -> bool as ==;
  fn not_equal(lhs: int, rhs: int) -> bool as !=;
  fn logical_not(input: bool) -> bool as !;
  fn logical_and(lhs: bool, rhs: bool) -> bool as && {
    return if lhs { rhs } else { false };
  }
  fn logical_or(lhs: bool, rhs: bool) -> bool as || {
    return if lhs { true } else { rhs };
  }
  fn module_identity(input: module) -> module;
  fn convert_word(input: test_ir.integer<8>) -> test_ir.integer<8>;
  fn convert_word(input: test_ir.integer<16>) -> test_ir.integer<16>;
  fn configured_copy(input: test_ir.integer<8>, tag: int = 7)
      -> test_ir.integer<8>;
  fn choose_first<T: type>(items: T...) -> T;
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
  fn clean(input: function) -> function {
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
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto test_ir = compiler.module("test_ir");
  const auto pipeline = compiler.module("pipeline");
  if (!test_ir || !compiler.load_behavior("test_ir", JOGGLE_TEST_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto integer_decl = test_ir ? test_ir->type("integer") : std::nullopt;
  const auto arith_cast_decl =
      test_ir ? test_ir->function("cast") : std::nullopt;
  const auto format_decl =
      test_ir ? test_ir->interface("numeric_format") : std::nullopt;
  const auto canonicalize =
      test_ir ? test_ir->function("canonicalize") : std::nullopt;
  const auto clean = pipeline ? pipeline->function("clean") : std::nullopt;
  const auto read = pipeline ? pipeline->function("read") : std::nullopt;
  const auto resolved_read = compiler.lookup("pipeline.read");
  const auto emit = pipeline ? pipeline->function("emit") : std::nullopt;
  const auto inspect =
      pipeline ? pipeline->function("inspect") : std::nullopt;
  const auto compile =
      pipeline ? pipeline->function("compile") : std::nullopt;
  const auto consume =
      pipeline ? pipeline->function("consume") : std::nullopt;
  const auto module_identity =
      pipeline ? pipeline->function("module_identity") : std::nullopt;
  const auto convert_words = pipeline
                                 ? pipeline->overloads("convert_word")
                                 : std::vector<joggle::Module::Function>{};
  const auto configured_copy =
      pipeline ? pipeline->function("configured_copy") : std::nullopt;
  const auto compute_width =
      pipeline ? pipeline->function("compute_width") : std::nullopt;
  const auto width_copy =
      pipeline ? pipeline->function("width_copy") : std::nullopt;
  const auto residual_overload =
      pipeline ? pipeline->function("residual_overload") : std::nullopt;
  const auto residual_arguments =
      pipeline ? pipeline->function("residual_arguments") : std::nullopt;
  const auto residual_variadic =
      pipeline ? pipeline->function("residual_variadic") : std::nullopt;
  const auto residual_dependent =
      pipeline ? pipeline->function("residual_dependent") : std::nullopt;
  const auto append = pipeline ? pipeline->function("append") : std::nullopt;
  const auto nonzero =
      pipeline ? pipeline->function("nonzero") : std::nullopt;
  const auto select = pipeline ? pipeline->function("select") : std::nullopt;
  const auto repeat = pipeline ? pipeline->function("repeat") : std::nullopt;
  const auto choose = pipeline ? pipeline->function("choose") : std::nullopt;
  const auto once = pipeline ? pipeline->function("once") : std::nullopt;
  const auto last = pipeline ? pipeline->function("last") : std::nullopt;
  const auto typed = pipeline ? pipeline->function("typed") : std::nullopt;
  const auto twice = pipeline ? pipeline->overloads("twice")
                              : std::vector<joggle::Module::Function>{};
  const auto add_offset =
      pipeline ? pipeline->function("add_offset") : std::nullopt;
  const auto less = pipeline ? pipeline->function("less") : std::nullopt;
  const auto text_less =
      pipeline ? pipeline->function("text_less") : std::nullopt;
  const auto less_equal =
      pipeline ? pipeline->function("less_equal") : std::nullopt;
  const auto greater =
      pipeline ? pipeline->function("greater") : std::nullopt;
  const auto greater_equal =
      pipeline ? pipeline->function("greater_equal") : std::nullopt;
  const auto equal = pipeline ? pipeline->function("equal") : std::nullopt;
  const auto not_equal =
      pipeline ? pipeline->function("not_equal") : std::nullopt;
  const auto logical_not =
      pipeline ? pipeline->function("logical_not") : std::nullopt;
  const auto earlier =
      pipeline ? pipeline->function("earlier") : std::nullopt;
  const auto invert = pipeline ? pipeline->function("invert") : std::nullopt;
  const auto ordered_typed =
      pipeline ? pipeline->function("ordered_typed") : std::nullopt;
  const auto unequal_order =
      pipeline ? pipeline->function("unequal_order") : std::nullopt;
  const auto relation_typed =
      pipeline ? pipeline->function("relation_typed") : std::nullopt;
  const auto text_relation_typed =
      pipeline ? pipeline->function("text_relation_typed") : std::nullopt;
  const auto overload_typed =
      pipeline ? pipeline->function("overload_typed") : std::nullopt;
  const auto default_typed =
      pipeline ? pipeline->function("default_typed") : std::nullopt;
  const auto staged_overload =
      pipeline ? pipeline->function("staged_overload") : std::nullopt;
  const auto use_twice =
      pipeline ? pipeline->function("use_twice") : std::nullopt;
  const auto use_operator =
      pipeline ? pipeline->function("use_operator") : std::nullopt;
  const auto divide = pipeline ? pipeline->function("divide") : std::nullopt;
  const auto divide_exact =
      pipeline ? pipeline->function("divide_exact") : std::nullopt;
  const auto observe =
      pipeline ? pipeline->function("observe") : std::nullopt;
  const auto observe_once =
      pipeline ? pipeline->function("observe_once") : std::nullopt;
  const auto fork = pipeline ? pipeline->function("fork") : std::nullopt;
  const auto relay_fork =
      pipeline ? pipeline->function("relay_fork") : std::nullopt;
  if (!integer_decl || !arith_cast_decl || !format_decl || !canonicalize ||
      !clean || !read || !resolved_read || !emit || !inspect || !compile || !consume ||
      !module_identity || convert_words.size() != 2U || !configured_copy ||
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
  ok &= expect(resolved_read->symbol() == read->symbol(),
               "qualified compiler lookup returns the same Function member "
               "used by typed invocation");
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
               "compiler functions reflect functional types");

  compiler.bind(*read,
                [](joggle::Compiler& current, const joggle::Bytes& bytes)
                    -> std::optional<joggle::ir::Function> {
                  return bytes.empty() ? std::nullopt : current.function();
                });
  compiler.bind(
      *inspect, [](const joggle::ir::Function& current) -> std::int64_t {
        return static_cast<std::int64_t>(current.instructions().size());
      });
  compiler.bind(
      *emit, [](const joggle::ir::Function& current) -> joggle::Bytes {
        return {static_cast<std::byte>(current.instructions().size())};
      });
  compiler.bind(*canonicalize, [arith_cast_decl](
                                   joggle::ir::Function& current,
                                   joggle::Diagnostics& diagnostics) {
    auto edit = current.edit();
    for (const joggle::ir::Instruction& instruction : current.instructions()) {
      if (instruction.callee() != *arith_cast_decl) {
        continue;
      }
      edit.replace(instruction.result(0), instruction.arguments().front());
      edit.erase(instruction);
    }
    return edit.commit(diagnostics);
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
  for (const auto& overload : twice) {
    if (overload.inputs().front().domain ==
        joggle::Module::Expression::reference("int")) {
      compiler.bind(overload, [](std::int64_t input) { return input * 2; });
    } else {
      compiler.bind(overload, [](std::string input) { return input + input; });
    }
  }
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
      joggle::HostEvaluation::Hermetic);
  ok &= expect(
      compiler.invocable<joggle::Module, joggle::Module>(*module_identity) &&
          compiler.invocable<joggle::ir::Function, joggle::ir::Function&>(
              *clean) &&
          !compiler.invocable<joggle::Module, joggle::ir::Function&>(*clean) &&
          compiler.invocable<std::tuple<std::int64_t, bool>, std::int64_t>(
              *divide) &&
          !compiler.invocable<std::int64_t, std::int64_t>(*divide) &&
          compiler.invocable<void, std::int64_t>(*observe),
      "typed invocability distinguishes whole-Module and single-Function "
      "transforms and checks complete result sequences");
  compiler.bind(*module_identity, [](joggle::Module input) { return input; });
  const joggle::Bytes encoded{std::byte{0x42}};
  auto decoded = compiler.run<joggle::ir::Function>(*read, encoded);
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
  ok &= expect(selected_once && selected_once->size() == 1U && selected_twice &&
                   selected_twice->size() == 2U && repeated &&
                   repeated->size() == 3U && chosen && chosen->size() == 1U &&
                   chosen->front() == std::byte{0x02} && broken &&
                   broken->size() == 1U && continued &&
                   continued->size() == 1U && append_calls == 8U &&
                   overloaded == std::optional<std::int64_t>{12} &&
                   operated == std::optional<std::int64_t>{105} &&
                   ordered == std::optional<std::int64_t>{7} &&
                   inverted == std::optional<bool>{false} &&
                   ascending == std::optional<bool>{true} &&
                   descending == std::optional<bool>{true} &&
                   equal_order == std::optional<bool>{false} && divided &&
                   *divided == std::tuple<std::int64_t, bool>{4, true} &&
                   observed_once && observed == std::optional<std::int64_t>{13},
               "structured compiler functions execute selected branches, "
               "loops, overloads, typed operators, zero-result calls, and "
               "multi-result calls");
  const auto typed_function = compiler.function(*typed);
  const auto ordered_typed_function = compiler.function(*ordered_typed);
  const auto relation_typed_function = compiler.function(*relation_typed);
  const auto text_relation_typed_function =
      compiler.function(*text_relation_typed);
  const auto overload_typed_function = compiler.function(*overload_typed);
  const auto default_typed_function = compiler.function(*default_typed);
  const auto staged_overload_function = compiler.function(*staged_overload);
  const auto residual_overload_function = compiler.function(*residual_overload);
  const auto residual_arguments_function =
      compiler.function(*residual_arguments);
  const auto residual_variadic_function = compiler.function(*residual_variadic);
  const auto residual_dependent_function =
      compiler.function(*residual_dependent);
  const auto relay_fork_function = compiler.function(*relay_fork);
  const auto convert_word_8 = std::find_if(
      convert_words.begin(), convert_words.end(), [](const auto& function) {
        const auto& domain = function.inputs().front().domain;
        return !domain.arguments.empty() &&
               domain.arguments.front().text == "8";
      });
  ok &= expect(
      typed_function && typed_function->arguments().size() == 1U &&
          typed_function->result_types().size() == 1U &&
          typed_function->arguments().front().type() ==
              typed_function->result_types().front() &&
          ordered_typed_function &&
          ordered_typed_function->arguments().size() == 1U &&
          ordered_typed_function->result_types().size() == 1U &&
          ordered_typed_function->arguments().front().type() ==
              ordered_typed_function->result_types().front() &&
          relation_typed_function &&
          relation_typed_function->arguments().size() == 1U &&
          relation_typed_function->result_types().size() == 1U &&
          relation_typed_function->arguments().front().type() ==
              relation_typed_function->result_types().front() &&
          text_relation_typed_function &&
          text_relation_typed_function->arguments().size() == 1U &&
          text_relation_typed_function->result_types().size() == 1U &&
          text_relation_typed_function->arguments().front().type() ==
              text_relation_typed_function->result_types().front() &&
          overload_typed_function &&
          overload_typed_function->arguments().size() == 1U &&
          overload_typed_function->arguments().front().type() ==
              overload_typed_function->result_types().front() &&
          default_typed_function &&
          default_typed_function->arguments().size() == 1U &&
          default_typed_function->arguments().front().type() ==
              default_typed_function->result_types().front() &&
          staged_overload_function &&
          staged_overload_function->arguments().size() == 1U &&
          staged_overload_function->arguments().front().type() ==
              staged_overload_function->result_types().front() &&
          residual_overload_function &&
          residual_overload_function->instructions().size() == 1U &&
          convert_word_8 != convert_words.end() &&
          residual_overload_function->instructions().front().callee() ==
              *convert_word_8 &&
          residual_arguments_function &&
          residual_arguments_function->instructions().size() == 2U &&
          residual_arguments_function->instructions()[0]
                  .arguments()[1]
                  .get<std::int64_t>() == std::optional<std::int64_t>{7} &&
          residual_arguments_function->instructions()[1]
                  .arguments()[1]
                  .get<std::int64_t>() == std::optional<std::int64_t>{9} &&
          residual_variadic_function &&
          residual_variadic_function->instructions().size() == 1U &&
          residual_variadic_function->instructions()
                  .front()
                  .arguments()
                  .size() == 2U &&
          residual_dependent_function &&
          residual_dependent_function->instructions().size() == 1U &&
          residual_dependent_function->instructions().front().callee() ==
              *width_copy &&
          relay_fork_function &&
          relay_fork_function->result_types().size() == 2U &&
          relay_fork_function->instructions().size() == 1U &&
          relay_fork_function->instructions().front().callee() == *fork &&
          relay_fork_function->instructions().front().results().size() == 2U &&
          width_evaluations == 1U,
      "structured compiler functions participate in dependent type "
      "evaluation and residual calls share overloads, named "
      "arguments, defaults, variadics, multi-results, and single "
      "evaluation");
  joggle::Diagnostics signature_diagnostics;
  const std::string signature_text = joggle::format(*pipeline);
  const auto signature_roundtrip = joggle::parse_module(
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
               "typed compiler-function signatures format and parse "
               "canonically");
  const auto bits = integer->get<std::int64_t>("storage_bits");
  ok &= expect(bits && *bits == 8,
               "derived parameters share the ordinary Type query path");
  ok &= expect(canonicalize->form() ==
                   joggle::Module::Function::Form::External,
               "a native transformation uses an ordinary function declaration");
  ok &= expect(compiler.run(*function, *clean),
               "an imported transformation composes through an ordinary fn");
  ok &= expect(function->instructions().empty(),
               "the native transformation removes redundant casts");

  joggle::Module module("compiler_pipeline", {1, 0, 0});
  auto module_main = compiler.function();
  joggle::Diagnostics module_diagnostics;
  if (!module_main ||
      !module.insert("main", std::move(*module_main), module_diagnostics)) {
    return EXIT_FAILURE;
  }
  auto copied_module = compiler.run<joggle::Module>(*module_identity, module);
  const auto i32 = compiler.make("i32");
  if (!copied_module || !i32 || !copied_module->function("main")) {
    return EXIT_FAILURE;
  }
  {
    auto edit = copied_module->body("main")->edit();
    edit.argument(*i32);
    if (!edit.commit(module_diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  ok &= expect(module.functions().size() == 1U &&
                   copied_module->functions().size() == 1U &&
                   module.body("main") != nullptr &&
                   module.body("main")->arguments().empty() &&
                   copied_module->body("main")->arguments().size() == 1U,
               "the builtin module value flows through an ordinary fn "
               "with deep-copy isolation");

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
  auto guarded_function = guarded_compiler.function();
  if (!guarded || !guarded_identity || !guarded_a || !guarded_function) {
    return EXIT_FAILURE;
  }
  {
    auto edit = guarded_function->edit();
    const auto input = edit.argument(*guarded_a);
    edit.append(*guarded_identity, {input});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  guarded_compiler.bind(
      *guarded_identity,
      [](const joggle::ir::Instruction&, joggle::Diagnostics& diagnostics) {
        diagnostics.report("guarded compiler-function input rejected");
        return false;
      });
  bool transform_called = false;
  const auto guarded_touch = guarded->function("touch");
  if (!guarded_touch) {
    return EXIT_FAILURE;
  }
  guarded_compiler.bind(
      *guarded_touch,
      [&](joggle::Compiler&, joggle::ir::Function&, joggle::Diagnostics&) {
        transform_called = true;
        return true;
      });
  ok &= expect(!guarded_compiler.run(*guarded_function, "guarded.touch") &&
                   !transform_called && !guarded_compiler.ok(),
               "a compiler function does not execute on a Function rejected "
               "by bound domain semantics");

  joggle::Compiler named_compiler;
  named_compiler.add("joggle 1; module named@1.0.0 { "
                     "fn noop(input: function) -> function; }",
                     "named.joggle");
  const bool named_linked = named_compiler.link();
  const auto named_module = named_compiler.module("named");
  auto named_function = named_compiler.function();
  bool named_called = false;
  if (named_module) {
    const auto noop = named_module->function("noop");
    if (!noop) {
      return EXIT_FAILURE;
    }
    named_compiler.bind(*noop, [&](joggle::Compiler&, joggle::ir::Function&,
                                   joggle::Diagnostics&) {
      named_called = true;
      return true;
    });
  }
  const bool unqualified_run = named_linked && named_function &&
                               named_compiler.run(*named_function, "noop");
  const auto named_diagnostics = named_compiler.diagnostics().entries();
  ok &=
      expect(!unqualified_run && !named_called && !named_diagnostics.empty() &&
                 named_diagnostics.back().message.find("module.member") !=
                     std::string::npos,
             "compiler-function lookup requires one unambiguous qualified "
             "member name");

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
               "compiler-function composition is checked by type");

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
  auto transactional_function = transactional.function();
  if (!transactional_linked || !token || !mutate || !reject || !transaction ||
      !transactional_function) {
    return EXIT_FAILURE;
  }
  transactional.bind(*mutate,
                     [token = *token](joggle::ir::Function& current,
                                      joggle::Diagnostics& diagnostics) {
                       auto edit = current.edit();
                       edit.append(token);
                       return edit.commit(diagnostics);
                     });
  transactional.bind(
      *reject, [](const joggle::ir::Function&) -> std::optional<joggle::Bytes> {
        return std::nullopt;
      });
  const auto rejected =
      transactional.run<joggle::Bytes>(*transaction, *transactional_function);
  ok &= expect(!rejected && transactional_function->instructions().empty(),
               "typed sequence failure restores its existing Function input");

  joggle::Compiler binding_mismatch;
  binding_mismatch.add("joggle 1; module binding_mismatch@1.0.0 { "
                       "fn count(input: function) -> int; }",
                       "binding-mismatch.joggle");
  const bool mismatch_linked = binding_mismatch.link();
  const auto binding_module = binding_mismatch.module("binding_mismatch");
  const auto count_function =
      binding_module ? binding_module->function("count") : std::nullopt;
  if (!mismatch_linked || !count_function) {
    return EXIT_FAILURE;
  }
  binding_mismatch.bind(*count_function, [](const joggle::ir::Function&) {
    return std::string{"bad"};
  });
  ok &= expect(!binding_mismatch.ok(),
               "C++ compiler-function binding is checked against its "
               "declared type");

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
  const auto target_type =
      represented_module ? represented_module->type("target") : std::nullopt;
  const auto estimate_type =
      represented_module ? represented_module->type("estimate") : std::nullopt;
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
  missing_projection.add("joggle 1; module parameterized_host@1.0.0 { "
                         "type target(lanes: int); }",
                         "parameterized-host.joggle");
  const bool missing_projection_linked = missing_projection.link();
  const auto missing_projection_module =
      missing_projection.module("parameterized_host");
  const auto missing_projection_type =
      missing_projection_module ? missing_projection_module->type("target")
                                : std::nullopt;
  const bool parameterized_rejected =
      missing_projection_linked && missing_projection_type &&
      !missing_projection.represent<Target>(*missing_projection_type);
  const bool reports_projection =
      std::any_of(missing_projection.diagnostics().entries().begin(),
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
  const auto parameterized_target = parameterized_module
                                        ? parameterized_module->type("target")
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
          *parameterized_estimate, [](const Estimate& estimate) {
            return std::tuple{estimate.cycles};
          })) {
    parameterized_host.diagnostics().print(std::cerr);
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

  joggle::Compiler lists;
  lists.add(R"(
joggle 1;
module lists@1.0.0 {
  fn reverse(values: list<int>) -> list<int>;
  fn sum(values: list<int>) -> int;
}
)",
            "lists.joggle");
  const bool lists_linked = lists.link();
  const auto lists_module = lists.module("lists");
  const auto reverse =
      lists_module ? lists_module->function("reverse") : std::nullopt;
  const auto sum =
      lists_module ? lists_module->function("sum") : std::nullopt;
  if (!lists_linked || !reverse || !sum) {
    lists.diagnostics().print(std::cerr);
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

  joggle::Compiler bounded({16U, 8U});
  bounded.add(R"(
joggle 1;
module bounded@1.0.0 {
  fn spin(input: bytes) -> bytes {
    while true {
    }
    return input;
  }
}
)",
              "bounded.joggle");
  const bool bounded_linked = bounded.link();
  const auto bounded_module = bounded.module("bounded");
  const auto spin =
      bounded_module ? bounded_module->function("spin") : std::nullopt;
  const auto spinning = bounded_linked && spin
                            ? bounded.run<joggle::Bytes>(*spin, joggle::Bytes{})
                            : std::optional<joggle::Bytes>{};
  const bool reports_budget = std::any_of(
      bounded.diagnostics().entries().begin(),
      bounded.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("step limit") != std::string::npos;
      });
  ok &= expect(!spinning && reports_budget,
               "structured compiler execution is bounded deterministically");

  joggle::Compiler unchecked_arm;
  unchecked_arm.add(R"(
joggle 1;
module unchecked_arm@1.0.0 {
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
      unchecked_arm.diagnostics().entries().begin(),
      unchecked_arm.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("no visible overload of 'missing'") !=
               std::string::npos;
      });
  ok &= expect(!unchecked_linked && reports_unselected_call,
               "linking verifies calls in every structured branch before "
               "Known execution selects one");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
