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

module fusion_fixture@1.0.0 {
  import fusion@1;
  import tensor@1 as t;

  fn model(
    input: t.tensor<f32, [1, 3, 8, 8]>,
    weight: t.tensor<f32, [4, 3, 3, 3]>,
    bias: t.tensor<f32, [4]>
  ) -> t.tensor<f32, [1, 4, 6, 6]> {
    convolved: t.tensor<f32, [1, 4, 6, 6]> = t.conv(
      input, weight, bias, [1, 1], [0, 0, 0, 0], [1, 1], 1
    );
    return t.relu(convolved);
  }

  fn optimize(input: function) -> function {
    return @fusion.run(input);
  }
}
)";

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_FUSION_MODULE);
  compiler.add(source, "fusion-fixture.joggle");
  if (!compiler.link() ||
      !compiler.load_native("fusion", JOGGLE_FUSION_NATIVE)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto model = compiler.materialize("fusion_fixture.model");
  const auto optimized = model ? compiler.run<joggle::Function>(
                                     "fusion_fixture.optimize", *model)
                               : std::nullopt;
  if (!optimized) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto ops = optimized->ops();
  bool ok = true;
  ok &= expect(ops.size() == 1U &&
                   ops.front().callee().symbol().module_name() == "fusion" &&
                   ops.front().callee().symbol().local_name() == "conv_relu",
               "a source pipeline invokes the installable fusion Module");
  ok &= expect(compiler.verify(*optimized),
               "the fused Function remains ordinary verified IR");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
