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

constexpr std::string_view source = R"(
joggle 1;

mod tensor_use@1.0.0 {
  import arith@1 as a;
  import tensor@4 as t;

  fn twice(
    input: t.tensor<f32, [4]>
  ) -> t.tensor<f32, [4]> {
    return t.map(input, (value: f32) => value + value);
  }

  fn dot(
    lhs: t.tensor<f32, [4]>,
    rhs: t.tensor<f32, [4]>,
    initial: f32
  ) -> f32 {
    products: t.tensor<f32, [4]> = t.map(
      [4],
      (position: t.coord<[4]>) => lhs[position] * rhs[position]
    );
    return t.reduce(
      products,
      initial,
      (sum: f32, value: f32) -> f32 => sum + value
    );
  }

  fn indexed(
    input: t.tensor<f32, [4]>,
    position: t.coord<[4]>
  ) -> f32 {
    return input[position];
  }

  fn axis(position: t.coord<[4]>) -> index {
    return position[0];
  }

  fn repeated(value: index) -> t.coord<[2]> {
    return t.coord([2], value, value);
  }

  fn zero() -> f32 {
    return a.zero();
  }

  fn matmul(
    lhs: t.tensor<f32, [2, 4]>,
    rhs: t.tensor<f32, [4, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    initial: f32 = a.zero();
    return t.map(
      [2, 3],
      (out: t.coord<[2, 3]>) -> f32 => t.reduce(
        t.map(
          [4],
          (k: t.coord<[4]>) -> f32 =>
            lhs[t.coord([2, 4], out[0], k[0])] *
            rhs[t.coord([4, 3], k[0], out[1])]
        ),
        initial,
        (sum: f32, value: f32) -> f32 => sum + value
      )
    );
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
                        [](std::int64_t extent) { return extent < 0; })) {
          diagnostics.report("static tensor extents must be non-negative");
          return false;
        }
        return true;
      });
}

