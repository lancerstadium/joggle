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

  fn floating_root(input: f32) -> f32 {
    return arith.sqrt(input);
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
  const auto floating_root =
      compiler.materialize("custom_numeric.floating_root");
  if (!integer || !logical || !index_equality || !logical_equality ||
      !floating_root) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto integer_ops = integer->ops();
  const auto logical_ops = logical->ops();
  bool ok = true;
  ok &= expect(integer_ops.size() == 4U &&
                   integer_ops[0].callee().symbol().qualified_name() ==
                       "arith.bitwise_and" &&
                   integer_ops[1].callee().symbol().qualified_name() ==
                       "arith.bitwise_not" &&
                   integer_ops[2].callee().symbol().qualified_name() ==
                       "arith.shift_left" &&
                   integer_ops[3].callee().symbol().qualified_name() ==
                       "arith.less",
               "custom low-bit integers reuse reflected arithmetic operators");
  ok &= expect(logical_ops.size() == 3U &&
                   logical_ops[0].callee().symbol().qualified_name() ==
                       "arith.logical_not" &&
                   logical_ops[1].callee().symbol().qualified_name() ==
                       "arith.logical_and" &&
                   logical_ops[2].callee().symbol().qualified_name() ==
                       "arith.logical_or",
               "nested logical expressions preserve operator precedence");
  ok &= expect(index_equality->ops().size() == 1U &&
                   index_equality->ops()
                           .front()
                           .callee()
                           .symbol()
                           .qualified_name() == "arith.equal" &&
                   logical_equality->ops().size() == 1U &&
                   logical_equality->ops()
                           .front()
                           .callee()
                           .symbol()
                           .qualified_name() == "arith.logical_not_equal",
               "numeric and logical equality overloads include index and i1");
  ok &= expect(floating_root->ops().size() == 1U &&
                   floating_root->ops()
                           .front()
                           .callee()
                           .symbol()
                           .qualified_name() == "arith.sqrt",
               "floating-point square root remains an ordinary typed call");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
