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

mod squeezenet_slice@1.0.0 {
  import tensor@1 as t;

  fn mapped(
    input: t.tensor<f32, [4]>
  ) -> t.tensor<f32, [4]> {
    return t.map(
      input,
      (value: f32) => t.relu_value(value)
    );
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
}
)";

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

void register_tensor_verifier(joggle::Compiler& compiler,
                              const joggle::Mod::TypeDecl& tensor) {
  compiler.verify(
      tensor, [](const joggle::Type& type, joggle::Diag& diagnostics) {
        const auto element = type.get<joggle::Type>("element");
        const auto shape = type.get<std::vector<std::int64_t>>("shape");
        if (!element || !shape) {
          diagnostics.report("tensor requires an element type and shape");
          return false;
        }
        if (std::any_of(shape->begin(), shape->end(),
                        [](std::int64_t dimension) { return dimension < 0; })) {
          diagnostics.report("static tensor dimensions must be non-negative");
          return false;
        }
        return true;
      });
}

std::optional<joggle::Mod::TypeDecl>
load_tensor_slice(joggle::Compiler& compiler) {
  compiler.load(JOGGLE_TENSOR_MOD);
  compiler.add(fire_source, "squeezenet-slice.joggle");
  if (!compiler.link()) {
    return std::nullopt;
  }
  const auto tensor_mod = compiler.mod("tensor");
  const auto tensor = tensor_mod ? tensor_mod->type("tensor") : std::nullopt;
  if (tensor) {
    register_tensor_verifier(compiler, *tensor);
  }
  return tensor;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  const auto tensor = load_tensor_slice(compiler);
  const auto slice = compiler.mod("squeezenet_slice");
  const auto tensor_mod = compiler.mod("tensor");
  const auto f32 = compiler.make("f32");
  const auto fire_decl = slice ? slice->fn("fire") : std::nullopt;
  const auto mapped_decl = slice ? slice->fn("mapped") : std::nullopt;
  const auto relu_decl = tensor_mod ? tensor_mod->fn("relu") : std::nullopt;
  const auto concat_decl = tensor_mod ? tensor_mod->fn("concat") : std::nullopt;
  if (!tensor || !slice || !tensor_mod || !f32 || !fire_decl || !mapped_decl ||
      !relu_decl || !concat_decl) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto scalar = compiler.make(*tensor, *f32, std::vector<std::int64_t>{});
  auto fire = compiler.materialize(*fire_decl);
  auto mapped = compiler.materialize(*mapped_decl);
  if (!scalar || !fire || !mapped || !compiler.ok()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto fire_ops = fire->ops();
  const auto result = fire->entry().terminator().returned();
  const auto relu_body = fire_ops.size() > 1U
                             ? compiler.materialize(fire_ops[1])
                             : std::optional<joggle::Fn>{};
  const auto relu_ops =
      relu_body ? relu_body->ops() : std::vector<joggle::Op>{};
  const auto element_fn =
      relu_ops.size() == 1U && relu_ops.front().arguments().size() == 1U
          ? relu_ops.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  const auto element_ops =
      element_fn ? element_fn->ops() : std::vector<joggle::Op>{};
  const auto relu_value_body = element_ops.size() == 2U
                                   ? compiler.materialize(element_ops.back())
                                   : std::optional<joggle::Fn>{};
  const auto mapped_ops = mapped->ops();
  const auto map_body = mapped_ops.size() == 1U
                            ? compiler.materialize(mapped_ops.front())
                            : std::optional<joggle::Fn>{};
  const auto map_ops = map_body ? map_body->ops() : std::vector<joggle::Op>{};
  const auto map_element_fn =
      map_ops.size() == 1U && map_ops.front().arguments().size() == 1U
          ? map_ops.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(scalar->get<std::vector<std::int64_t>>("shape") ==
                   std::vector<std::int64_t>{},
               "rank-zero static tensors remain valid tensor types");
  ok &=
      expect(fire_ops.size() == 7U &&
                 fire_ops[0].callee().referenced_fn()->symbol().mod_name() ==
                     "tensor" &&
                 fire_ops[0].callee().referenced_fn()->symbol().local_name() ==
                     "conv" &&
                 fire_ops[1].callee().referenced_fn() == relu_decl &&
                 fire_ops.back().callee().referenced_fn() == concat_decl,
             "a Fire block materializes as three Conv/Relu pairs and one "
             "Concat without a second graph container");
  ok &= expect(
      fire_ops[0].callee().binding<std::vector<std::int64_t>>("strides") ==
              std::vector<std::int64_t>({1, 1}) &&
          fire_ops[0].callee().binding<std::vector<std::int64_t>>("pads") ==
              std::vector<std::int64_t>({0, 0, 0, 0}) &&
          fire_ops[0].callee().binding<std::int64_t>("group") == 1 &&
          fire_ops[4].callee().binding<std::vector<std::int64_t>>("pads") ==
              std::vector<std::int64_t>({1, 1, 1, 1}) &&
          fire_ops.back().callee().binding<std::int64_t>("axis") == 1,
      "compiler-domain tensor arguments specialize an immutable callee");
  ok &= expect(
      relu_body && relu_ops.size() == 1U && element_fn &&
          relu_ops.front().callee().referenced_fn() &&
          relu_ops.front().callee().referenced_fn()->name() == "generate" &&
          relu_ops.front().arguments().front().captures() ==
              std::vector<joggle::Val>{relu_body->arguments().front()} &&
          element_fn->arguments().size() == 2U && element_ops.size() == 2U &&
          element_ops.front().callee().referenced_fn()->name() == "at" &&
          element_ops.back().callee().referenced_fn()->name() == "relu_value" &&
          relu_value_body && relu_value_body->ops().size() == 2U &&
          relu_value_body->ops().front().callee().referenced_fn()->name() ==
              "zero" &&
          relu_value_body->ops().back().callee().referenced_fn()->name() ==
              "max" &&
          compiler.verify(*relu_body) && compiler.verify(*element_fn) &&
          compiler.verify(*relu_value_body),
      "Relu expands through ordinary source fns into generate, access, and "
      "scalar calls with an explicit captured tensor");
  ok &= expect(
      map_body && map_ops.size() == 1U && map_element_fn &&
          map_ops.front().callee().referenced_fn()->name() == "generate" &&
          map_ops.front().arguments().front().captures().size() == 2U &&
          map_ops.front().arguments().front().captures()[0] ==
              map_body->arguments()[1] &&
          map_ops.front().arguments().front().captures()[1] ==
              map_body->arguments()[0] &&
          map_element_fn->ops().size() == 2U &&
          !map_element_fn->ops().back().callee().referenced_fn() &&
          compiler.verify(*map_body) && compiler.verify(*map_element_fn),
      "Map is a bodyful higher-order fn whose element body calls the user "
      "callable without an operator registry or name-specific compiler case");

  joggle::Fn expanded_fire = *fire;
  joggle::Diag inline_diagnostics;
  const auto inlined =
      joggle::inline_calls(compiler, expanded_fire, inline_diagnostics);
  const auto expanded_ops = expanded_fire.ops();
  const bool only_basis_calls = std::all_of(
      expanded_ops.begin(), expanded_ops.end(), [](const joggle::Op& op) {
        const auto declaration = op.callee().referenced_fn();
        return declaration && declaration->name() != "relu";
      });
  const auto first_element =
      expanded_ops.size() > 1U && expanded_ops[1].arguments().size() == 1U
          ? std::optional<joggle::Val>{expanded_ops[1].arguments().front()}
          : std::nullopt;
  ok &= expect(
      inlined == std::optional<std::size_t>{3U} && inline_diagnostics.ok() &&
          expanded_ops.size() == 7U && only_basis_calls &&
          expanded_ops[1].callee().referenced_fn()->name() == "generate" &&
          first_element && first_element->inline_fn() &&
          first_element->captures().size() == 1U &&
          first_element->captures().front() == expanded_ops.front().value() &&
          compiler.verify(expanded_fire),
      "generic inlining replaces every bodyful Relu Call with its real "
      "generate body and remaps closure captures to producer values");
  ok &=
      expect(result.size() == 1U &&
                 result.front().type().get<std::vector<std::int64_t>>(
                     "shape") == std::vector<std::int64_t>({1, 128, 55, 55}) &&
                 compiler.verify(*fire),
             "the Fire block keeps its explicit semantic output type");

  const std::string canonical = joggle::format(*fire, "fire_body");
  joggle::Compiler roundtrip;
  roundtrip.add(*tensor_mod);
  roundtrip.add(*slice);
  roundtrip.add("joggle 1;\nmod artifact@1.0.0 {\n"
                "  import tensor@1;\n"
                "  import squeezenet_slice@1;\n" +
                    canonical + "}\n",
                "artifact.joggle");
  const bool roundtrip_linked = roundtrip.link();
  const auto roundtrip_tensor_mod = roundtrip.mod("tensor");
  const auto roundtrip_tensor = roundtrip_tensor_mod
                                    ? roundtrip_tensor_mod->type("tensor")
                                    : std::nullopt;
  if (roundtrip_tensor) {
    register_tensor_verifier(roundtrip, *roundtrip_tensor);
  }
  const auto replayed = roundtrip_linked && roundtrip_tensor
                            ? roundtrip.materialize("artifact.fire_body")
                            : std::nullopt;
  if (!replayed) {
    roundtrip.diag().print(std::cerr);
  }
  ok &= expect(replayed && joggle::format(*replayed, "fire_body") == canonical,
               "a tensor Fn has canonical round-trippable source");

  joggle::Compiler invalid;
  invalid.load(JOGGLE_TENSOR_MOD);
  const bool invalid_linked = invalid.link();
  const auto invalid_mod = invalid.mod("tensor");
  const auto invalid_tensor =
      invalid_mod ? invalid_mod->type("tensor") : std::nullopt;
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
