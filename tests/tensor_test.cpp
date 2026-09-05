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
  import tensor@5 as t;

  fn transpose(
    input: t.tensor<f32, [2, 3]>
  ) -> t.tensor<f32, [3, 2]> {
    return t.compute([3, 2], (at) => input[at[1], at[0]]);
  }

  fn product(
    lhs: t.tensor<f32, [2, 4]>,
    rhs: t.tensor<f32, [4, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    return n.matmul(lhs, rhs);
  }

  fn activate(
    input: t.tensor<f32, [2, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    return n.relu(input);
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
  ok &= expect(
      names == std::vector<std::string>({"compute", "[]", "[]", "[]"}),
               "Tensor has construction and overloaded indexing only");

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
                   callback->arguments().size() == 2U &&
                   callback->ops().size() == 3U &&
                   callee(callback->ops().back()) == "[]" &&
                   callback->ops().back().arguments().size() == 3U &&
                   compiler.verify(*transpose) && compiler.verify(*callback),
               "an index list supports generic rank and multi-index access");

  const auto product = compiler.materialize("tensor_use.product");
  const auto product_ops = product ? product->ops() : std::vector<joggle::Op>{};
  const auto matmul = product_ops.size() == 1U
                          ? compiler.materialize(product_ops.front())
                          : std::optional<joggle::Fn>{};
  if (!product || !matmul) {
    compiler.diag().print(std::cerr);
  }
  const auto matmul_ops = matmul ? matmul->ops() : std::vector<joggle::Op>{};
  const auto matmul_callback =
      matmul_ops.size() == 1U && matmul_ops.front().arguments().size() == 1U
          ? matmul_ops.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  const auto matmul_callback_ops =
      matmul_callback ? matmul_callback->ops() : std::vector<joggle::Op>{};
  const auto dot = matmul_callback_ops.size() == 3U
                       ? compiler.materialize(matmul_callback_ops.back())
                       : std::optional<joggle::Fn>{};
  if (!dot) {
    compiler.diag().print(std::cerr);
  }
  ok &= expect(product && product_ops.size() == 1U &&
                   callee(product_ops.front()) == "matmul" && matmul &&
                   matmul_ops.size() == 1U &&
                   callee(matmul_ops.front()) == "compute" &&
                   matmul_callback && matmul_callback_ops.size() == 3U &&
                   callee(matmul_callback_ops.back()) == "dot" && dot &&
                   dot->blks().size() == 5U && compiler.verify(*matmul) &&
                   compiler.verify(*matmul_callback) && compiler.verify(*dot),
               "MatMul uses an ordinary loop-carried scalar accumulation");

  const auto activate = compiler.materialize("tensor_use.activate");
  const auto activate_ops =
      activate ? activate->ops() : std::vector<joggle::Op>{};
  const auto relu = activate_ops.size() == 1U
                        ? compiler.materialize(activate_ops.front())
                        : std::optional<joggle::Fn>{};
  const auto relu_ops = relu ? relu->ops() : std::vector<joggle::Op>{};
  const auto relu_callback =
      relu_ops.size() == 1U && relu_ops.front().arguments().size() == 1U
          ? relu_ops.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(activate && relu && relu_callback &&
                   callee(relu_ops.front()) == "compute" &&
                   compiler.verify(*activate) && compiler.verify(*relu) &&
                   compiler.verify(*relu_callback),
               "rank-polymorphic Relu is ordinary indexed construction");

  const std::string formatted = tensor ? joggle::format(*tensor) : std::string{};
  joggle::Diag roundtrip_diagnostics;
  const auto roundtrip =
      tensor ? joggle::parse_mod(formatted, roundtrip_diagnostics,
                                 "tensor-roundtrip.joggle")
             : std::optional<joggle::Mod>{};
  ok &= expect(formatted.find("coord") == std::string::npos &&
                   formatted.find("fn compute") != std::string::npos &&
                   formatted.find("fn map") == std::string::npos &&
                   formatted.find("fn reduce") == std::string::npos &&
                   formatted.find("list<index>") != std::string::npos &&
                   roundtrip && roundtrip_diagnostics.ok() &&
                   joggle::format(*roundtrip) == formatted,
               "the minimal Tensor surface round-trips without stage syntax");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
