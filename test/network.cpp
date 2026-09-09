#include "joggle/joggle.h"

#include <cstdio>
#include <string_view>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (false)

int main(int argc, char** argv) {
  CHECK(argc == 3);
  joggle::Env env;
  env.path(argv[1]);
  env.path(argv[2]);
  CHECK(env.load("nn"));

  constexpr std::string_view dynamic_source =
      "module dynamic.network\n"
      "use tensor\n"
      "fn keep(x: tensor<f32, [_, 3]>) -> tensor<f32, [_, 3]> {\n"
      "  return x\n"
      "}\n";
  joggle::Mod dynamic;
  CHECK(joggle::parse(env, dynamic_source, dynamic, "dynamic.jog"));
  CHECK(dynamic.verify(env));
  joggle::Mod dynamic_roundtrip;
  CHECK(joggle::parse(env, joggle::print(dynamic), dynamic_roundtrip,
                      "dynamic-roundtrip.jog"));
  CHECK(dynamic_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(dynamic, dynamic_roundtrip));

  constexpr std::string_view broadcast_source =
      "module broadcast.network\n"
      "use nn\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3, 1]>,\n"
      "  right: tensor<f32, [2, 1, 4]>\n"
      ") -> tensor<f32, [2, 3, 4]> {\n"
      "  return nn.add(left, right, \"NONE\")\n"
      "}\n";
  joggle::Mod broadcast;
  CHECK(joggle::parse(env, broadcast_source, broadcast, "broadcast.jog"));
  CHECK(broadcast.verify(env));
  joggle::Op add;
  for (joggle::Op op : broadcast.ops())
    if (op.callee() == "nn.add")
      add = op;
  CHECK(add);
  const joggle::Fn add_fn = env.resolve(broadcast, add);
  CHECK(add_fn && broadcast.expand(add, add_fn));
  CHECK(broadcast.verify(env));
  std::vector<joggle::Op> copies;
  for (joggle::Op op : broadcast.ops())
    if (op.callee() == "tensor.broadcast")
      copies.push_back(op);
  CHECK(copies.size() == 2);
  for (joggle::Op op : copies) {
    const joggle::Fn callee = env.resolve(broadcast, op);
    CHECK(callee && broadcast.expand(op, callee));
  }
  CHECK(broadcast.verify(env));
  std::size_t loops = 0;
  for (joggle::Op op : broadcast.ops()) {
    CHECK(op.callee() != "nn.add" && op.callee() != "tensor.broadcast");
    loops += op.kind() == joggle::Op::Kind::loop ? 1 : 0;
  }
  CHECK(loops == 2);
  joggle::Mod broadcast_roundtrip;
  CHECK(joggle::parse(env, joggle::print(broadcast), broadcast_roundtrip,
                      "broadcast-roundtrip.jog"));
  CHECK(broadcast_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(broadcast, broadcast_roundtrip));

  constexpr std::string_view residual_source =
      "module residual.network\n"
      "use nn\n"
      "fn main(\n"
      "  left: tensor<f32, [2, 3]>, right: tensor<f32, [2, 3]>\n"
      ") -> tensor<f32, [2, 3]> {\n"
      "  return nn.add(left, right, \"NONE\")\n"
      "}\n";
  joggle::Mod residual;
  CHECK(joggle::parse(env, residual_source, residual, "residual.jog"));
  CHECK(residual.verify(env));
  joggle::Op residual_add;
  for (joggle::Op op : residual.ops())
    if (op.callee() == "nn.add")
      residual_add = op;
  const joggle::Fn residual_fn = env.resolve(residual, residual_add);
  CHECK(residual_fn && residual_fn.generics().size() == 2);
  CHECK(residual.expand(residual_add, residual_fn));
  CHECK(residual.verify(env));
  for (joggle::Op op : residual.ops())
    CHECK(op.callee() != "tensor.broadcast");

  CHECK(env.load("onnx.nn"));
  constexpr std::string_view bridge_source =
      "module broadcast.bridge\n"
      "use onnx\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3, 1]>,\n"
      "  right: tensor<f32, [2, 1, 4]>\n"
      ") -> tensor<f32, [2, 3, 4]> {\n"
      "  let sum = onnx.Add(left, right)\n"
      "  let out: tensor<f32, [2, 3, 4]> = onnx.Relu(sum)\n"
      "  return out\n"
      "}\n";
  joggle::Mod bridge;
  CHECK(joggle::parse(env, bridge_source, bridge, "broadcast-bridge.jog"));
  CHECK(bridge.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", bridge));
  CHECK(bridge.verify(env));
  joggle::Op source_add;
  for (joggle::Op op : bridge.ops())
    if (op.callee() == "onnx.Add")
      source_add = op;
  CHECK(source_add && source_add.outs().size() == 1);
  CHECK(source_add.outs()[0].type() ==
        joggle::Ty("tensor<f32, [2, 3, 4]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", bridge));
  CHECK(bridge.verify(env));
  joggle::Op semantic_add;
  for (joggle::Op op : bridge.ops())
    if (op.callee() == "nn.add")
      semantic_add = op;
  CHECK(semantic_add && semantic_add.args().size() == 3);
  const joggle::Fn semantic_fn = env.resolve(bridge, semantic_add);
  CHECK(semantic_fn && bridge.expand(semantic_add, semantic_fn));
  CHECK(bridge.verify(env));

  constexpr std::string_view invalid_source =
      "module invalid.broadcast\n"
      "use onnx\n"
      "fn main(\n"
      "  left: tensor<f32, [2, 3]>, right: tensor<f32, [4, 1]>\n"
      ") -> tensor<f32, [2, 3]> {\n"
      "  let out: tensor<f32, [2, 3]> = onnx.Add(left, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod invalid;
  CHECK(joggle::parse(env, invalid_source, invalid, "invalid-broadcast.jog"));
  CHECK(invalid.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", invalid));
  CHECK(invalid.verify(env));
  bool retained = false;
  for (joggle::Op op : invalid.ops()) {
    retained = retained || op.callee() == "onnx.Add";
    CHECK(op.callee() != "nn.add");
  }
  CHECK(retained);
  return 0;
}
