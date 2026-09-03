#include <cstdlib>
#include <iostream>
#include <string_view>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.add(R"(
joggle 1;

module custom_numeric@1.0.0 {
  import arith@2.0.0;

  type nibble() : prelude.scalar, prelude.numeric, prelude.integer, prelude.unsigned_integer {
    storage_bits = 4;
  }

  fn integer_operators(lhs: nibble, rhs: nibble) -> (nibble, i1) {
    mixed = ~(lhs & rhs) << rhs;
    ordered = lhs < rhs;
    return mixed, ordered;
  }

  fn logical_operators(lhs: i1, rhs: i1) -> i1 {
    return !lhs || rhs && lhs;
  }

  fn index_equality(lhs: index, rhs: index) -> i1 {
    return lhs == rhs;
  }

  fn logical_equality(lhs: i1, rhs: i1) -> i1 {
    return lhs != rhs;
  }
}
)",
               "custom-numeric.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto integer = compiler.materialize("custom_numeric.integer_operators");
  const auto logical = compiler.materialize("custom_numeric.logical_operators");
  const auto index_equality =
      compiler.materialize("custom_numeric.index_equality");
  const auto logical_equality =
      compiler.materialize("custom_numeric.logical_equality");
  if (!integer || !logical || !index_equality || !logical_equality) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto integer_instructions = integer->instructions();
  const auto logical_instructions = logical->instructions();
  bool ok = true;
  ok &= expect(integer_instructions.size() == 4U &&
                   integer_instructions[0].callee().symbol().qualified_name() ==
                       "arith.bitwise_and" &&
                   integer_instructions[1].callee().symbol().qualified_name() ==
                       "arith.bitwise_not" &&
                   integer_instructions[2].callee().symbol().qualified_name() ==
                       "arith.shift_left" &&
                   integer_instructions[3].callee().symbol().qualified_name() ==
                       "arith.less",
               "custom low-bit integers reuse reflected arithmetic operators");
  ok &= expect(logical_instructions.size() == 3U &&
                   logical_instructions[0].callee().symbol().qualified_name() ==
                       "arith.logical_not" &&
                   logical_instructions[1].callee().symbol().qualified_name() ==
                       "arith.logical_and" &&
                   logical_instructions[2].callee().symbol().qualified_name() ==
                       "arith.logical_or",
               "nested logical expressions preserve operator precedence");
  ok &= expect(index_equality->instructions().size() == 1U &&
                   index_equality->instructions()
                           .front()
                           .callee()
                           .symbol()
                           .qualified_name() == "arith.equal" &&
                   logical_equality->instructions().size() == 1U &&
                   logical_equality->instructions()
                           .front()
                           .callee()
                           .symbol()
                           .qualified_name() == "arith.logical_not_equal",
               "numeric and logical equality overloads include index and i1");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
