#include <algorithm>
#include <cstdint>
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
  import nn@3 as n;
  import tensor@8 as t;

  pub fn transpose(
    input: t.tensor<f32, [2, 3]>
  ) -> t.tensor<f32, [3, 2]> {
    return t.tensor([3, 2], (row, column) => input[column, row]);
  }

  pub fn product(
    lhs: t.tensor<f32, [2, 4]>,
    rhs: t.tensor<f32, [4, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    return n.matmul(lhs, rhs);
  }

  pub fn activate(
    input: t.tensor<f32, [2, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    return n.relu(input);
  }

  pub fn product_relu(
    lhs: t.tensor<f32, [2, 4]>,
    rhs: t.tensor<f32, [4, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    product: t.tensor<f32, [2, 3]> = n.matmul(lhs, rhs);
    return n.relu(product);
  }

  pub fn convolution(
    input: t.tensor<f32, [1, 2, 4, 4]>,
    weight: t.tensor<f32, [3, 2, 3, 3]>
  ) -> t.tensor<f32, [1, 3, 4, 4]> {
    return n.conv(input, weight, pads: [1, 1, 1, 1]);
  }

  pub fn grouped_convolution(
    input: t.tensor<f32, [1, 2, 5, 6]>,
    weight: t.tensor<f32, [4, 1, 3, 3]>,
    bias: t.tensor<f32, [4]>
  ) -> t.tensor<f32, [1, 4, 3, 3]> {
    return n.conv(
      input,
      weight,
      bias,
      strides: [2, 2],
      group: 2,
      auto_pad: "SAME_UPPER"
    );
  }

  pub fn prepare(input: fn) -> fn {
    fused = @t.fuse(input);
    return @t.loops(fused);
  }

}
)",
               "tensor-use.joggle");
  if (!compiler.link() ||
      !compiler.load_native("tensor", JOGGLE_TENSOR_NATIVE)) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto tensor = compiler.mod("tensor");
  const auto declarations =
      tensor ? tensor->fns() : std::vector<joggle::Mod::FnDecl>{};
  const std::vector<std::string> names = [&] {
    std::vector<std::string> result;
    for (const auto& fn : declarations) {
      result.emplace_back(fn.name());
    }
    return result;
  }();
  ok &= expect(
      names ==
          std::vector<std::string>({"tensor", "map", "reduce", "[]", "empty",
                                    "set", "constant", "fuse", "loops"}),
      "Tensor has one value type, a compact basis, and two explicit passes");

  const auto transpose = compiler.materialize("tensor_use.transpose");
  if (!transpose) {
    compiler.diag().print(std::cerr);
  }
  const auto transpose_ops =
      transpose ? transpose->ops() : std::vector<joggle::Op>{};
  const auto callback =
      transpose_ops.size() == 1U &&
              transpose_ops.front().arguments().size() == 1U
          ? transpose_ops.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(transpose && transpose_ops.size() == 1U &&
                   callee(transpose_ops.front()) == "tensor" && callback &&
                   callback->arguments().size() == 3U &&
                   callback->ops().size() == 1U &&
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
  ok &= expect(
      product && product_ops.size() == 1U &&
          callee(product_ops.front()) == "matmul" && matmul &&
          matmul_ops.size() == 1U && callee(matmul_ops.front()) == "tensor" &&
          matmul_callback && matmul_callback->blks().size() == 1U &&
          matmul_callback_ops.size() == 2U &&
          callee(matmul_callback_ops.back()) == "reduce" &&
          compiler.verify(*matmul) && compiler.verify(*matmul_callback),
      "MatMul has a pure indexed construction and reduction");

  const auto activate = compiler.materialize("tensor_use.activate");
  const auto activate_ops =
      activate ? activate->ops() : std::vector<joggle::Op>{};
  const auto relu = activate_ops.size() == 1U
                        ? compiler.materialize(activate_ops.front())
                        : std::optional<joggle::Fn>{};
  const auto relu_ops = relu ? relu->ops() : std::vector<joggle::Op>{};
  const auto relu_callback =
      relu_ops.size() == 1U && relu_ops.front().arguments().size() == 2U
          ? relu_ops.front().arguments().back().inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(activate && relu && relu_callback &&
                   callee(relu_ops.front()) == "map" &&
                   compiler.verify(*activate) && compiler.verify(*relu) &&
                   compiler.verify(*relu_callback),
               "rank-polymorphic Relu is ordinary indexed construction");

  const auto product_relu = compiler.materialize("tensor_use.product_relu");
  const auto fused =
      product_relu ? compiler.run<joggle::Fn>("tensor.fuse", *product_relu)
                   : std::optional<joggle::Fn>{};
  const auto fused_ops = fused ? fused->ops() : std::vector<joggle::Op>{};
  const auto fused_body =
      fused_ops.size() == 1U && fused_ops.front().arguments().size() == 1U
          ? fused_ops.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  const auto fused_body_ops =
      fused_body ? fused_body->ops() : std::vector<joggle::Op>{};
  ok &= expect(
      product_relu && fused && compiler.verify(*fused) && fused_body &&
          compiler.verify(*fused_body) && fused_ops.size() == 1U &&
          callee(fused_ops.front()) == "tensor" &&
          std::none_of(fused_body_ops.begin(), fused_body_ops.end(),
                       [](const auto& op) { return callee(op) == "map"; }) &&
          std::any_of(fused_body_ops.begin(), fused_body_ops.end(),
                      [](const auto& op) { return callee(op) == "reduce"; }) &&
          std::any_of(fused_body_ops.begin(), fused_body_ops.end(),
                      [](const auto& op) { return callee(op) == "max"; }),
      "MatMul followed by Relu composes into one tensor construction");

  const auto looped = fused ? compiler.run<joggle::Fn>("tensor.loops", *fused)
                            : std::optional<joggle::Fn>{};
  const auto looped_ops = looped ? looped->ops() : std::vector<joggle::Op>{};
  const auto count = [&](std::string_view owner, std::string_view name) {
    return std::count_if(
        looped_ops.begin(), looped_ops.end(), [&](const auto& op) {
          const auto fn = op.callee().referenced_fn();
          return fn && fn->symbol().mod_name() == owner && fn->name() == name;
        });
  };
  ok &= expect(
      looped && compiler.verify(*looped) && looped->blks().size() > 1U &&
          count("tensor", "empty") == 1U && count("tensor", "set") == 1U &&
          count("tensor", "[]") == 2U && count("arith", "*") == 1U &&
          count("arith", "max") == 1U && count("nn", "matmul") == 0U &&
          count("nn", "relu") == 0U && count("tensor", "tensor") == 0U &&
          count("tensor", "map") == 0U && count("tensor", "reduce") == 0U,
      "the fused operator pipeline preserves MatMul-Relu scalar work in CFG "
      "loops");

  const auto prepared =
      product_relu
          ? compiler.run<joggle::Fn>("tensor_use.prepare", *product_relu)
          : std::optional<joggle::Fn>{};
  ok &= expect(prepared && looped && compiler.verify(*prepared) &&
                   prepared->blks().size() == looped->blks().size() &&
                   prepared->ops().size() == looped->ops().size(),
               "the same pipeline composes through ordinary staged source");

  const auto convolution = compiler.materialize("tensor_use.convolution");
  const auto convolution_fused =
      convolution ? compiler.run<joggle::Fn>("tensor.fuse", *convolution)
                  : std::optional<joggle::Fn>{};
  const auto convolution_looped =
      convolution_fused
          ? compiler.run<joggle::Fn>("tensor.loops", *convolution_fused)
          : std::optional<joggle::Fn>{};
  const auto convolution_ops = convolution_looped ? convolution_looped->ops()
                                                  : std::vector<joggle::Op>{};
  const auto has_call = [&](std::string_view owner, std::string_view name) {
    return std::any_of(convolution_ops.begin(), convolution_ops.end(),
                       [&](const joggle::Op& op) {
                         const auto fn = op.callee().referenced_fn();
                         return fn && fn->symbol().mod_name() == owner &&
                                fn->name() == name;
                       });
  };
  ok &= expect(convolution && convolution_fused && convolution_looped &&
                   compiler.verify(*convolution_fused) &&
                   compiler.verify(*convolution_looped) &&
                   !has_call("nn", "conv") && !has_call("tensor", "tensor") &&
                   !has_call("tensor", "reduce") && has_call("tensor", "[]") &&
                   has_call("arith", "select"),
               "padded Conv lowers from its ordinary body without a Conv "
               "case in either tensor pass");

  const auto grouped =
      compiler.materialize("tensor_use.grouped_convolution");
  const auto grouped_fused =
      grouped ? compiler.run<joggle::Fn>("tensor.fuse", *grouped)
              : std::optional<joggle::Fn>{};
  const auto grouped_looped =
      grouped_fused
          ? compiler.run<joggle::Fn>("tensor.loops", *grouped_fused)
          : std::optional<joggle::Fn>{};
  const auto grouped_shape = grouped && grouped->result_types().size() == 1U
                                 ? grouped->result_types().front().get<
                                       std::vector<std::int64_t>>("shape")
                                 : std::nullopt;
  const auto grouped_ops =
      grouped_looped ? grouped_looped->ops() : std::vector<joggle::Op>{};
  const auto grouped_has = [&](std::string_view owner, std::string_view name) {
    return std::any_of(grouped_ops.begin(), grouped_ops.end(),
                       [&](const joggle::Op& op) {
                         const auto fn = op.callee().referenced_fn();
                         return fn && fn->symbol().mod_name() == owner &&
                                fn->name() == name;
                       });
  };
  ok &= expect(
      grouped && grouped_shape ==
                     std::optional<std::vector<std::int64_t>>{{1, 4, 3, 3}} &&
          grouped_fused && grouped_looped && compiler.verify(*grouped_fused) &&
          compiler.verify(*grouped_looped) && !grouped_has("nn", "conv") &&
          !grouped_has("tensor", "tensor") &&
          !grouped_has("tensor", "reduce") && grouped_has("tensor", "[]") &&
          grouped_has("arith", "//") && grouped_has("arith", "select"),
      "Conv derives SAME padding and lowers grouped bias semantics through "
      "the same generic functions");

  const std::string formatted =
      tensor ? joggle::format(*tensor) : std::string{};
  joggle::Diag roundtrip_diagnostics;
  const auto roundtrip =
      tensor ? joggle::parse_mod(formatted, roundtrip_diagnostics,
                                 "tensor-roundtrip.joggle")
             : std::optional<joggle::Mod>{};
  ok &= expect(formatted.find("list<index>") == std::string::npos &&
                   formatted.find("fn tensor") != std::string::npos &&
                   formatted.find("fn map") != std::string::npos &&
                   formatted.find("fn reduce") != std::string::npos &&
                   formatted.find("fn fuse") != std::string::npos &&
                   formatted.find("fn loops") != std::string::npos &&
                   formatted.find("fn constant") != std::string::npos &&
                   roundtrip && roundtrip_diagnostics.ok() &&
                   joggle::format(*roundtrip) == formatted,
               "the minimal Tensor surface round-trips without stage syntax");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