std::string callee_name(const joggle::Op& op) {
  const auto declaration = op.callee().referenced_fn();
  return declaration ? std::string(declaration->name()) : std::string{};
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MOD);
  compiler.load(JOGGLE_TENSOR_MOD);
  compiler.add(source, "tensor-use.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto arith_mod = compiler.mod("arith");
  const auto tensor_mod = compiler.mod("tensor");
  const auto use_mod = compiler.mod("tensor_use");
  const auto tensor = tensor_mod ? tensor_mod->type("tensor") : std::nullopt;
  if (!arith_mod || !tensor_mod || !use_mod || !tensor) {
    return EXIT_FAILURE;
  }
  register_tensor_verifier(compiler, *tensor);

  const auto twice = compiler.materialize("tensor_use.twice");
  const auto dot = compiler.materialize("tensor_use.dot");
  if (!twice || !dot) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto members = tensor_mod->fns();
  const std::vector<std::string> expected{"coord", "[]", "map",
                                          "[]",    "reduce", "map"};
  std::vector<std::string> names;
  names.reserve(members.size());
  for (const auto& member : members) {
    names.emplace_back(member.name());
  }
  ok &= expect(names == expected,
               "tensor exposes coordinates, structural calculus, and map");

  const auto indexed = compiler.materialize("tensor_use.indexed");
  const auto axis = compiler.materialize("tensor_use.axis");
  const auto repeated = compiler.materialize("tensor_use.repeated");
  const auto zero = compiler.materialize("tensor_use.zero");
  const auto matmul = compiler.materialize("tensor_use.matmul");
  const auto indexed_ops = indexed ? indexed->ops() : std::vector<joggle::Op>{};
  const auto axis_ops = axis ? axis->ops() : std::vector<joggle::Op>{};
  const auto repeated_ops =
      repeated ? repeated->ops() : std::vector<joggle::Op>{};
  ok &= expect(
      indexed && axis && repeated && zero && matmul &&
          indexed_ops.size() == 1U &&
          callee_name(indexed_ops.front()) == "[]" &&
          indexed_ops.front().arguments().size() == 2U &&
          axis_ops.size() == 1U && callee_name(axis_ops.front()) == "[]" &&
          axis_ops.front().arguments().size() == 1U &&
          axis_ops.front().callee().binding<std::int64_t>("axis") ==
              std::optional<std::int64_t>{0} &&
          repeated_ops.size() == 1U &&
          callee_name(repeated_ops.front()) == "coord" &&
          repeated_ops.front().arguments().size() == 2U &&
          repeated_ops.front().callee().binding<std::vector<std::int64_t>>(
              "shape") == std::optional<std::vector<std::int64_t>>{{2}} &&
          joggle::format(*indexed, "indexed").find("arg0[arg1]") !=
              std::string::npos &&
          joggle::format(*axis, "axis").find("arg0[0]") != std::string::npos &&
          joggle::format(*repeated, "repeated")
                  .find("tensor.coord([2], arg0, arg0)") != std::string::npos &&
          zero->ops().size() == 1U &&
          callee_name(zero->ops().front()) == "zero",
      "coordinate construction and indexing are ordinary typed fn calls");

  const auto matmul_ops = matmul ? matmul->ops() : std::vector<joggle::Op>{};
  const auto output_body =
      matmul_ops.size() == 2U && matmul_ops.back().arguments().size() == 1U
          ? matmul_ops.back().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  const auto output_ops =
      output_body ? output_body->ops() : std::vector<joggle::Op>{};
  const auto product_body =
      output_ops.size() == 2U && output_ops.front().arguments().size() == 1U
          ? output_ops.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  const auto reduction_body =
      output_ops.size() == 2U && output_ops.back().arguments().size() == 3U
          ? output_ops.back().arguments()[2].inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(
      matmul && matmul_ops.size() == 2U &&
          callee_name(matmul_ops.front()) == "zero" &&
          callee_name(matmul_ops.back()) == "map" && output_body &&
          output_ops.size() == 2U &&
          callee_name(output_ops.front()) == "map" &&
          callee_name(output_ops.back()) == "reduce" && product_body &&
          product_body->ops().size() == 9U && reduction_body &&
          reduction_body->ops().size() == 1U &&
          callee_name(reduction_body->ops().front()) == "+" &&
          compiler.verify(*matmul) && compiler.verify(*output_body) &&
          compiler.verify(*product_body) && compiler.verify(*reduction_body),
      "a MatMul body exposes output construction, reduction, indexing, and "
          "scalar arithmetic as nested Fns");

  const auto twice_ops = twice->ops();
  const auto map = twice_ops.size() == 1U
                       ? compiler.materialize(twice_ops.front())
                       : std::optional<joggle::Fn>{};
  const auto map_ops = map ? map->ops() : std::vector<joggle::Op>{};
  const auto builder =
      map_ops.size() == 1U
          ? std::optional<joggle::Val>{map_ops.front().arguments().front()}
          : std::optional<joggle::Val>{};
  const auto builder_body =
      builder ? builder->inline_fn() : std::optional<joggle::Fn>{};
  ok &= expect(
      twice_ops.size() == 1U && callee_name(twice_ops.front()) == "map" &&
          twice_ops.front().arguments().size() == 2U &&
          twice_ops.front().arguments()[1].inline_fn() && map &&
          map_ops.size() == 1U && callee_name(map_ops.front()) == "map" &&
          builder && builder->captures().size() == 2U && builder_body &&
          builder_body->ops().size() == 2U &&
          callee_name(builder_body->ops().front()) == "[]" &&
          !builder_body->ops().back().callee().referenced_fn() &&
          compiler.verify(*twice) && compiler.verify(*map) &&
          compiler.verify(*builder_body),
      "tensor map expands to a domain map and subscript");

  const auto dot_ops = dot->ops();
  const auto product =
      dot_ops.size() == 2U && dot_ops.front().arguments().size() == 1U
          ? std::optional<joggle::Val>{dot_ops.front().arguments().front()}
          : std::optional<joggle::Val>{};
  const auto products =
      product ? product->inline_fn() : std::optional<joggle::Fn>{};
  const auto product_ops =
      products ? products->ops() : std::vector<joggle::Op>{};
  const auto update =
      dot_ops.size() == 2U && dot_ops.back().arguments().size() == 3U
          ? std::optional<joggle::Val>{dot_ops.back().arguments()[2]}
          : std::optional<joggle::Val>{};
  const auto update_body =
      update ? update->inline_fn() : std::optional<joggle::Fn>{};
  const auto update_ops =
      update_body ? update_body->ops() : std::vector<joggle::Op>{};
  ok &= expect(
      dot_ops.size() == 2U && callee_name(dot_ops.front()) == "map" &&
          callee_name(dot_ops.back()) == "reduce" && products &&
          product && product->captures().size() == 2U &&
          products->arguments().size() == 3U && product_ops.size() == 3U &&
          callee_name(product_ops[0]) == "[]" &&
          callee_name(product_ops[1]) == "[]" &&
          callee_name(product_ops[2]) == "*" && update &&
          update->captures().empty() && update_body &&
          update_body->arguments().size() == 2U && update_ops.size() == 1U &&
          callee_name(update_ops.front()) == "+" && compiler.verify(*dot) &&
          compiler.verify(*products) && compiler.verify(*update_body),
      "dot composes map, indexed reads, scalar overloads, and reduce");

  joggle::Fn expanded = *twice;
  joggle::Diag diagnostics;
  const auto changed = joggle::inline_calls(compiler, expanded, diagnostics);
  ok &= expect(changed == std::optional<std::size_t>{1U} && diagnostics.ok() &&
               expanded.ops().size() == 1U &&
                   callee_name(expanded.ops().front()) == "map" &&
                   compiler.verify(expanded),
               "generic inlining exposes map without recognizing its name");

  const std::string canonical = joggle::format(*dot, "dot_body");
  joggle::Compiler roundtrip;
  roundtrip.add(*arith_mod);
  roundtrip.add(*tensor_mod);
  roundtrip.add(*use_mod);
  roundtrip.add("joggle 1;\nmod artifact@1.0.0 {\n"
                "  import arith@1;\n"
                "  import tensor@4;\n"
                "  import tensor_use@1;\n" +
                    canonical + "}\n",
                "artifact.joggle");
  const bool linked = roundtrip.link();
  const auto replayed = linked ? roundtrip.materialize("artifact.dot_body")
                               : std::optional<joggle::Fn>{};
  if (!replayed) {
    roundtrip.diag().print(std::cerr);
  }
  ok &= expect(replayed && joggle::format(*replayed, "dot_body") == canonical,
               "the tensor calculus round-trips as ordinary Fn IR");

  joggle::Compiler invalid;
  invalid.load(JOGGLE_TENSOR_MOD);
  const bool invalid_linked = invalid.link();
  const auto invalid_mod = invalid.mod("tensor");
  const auto invalid_tensor =
      invalid_mod ? invalid_mod->type("tensor") : std::nullopt;
  const auto f32 = invalid.make("f32");
  if (invalid_tensor) {
    register_tensor_verifier(invalid, *invalid_tensor);
  }
  const auto rejected = invalid_linked && invalid_tensor && f32
                            ? invalid.make(*invalid_tensor, *f32,
                                           std::vector<std::int64_t>{1, -1})
                            : std::nullopt;
  ok &= expect(!rejected && !invalid.ok(),
               "a normal type verifier rejects an invalid static extent");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
