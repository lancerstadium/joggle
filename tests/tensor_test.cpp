#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

std::string callee(const joggle::Op& op) {
  const auto fn = op.callee().referenced_fn();
  return fn ? std::string(fn->name()) : std::string{};
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MOD);
  compiler.load(JOGGLE_TENSOR_MOD);
  compiler.load(JOGGLE_NN_MOD);
  compiler.add(R"(
joggle 1;

mod tensor_use@1.0.0 {
  import nn@1 as n;
  import tensor@4 as t;

  fn transpose(
    input: t.tensor<f32, [2, 3]>
  ) -> t.tensor<f32, [3, 2]> {
    return t.compute([3, 2], (row, column) => input[column, row]);
  }

  fn product(
    lhs: t.tensor<f32, [2, 4]>,
    rhs: t.tensor<f32, [4, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    return n.matmul(lhs, rhs);
  }
}
)",
               "tensor-use.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto tensor = compiler.mod("tensor");
  const auto declarations = tensor ? tensor->fns()
                                   : std::vector<joggle::Mod::FnDecl>{};
  const std::vector<std::string> names = [&] {
    std::vector<std::string> result;
    for (const auto& fn : declarations) {
      result.emplace_back(fn.name());
    }
    return result;
  }();
  ok &= expect(names == std::vector<std::string>({"compute", "[]", "map",
                                                  "reduce"}),
               "Tensor has one small frontend-neutral algebra");

  const auto transpose = compiler.materialize("tensor_use.transpose");
  if (!transpose) {
    compiler.diag().print(std::cerr);
  }
  const auto transpose_ops = transpose ? transpose->ops()
                                       : std::vector<joggle::Op>{};
  const auto callback = transpose_ops.size() == 1U &&
                                transpose_ops.front().arguments().size() == 1U
                            ? transpose_ops.front().arguments().front().inline_fn()
                            : std::optional<joggle::Fn>{};
  ok &= expect(transpose && transpose_ops.size() == 1U &&
                   callee(transpose_ops.front()) == "compute" && callback &&
                   callback->arguments().size() == 3U &&
                   callback->ops().size() == 1U &&
                   callee(callback->ops().front()) == "[]" &&
                   callback->ops().front().arguments().size() == 3U &&
                   compiler.verify(*transpose) && compiler.verify(*callback),
               "shape context infers lambda arity and multi-index access");

  const auto product = compiler.materialize("tensor_use.product");
  const auto product_ops = product ? product->ops() : std::vector<joggle::Op>{};
  const auto matmul = product_ops.size() == 1U
                          ? compiler.materialize(product_ops.front())
                          : std::optional<joggle::Fn>{};
  if (!product || !matmul) {
    compiler.diag().print(std::cerr);
  }
  const auto matmul_ops = matmul ? matmul->ops() : std::vector<joggle::Op>{};
  ok &= expect(product && product_ops.size() == 1U &&
                   callee(product_ops.front()) == "matmul" && matmul &&
                   matmul_ops.size() == 2U && callee(matmul_ops.front()) == "zero" &&
                   callee(matmul_ops.back()) == "compute" &&
                   compiler.verify(*matmul),
               "NN matmul owns a real body over the Tensor algebra");

  const std::string formatted = tensor ? joggle::format(*tensor) : std::string{};
  ok &= expect(formatted.find("coord") == std::string::npos &&
                   formatted.find("fn compute") != std::string::npos &&
                   formatted.find("indices: index...") != std::string::npos,
               "the public source has no coordinate wrapper burden");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
