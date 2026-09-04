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

constexpr std::string_view fire_source = R"(
joggle 1;

module squeezenet_slice@1.0.0 {
  import tensor@1 as t;

  fn conv_relu(
    input: t.tensor<f32, [1, 64, 55, 55]>,
    weight: t.tensor<f32, [16, 64, 1, 1]>,
    bias: t.tensor<f32, [16]>,
    strides: list<int>,
    pads: list<int>,
    dilations: list<int>,
    group: int
  ) -> t.tensor<f32, [1, 16, 55, 55]> {
    convolved: t.tensor<f32, [1, 16, 55, 55]> = t.conv(
      input, weight, bias, strides, pads, dilations, group
    );
    return t.relu(convolved);
  }

  fn conv_relu_wrong(
    input: t.tensor<f32, [1, 64, 55, 55]>,
    weight: t.tensor<f32, [16, 64, 1, 1]>,
    bias: t.tensor<f32, [16]>
  ) -> t.tensor<f32, [1, 16, 55, 55]> {
    convolved: t.tensor<f32, [1, 16, 55, 55]> = t.conv(
      input, weight, bias, [1, 1], [0, 0, 0, 0], [1, 1], 2
    );
    return t.relu(convolved);
  }

  fn fire(
    input: t.tensor<f32, [1, 64, 55, 55]>,
    squeeze_weight: t.tensor<f32, [16, 64, 1, 1]>,
    squeeze_bias: t.tensor<f32, [16]>,
    expand1_weight: t.tensor<f32, [64, 16, 1, 1]>,
    expand1_bias: t.tensor<f32, [64]>,
    expand3_weight: t.tensor<f32, [64, 16, 3, 3]>,
    expand3_bias: t.tensor<f32, [64]>
  ) -> t.tensor<f32, [1, 128, 55, 55]> {
    squeezed_conv: t.tensor<f32, [1, 16, 55, 55]> = t.conv(
      input,
      squeeze_weight,
      squeeze_bias,
      [1, 1],
      [0, 0, 0, 0],
      [1, 1],
      1
    );
    squeezed: t.tensor<f32, [1, 16, 55, 55]> = t.relu(squeezed_conv);

    expanded1_conv: t.tensor<f32, [1, 64, 55, 55]> = t.conv(
      squeezed,
      expand1_weight,
      expand1_bias,
      [1, 1],
      [0, 0, 0, 0],
      [1, 1],
      1
    );
    expanded1: t.tensor<f32, [1, 64, 55, 55]> = t.relu(expanded1_conv);

    expanded3_conv: t.tensor<f32, [1, 64, 55, 55]> = t.conv(
      squeezed,
      expand3_weight,
      expand3_bias,
      [1, 1],
      [1, 1, 1, 1],
      [1, 1],
      1
    );
    expanded3: t.tensor<f32, [1, 64, 55, 55]> = t.relu(expanded3_conv);
    return t.concat(expanded1, expanded3, 1);
  }

  fn squeeze_pattern(
    input: t.tensor<f32, [1, 64, 55, 55]>,
    weight: t.tensor<f32, [16, 64, 1, 1]>,
    bias: t.tensor<f32, [16]>
  ) -> t.tensor<f32, [1, 16, 55, 55]> {
    convolved: t.tensor<f32, [1, 16, 55, 55]> = t.conv(
      input,
      weight,
      bias,
      [1, 1],
      [0, 0, 0, 0],
      [1, 1],
      1
    );
    return t.relu(convolved);
  }

  fn squeeze_fused(
    input: t.tensor<f32, [1, 64, 55, 55]>,
    weight: t.tensor<f32, [16, 64, 1, 1]>,
    bias: t.tensor<f32, [16]>
  ) -> t.tensor<f32, [1, 16, 55, 55]> {
    return conv_relu(
      input,
      weight,
      bias,
      [1, 1],
      [0, 0, 0, 0],
      [1, 1],
      1
    );
  }

