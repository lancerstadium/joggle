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
  import tensor@2 as t;

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
    return t.fold(
      initial,
      (sum: f32, position: t.coord<[4]>) =>
        sum + t.at(lhs, position) * t.at(rhs, position),
      shape: [4]
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
  const std::vector<std::string> expected{"build", "at", "fold", "map"};
  std::vector<std::string> names;
  names.reserve(members.size());
  for (const auto& member : members) {
    names.emplace_back(member.name());
  }
  ok &= expect(names == expected,
               "tensor exposes only its structural calculus and map");

  const auto twice_ops = twice->ops();
  const auto map = twice_ops.size() == 1U
                       ? compiler.materialize(twice_ops.front())
                       : std::optional<joggle::Fn>{};
  const auto map_ops = map ? map->ops() : std::vector<joggle::Op>{};
  const auto builder = map_ops.size() == 1U
                           ? std::optional<joggle::Val>{
                                 map_ops.front().arguments().front()}
                           : std::optional<joggle::Val>{};
  const auto builder_body = builder ? builder->inline_fn()
                                    : std::optional<joggle::Fn>{};
  ok &= expect(
      twice_ops.size() == 1U && callee_name(twice_ops.front()) == "map" &&
          twice_ops.front().arguments().size() == 2U &&
          twice_ops.front().arguments()[1].inline_fn() && map &&
          map_ops.size() == 1U && callee_name(map_ops.front()) == "build" &&
          builder && builder->captures().size() == 2U && builder_body &&
          builder_body->ops().size() == 2U &&
          callee_name(builder_body->ops().front()) == "at" &&
          !builder_body->ops().back().callee().referenced_fn() &&
          compiler.verify(*twice) && compiler.verify(*map) &&
          compiler.verify(*builder_body),
      "map is a real higher-order body over build and at");

  const auto dot_ops = dot->ops();
  const auto update = dot_ops.size() == 1U &&
                              dot_ops.front().arguments().size() == 2U
                          ? std::optional<joggle::Val>{
                                dot_ops.front().arguments()[1]}
                          : std::optional<joggle::Val>{};
  const auto update_body = update ? update->inline_fn()
                                  : std::optional<joggle::Fn>{};
  const auto update_ops = update_body ? update_body->ops()
                                      : std::vector<joggle::Op>{};
  ok &= expect(
      dot_ops.size() == 1U && callee_name(dot_ops.front()) == "fold" &&
          update && update->captures().size() == 2U && update_body &&
          update_body->arguments().size() == 4U && update_ops.size() == 4U &&
          callee_name(update_ops[0]) == "at" &&
          callee_name(update_ops[1]) == "at" &&
          callee_name(update_ops[2]) == "*" &&
          callee_name(update_ops[3]) == "+" && compiler.verify(*dot) &&
          compiler.verify(*update_body),
      "dot composes fold, indexed reads, scalar overloads, and captures");

  joggle::Fn expanded = *twice;
  joggle::Diag diagnostics;
  const auto changed = joggle::inline_calls(compiler, expanded, diagnostics);
  ok &= expect(changed == std::optional<std::size_t>{1U} && diagnostics.ok() &&
                   expanded.ops().size() == 1U &&
                   callee_name(expanded.ops().front()) == "build" &&
                   compiler.verify(expanded),
               "generic inlining exposes map without recognizing its name");

  const std::string canonical = joggle::format(*dot, "dot_body");
  joggle::Compiler roundtrip;
  roundtrip.add(*arith_mod);
  roundtrip.add(*tensor_mod);
  roundtrip.add(*use_mod);
  roundtrip.add("joggle 1;\nmod artifact@1.0.0 {\n"
                "  import arith@1;\n"
                "  import tensor@2;\n"
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
  const auto invalid_tensor = invalid_mod ? invalid_mod->type("tensor")
                                          : std::nullopt;
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
