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

constexpr std::string_view source = R"(
joggle 1;

module qconv_fixture@1.0.0 {
  import qconv@1;
  import quant@1 as q;
  import tensor@1 as t;

  fn model(
    input: t.tensor<u8, [1, 3, 8, 8]>,
    input_scale: t.tensor<f32, []>,
    input_zero: t.tensor<u8, []>,
    weight: t.tensor<i8, [4, 3, 3, 3]>,
    weight_scale: t.tensor<f32, [4]>,
    weight_zero: t.tensor<i8, [4]>,
    bias: t.tensor<i32, [4]>,
    bias_scale: t.tensor<f32, [4]>,
    bias_zero: t.tensor<i32, [4]>,
    output_scale: t.tensor<f32, []>,
    output_zero: t.tensor<u8, []>
  ) -> t.tensor<u8, [1, 4, 6, 6]> {
    expressed_input: t.tensor<f32, [1, 3, 8, 8]> = q.dequantize(
      input, input_scale, input_zero, axis: 1
    );
    expressed_weight: t.tensor<f32, [4, 3, 3, 3]> = q.dequantize(
      weight, weight_scale, weight_zero, axis: 0
    );
    expressed_bias: t.tensor<f32, [4]> = q.dequantize(
      bias, bias_scale, bias_zero, axis: 0
    );
    convolved: t.tensor<f32, [1, 4, 6, 6]> = t.conv(
      expressed_input,
      expressed_weight,
      expressed_bias,
      [1, 1],
      [0, 0, 0, 0],
      [1, 1],
      1
    );
    return q.quantize(
      convolved, output_scale, output_zero, axis: 1
    );
  }

  fn optimize(input: function) -> function {
    return @qconv.run(input);
  }
}
)";

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_QUANT_MODULE);
  compiler.load(JOGGLE_QCONV_MODULE);
  compiler.add(source, "qconv-fixture.joggle");
  if (!compiler.link() ||
      !compiler.load_native("qconv", JOGGLE_QCONV_NATIVE)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto model = compiler.materialize("qconv_fixture.model");
  const auto optimized = model ? compiler.run<joggle::Function>(
                                     "qconv_fixture.optimize", *model)
                               : std::nullopt;
  if (!model || !optimized) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto ops = optimized->ops();
  joggle::Diagnostics equivalence;
  bool ok = true;
  ok &= expect(ops.size() == 1U &&
                   ops.front().callee().symbol().module_name() == "qconv" &&
                   ops.front().callee().symbol().local_name() == "conv",
               "the QDQ Conv expression becomes one ordinary qconv call");
  ok &= expect(joggle::equivalent(compiler, *model, *optimized,
                                  equivalence) &&
                   equivalence.ok(),
               "the qconv source body proves the replacement meaning");
  ok &= expect(compiler.verify(*optimized),
               "the transformed Function remains verified IR");
  if (!ok) {
    equivalence.print(std::cerr);
    compiler.diagnostics().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