  fn squeeze_wrong(
    input: t.tensor<f32, [1, 64, 55, 55]>,
    weight: t.tensor<f32, [16, 64, 1, 1]>,
    bias: t.tensor<f32, [16]>
  ) -> t.tensor<f32, [1, 16, 55, 55]> {
    return conv_relu_wrong(input, weight, bias);
  }
}
)";

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

void register_tensor_verifier(joggle::Compiler& compiler,
                              const joggle::Module::TypeDecl& tensor) {
  compiler.verify(
      tensor, [](const joggle::Type& type, joggle::Diagnostics& diagnostics) {
        const auto element = type.get<joggle::Type>("element");
        const auto shape = type.get<std::vector<std::int64_t>>("shape");
        if (!element || !shape) {
          diagnostics.report("tensor requires an element type and shape");
          return false;
        }
        if (std::any_of(shape->begin(), shape->end(),
                        [](std::int64_t dimension) {
                          return dimension < 0;
                        })) {
          diagnostics.report("static tensor dimensions must be non-negative");
          return false;
        }
        return true;
      });
}

std::optional<joggle::Module::TypeDecl>
load_tensor_slice(joggle::Compiler& compiler) {
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.add(fire_source, "squeezenet-slice.joggle");
  if (!compiler.link()) {
    return std::nullopt;
  }
  const auto tensor_module = compiler.module("tensor");
  const auto tensor = tensor_module ? tensor_module->type("tensor")
                                    : std::nullopt;
  if (tensor) {
    register_tensor_verifier(compiler, *tensor);
  }
  return tensor;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  const auto tensor = load_tensor_slice(compiler);
  const auto slice = compiler.module("squeezenet_slice");
  const auto tensor_module = compiler.module("tensor");
  const auto f32 = compiler.make("f32");
  const auto fire_decl = slice ? slice->function("fire") : std::nullopt;
  const auto pattern_decl =
      slice ? slice->function("squeeze_pattern") : std::nullopt;
  const auto fused_decl =
      slice ? slice->function("squeeze_fused") : std::nullopt;
  const auto wrong_decl =
      slice ? slice->function("squeeze_wrong") : std::nullopt;
  const auto conv_relu_decl =
      slice ? slice->function("conv_relu") : std::nullopt;
  const auto relu_decl = tensor_module ? tensor_module->function("relu")
                                       : std::nullopt;
  const auto concat_decl = tensor_module ? tensor_module->function("concat")
                                         : std::nullopt;
  if (!tensor || !slice || !tensor_module || !f32 || !fire_decl ||
      !pattern_decl || !fused_decl || !wrong_decl || !conv_relu_decl ||
      !relu_decl ||
      !concat_decl) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto scalar = compiler.make(*tensor, *f32,
                                    std::vector<std::int64_t>{});
  auto fire = compiler.materialize(*fire_decl);
  auto pattern = compiler.materialize(*pattern_decl);
  auto fused = compiler.materialize(*fused_decl);
  auto wrong = compiler.materialize(*wrong_decl);
  if (!scalar || !fire || !pattern || !fused || !wrong || !compiler.ok()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto fire_ops = fire->ops();
  const auto result = fire->entry().terminator().returned();
  ok &= expect(scalar->get<std::vector<std::int64_t>>("shape") ==
                   std::vector<std::int64_t>{},
               "rank-zero static tensors remain valid tensor types");
  ok &= expect(fire_ops.size() == 7U &&
                   fire_ops[0].callee().symbol().module_name() == "tensor" &&
                   fire_ops[0].callee().symbol().local_name() == "conv" &&
                   fire_ops[1].callee() == *relu_decl &&
                   fire_ops.back().callee() == *concat_decl,
               "a Fire block materializes as three Conv/Relu pairs and one "
               "Concat without a second graph container");
  ok &= expect(
      fire_ops[0].property<std::vector<std::int64_t>>("strides") ==
              std::vector<std::int64_t>({1, 1}) &&
          fire_ops[0].property<std::vector<std::int64_t>>("pads") ==
              std::vector<std::int64_t>({0, 0, 0, 0}) &&
          fire_ops[0].property<std::int64_t>("group") == 1 &&
          fire_ops[4].property<std::vector<std::int64_t>>("pads") ==
              std::vector<std::int64_t>({1, 1, 1, 1}) &&
          fire_ops.back().property<std::int64_t>("axis") == 1,
      "compiler-domain tensor arguments become typed immutable properties");
  ok &= expect(result.size() == 1U &&
                   result.front().type().get<std::vector<std::int64_t>>(
                       "shape") ==
                       std::vector<std::int64_t>({1, 128, 55, 55}) &&
                   compiler.verify(*fire),
               "the Fire block keeps its explicit semantic output type");

  joggle::Function rejected_fire = *fire;
  const auto rejected_revision = rejected_fire.revision();
  joggle::Diagnostics rejected_replacement_diagnostics;
  const auto rejected_replacement = joggle::replace(
      compiler, rejected_fire, *pattern, *wrong,
      rejected_replacement_diagnostics);
  ok &= expect(!rejected_replacement &&
                   !rejected_replacement_diagnostics.ok() &&
                   rejected_fire.revision() == rejected_revision,
               "a type-correct tensor kernel with different reference "
               "semantics is rejected without mutation");

  joggle::Diagnostics replacement_diagnostics;
  const auto replacements = joggle::replace(
      compiler, *fire, *pattern, *fused, replacement_diagnostics);
  const auto fused_ops = fire->ops();
  ok &= expect(replacements && *replacements == 1U &&
                   replacement_diagnostics.ok() && fused_ops.size() == 6U &&
                   fused_ops.front().callee() == *conv_relu_decl &&
                   compiler.verify(*fire),
               "an extension-local kernel fuses one shape-specific Conv/Relu "
               "pair without changing the tensor module");

  const std::string canonical = joggle::format(*fire, "fire_optimized");
  joggle::Compiler roundtrip;
  roundtrip.add(*tensor_module);
  roundtrip.add(*slice);
  roundtrip.add("joggle 1;\nmodule artifact@1.0.0 {\n"
                "  import tensor@1;\n"
                "  import squeezenet_slice@1;\n" +
                    canonical + "}\n",
                "artifact.joggle");
  const bool roundtrip_linked = roundtrip.link();
  const auto roundtrip_tensor_module = roundtrip.module("tensor");
  const auto roundtrip_tensor = roundtrip_tensor_module
                                    ? roundtrip_tensor_module->type("tensor")
                                    : std::nullopt;
  if (roundtrip_tensor) {
    register_tensor_verifier(roundtrip, *roundtrip_tensor);
  }
  const auto replayed =
      roundtrip_linked && roundtrip_tensor
          ? roundtrip.materialize("artifact.fire_optimized")
          : std::nullopt;
  if (!replayed) {
    roundtrip.diagnostics().print(std::cerr);
  }
  ok &= expect(replayed &&
                   joggle::format(*replayed, "fire_optimized") == canonical,
               "a transformed tensor Function has canonical round-trippable "
               "source");

  joggle::Compiler invalid;
  invalid.load(JOGGLE_TENSOR_MODULE);
  const bool invalid_linked = invalid.link();
  const auto invalid_module = invalid.module("tensor");
  const auto invalid_tensor =
      invalid_module ? invalid_module->type("tensor") : std::nullopt;
  const auto invalid_f32 = invalid.make("f32");
  if (invalid_tensor) {
    register_tensor_verifier(invalid, *invalid_tensor);
  }
  const auto rejected = invalid_linked && invalid_tensor && invalid_f32
                            ? invalid.make(*invalid_tensor, *invalid_f32,
                                           std::vector<std::int64_t>{1, -1, 8})
                            : std::nullopt;
  ok &= expect(!rejected && !invalid.ok(),
               "the normal type verifier rejects a dynamic sentinel without "
               "a tensor-specific core type case");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
