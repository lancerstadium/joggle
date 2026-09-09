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
  CHECK(env.load("script"));

  constexpr std::string_view legal_source =
      "module legal.network\n"
      "use nn\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  return relu(x)\n"
      "}\n";
  joggle::Mod legal;
  CHECK(joggle::parse(env, legal_source, legal, "legal-network.jog"));
  CHECK(legal.verify(env));
  const std::string before_keep = joggle::print(legal);
  CHECK(joggle::run(env, "script.keep_relu", legal));
  CHECK(joggle::print(legal) == before_keep);
  const std::vector<joggle::Attr> supported_args{
      joggle::Attr(joggle::Attr::List{joggle::Attr("nn.relu")})};
  joggle::Attr supported_frontier;
  CHECK(joggle::query(env, "opt.frontier", legal, supported_frontier,
                      supported_args));
  CHECK(supported_frontier.list() && supported_frontier.list()->empty());
  joggle::Attr open_frontier;
  CHECK(joggle::query(env, "script.network_frontier", legal,
                      open_frontier));
  CHECK(open_frontier.list() && open_frontier.list()->size() == 1);
  CHECK(open_frontier.list()->front().string() == "nn.relu");
  CHECK(joggle::run(env, "script.expose_network", legal));
  CHECK(legal.verify(env));
  for (joggle::Op op : legal.ops())
    CHECK(op.callee() != "relu" && op.callee() != "nn.relu");
  CHECK(joggle::query(env, "script.network_frontier", legal,
                      open_frontier));
  CHECK(open_frontier.list() && !open_frontier.list()->empty());
  joggle::Mod legal_roundtrip;
  CHECK(joggle::parse(env, joggle::print(legal), legal_roundtrip,
                      "legal-network-roundtrip.jog"));
  CHECK(legal_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(legal, legal_roundtrip));

  constexpr std::string_view annotated_legal_source =
      "module annotated.legal\n"
      "use nn\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  [schedule: {width: 4}]\n"
      "  let y = relu(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod annotated_legal;
  CHECK(joggle::parse(env, annotated_legal_source, annotated_legal,
                      "annotated-legal.jog"));
  CHECK(annotated_legal.verify(env));
  const std::string before_annotated = joggle::print(annotated_legal);
  CHECK(joggle::run(env, "script.expose_network", annotated_legal));
  CHECK(joggle::print(annotated_legal) == before_annotated);
  CHECK(joggle::query(env, "script.network_frontier", annotated_legal,
                      open_frontier));
  CHECK(open_frontier.list() && open_frontier.list()->size() == 1);
  CHECK(open_frontier.list()->front().string() == "nn.relu");

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

  constexpr std::string_view max_pool_source =
      "module max.pool\n"
      "use nn\n"
      "fn main(x: tensor<f32, [1, 1, 4, 4]>) "
      "-> tensor<f32, [1, 1, 2, 2]> {\n"
      "  return nn.max_pool2d(\n"
      "    x, [2, 2], [2, 2], [0, 0, 0, 0], [1, 1], [0, 1, 2, 3]\n"
      "  )\n"
      "}\n";
  joggle::Mod max_pool;
  CHECK(joggle::parse(env, max_pool_source, max_pool, "max-pool.jog"));
  CHECK(max_pool.verify(env));
  joggle::Op max_pool_call;
  for (joggle::Op op : max_pool.ops())
    if (op.callee() == "nn.max_pool2d")
      max_pool_call = op;
  const joggle::Fn max_pool_fn = env.resolve(max_pool, max_pool_call);
  CHECK(max_pool_fn && max_pool.expand(max_pool_call, max_pool_fn));
  CHECK(max_pool.verify(env));
  for (joggle::Op op : max_pool.ops())
    CHECK(op.callee() != "nn.max_pool2d");
  joggle::Mod max_pool_roundtrip;
  CHECK(joggle::parse(env, joggle::print(max_pool), max_pool_roundtrip,
                      "max-pool-roundtrip.jog"));
  CHECK(max_pool_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(max_pool, max_pool_roundtrip));

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

  constexpr std::string_view binary_bridge_source =
      "module binary.bridge\n"
      "use onnx\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3, 1]>,\n"
      "  right: tensor<f32, [2, 1, 4]>\n"
      ") -> tensor<f32, [2, 3, 4]> {\n"
      "  let product = onnx.Mul(left, right)\n"
      "  let out: tensor<f32, [2, 3, 4]> = onnx.Sub(product, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod binary_bridge;
  CHECK(joggle::parse(env, binary_bridge_source, binary_bridge,
                      "binary-bridge.jog"));
  CHECK(binary_bridge.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", binary_bridge));
  CHECK(binary_bridge.verify(env));
  joggle::Op source_mul;
  for (joggle::Op op : binary_bridge.ops())
    if (op.callee() == "onnx.Mul")
      source_mul = op;
  CHECK(source_mul && source_mul.outs().size() == 1);
  CHECK(source_mul.outs()[0].type() ==
        joggle::Ty("tensor<f32, [2, 3, 4]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", binary_bridge));
  CHECK(binary_bridge.verify(env));
  std::size_t binaries = 0;
  for (joggle::Op op : binary_bridge.ops()) {
    if (op.callee() != "nn.mul" && op.callee() != "nn.sub")
      continue;
    CHECK(op.args().size() == 3 && op.meta().empty());
    const joggle::Fn fn = env.resolve(binary_bridge, op);
    CHECK(fn && binary_bridge.expand(op, fn));
    ++binaries;
  }
  CHECK(binaries == 2 && binary_bridge.verify(env));
  for (joggle::Op op : binary_bridge.ops())
    CHECK(op.callee() != "nn.mul" && op.callee() != "nn.sub");

  constexpr std::string_view matrix_bridge_source =
      "module matrix.bridge\n"
      "use onnx\n"
      "fn main(\n"
      "  x: tensor<f32, [2, 3, 4]>, w: tensor<f32, [4, 5]>\n"
      ") -> tensor<f32, [5, 6]> {\n"
      "  [onnx: {axis: -1}]\n"
      "  let flat = onnx.Flatten(x)\n"
      "  let product = onnx.MatMul(flat, w)\n"
      "  [onnx: {perm: [1, 0]}]\n"
      "  let out = onnx.Transpose(product)\n"
      "  return out\n"
      "}\n";
  joggle::Mod matrix_bridge;
  CHECK(joggle::parse(env, matrix_bridge_source, matrix_bridge,
                      "matrix-bridge.jog"));
  CHECK(matrix_bridge.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", matrix_bridge));
  CHECK(matrix_bridge.verify(env));
  CHECK(matrix_bridge.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [5, 6]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", matrix_bridge));
  CHECK(matrix_bridge.verify(env));
  std::size_t matrix_calls = 0;
  for (joggle::Op op : matrix_bridge.ops()) {
    if (op.callee() != "tensor.reshape" && op.callee() != "tensor.matmul" &&
        op.callee() != "tensor.permute")
      continue;
    const joggle::Fn fn = env.resolve(matrix_bridge, op);
    CHECK(fn && matrix_bridge.expand(op, fn));
    ++matrix_calls;
  }
  CHECK(matrix_calls == 3 && matrix_bridge.verify(env));

  constexpr std::string_view pool_bridge_source =
      "module pool.bridge\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [1, 1, 4, 4]>) "
      "-> tensor<f32, [1, 1, 2, 2]> {\n"
      "  [onnx: {kernel_shape: [2, 2], strides: [2, 2]}]\n"
      "  let y: tensor<f32, [1, 1, 2, 2]> = onnx.MaxPool(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod pool_bridge;
  CHECK(joggle::parse(env, pool_bridge_source, pool_bridge,
                      "pool-bridge.jog"));
  CHECK(pool_bridge.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", pool_bridge));
  CHECK(pool_bridge.verify(env));
  joggle::Op semantic_pool;
  for (joggle::Op op : pool_bridge.ops())
    if (op.callee() == "nn.max_pool2d")
      semantic_pool = op;
  CHECK(semantic_pool && semantic_pool.args().size() == 6);
  CHECK(semantic_pool.meta().empty());
  const joggle::Fn semantic_pool_fn = env.resolve(pool_bridge, semantic_pool);
  CHECK(semantic_pool_fn &&
        pool_bridge.expand(semantic_pool, semantic_pool_fn));
  CHECK(pool_bridge.verify(env));

  constexpr std::string_view symbolic_source =
      "module symbolic.bridge\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  left: tensor<f32, [N, 3]>, right: tensor<f32, [1, 3]>\n"
      ") -> tensor<f32, [N, 3]> {\n"
      "  let unknown = onnx.Unknown(left)\n"
      "  let pending: tensor<f32, [N, 3]> = onnx.Add(unknown, right)\n"
      "  let out: tensor<f32, [N, 3]> = onnx.Mul(left, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod symbolic;
  CHECK(joggle::parse(env, symbolic_source, symbolic, "symbolic-bridge.jog"));
  CHECK(symbolic.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", symbolic));
  CHECK(symbolic.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", symbolic));
  CHECK(symbolic.verify(env));
  std::size_t retained_binaries = 0;
  joggle::Op symbolic_mul;
  for (joggle::Op op : symbolic.ops()) {
    if (op.callee() == "onnx.Add" || op.callee() == "onnx.Mul")
      ++retained_binaries;
    if (op.callee() == "nn.mul")
      symbolic_mul = op;
  }
  CHECK(retained_binaries == 1);
  CHECK(symbolic_mul);
  const joggle::Fn symbolic_mul_fn = env.resolve(symbolic, symbolic_mul);
  CHECK(symbolic_mul_fn && symbolic.expand(symbolic_mul, symbolic_mul_fn));
  CHECK(symbolic.verify(env));

  constexpr std::string_view symbolic_matrix_source =
      "module symbolic.matrix\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 2, 3]>, w: tensor<f32, [6, 4]>\n"
      ") -> tensor<f32, [4, N]> {\n"
      "  [onnx: {axis: 1}]\n"
      "  let flat = onnx.Flatten(x)\n"
      "  let product = onnx.MatMul(flat, w)\n"
      "  [onnx: {perm: [1, 0]}]\n"
      "  let out = onnx.Transpose(product)\n"
      "  return out\n"
      "}\n";
  joggle::Mod symbolic_matrix;
  CHECK(joggle::parse(env, symbolic_matrix_source, symbolic_matrix,
                      "symbolic-matrix.jog"));
  CHECK(symbolic_matrix.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", symbolic_matrix));
  CHECK(symbolic_matrix.verify(env));
  CHECK(symbolic_matrix.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [4, N]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", symbolic_matrix));
  CHECK(symbolic_matrix.verify(env));
  std::size_t symbolic_matrix_calls = 0;
  for (joggle::Op op : symbolic_matrix.ops()) {
    if (op.callee() != "tensor.reshape" &&
        op.callee() != "tensor.matmul" &&
        op.callee() != "tensor.permute")
      continue;
    const joggle::Fn fn = env.resolve(symbolic_matrix, op);
    CHECK(fn && symbolic_matrix.expand(op, fn));
    ++symbolic_matrix_calls;
  }
  CHECK(symbolic_matrix_calls == 3 && symbolic_matrix.verify(env));

  constexpr std::string_view unresolved_shape_source =
      "module unresolved.shape\n"
      "use onnx\n"
      "fn main<N: int, M: int>(\n"
      "  x: tensor<f32, [N, M, 3]>\n"
      ") -> tensor<f32, [_, 3]> {\n"
      "  [onnx: {axis: 2}]\n"
      "  let out: tensor<f32, [_, 3]> = onnx.Flatten(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod unresolved_shape;
  CHECK(joggle::parse(env, unresolved_shape_source, unresolved_shape,
                      "unresolved-shape.jog"));
  CHECK(unresolved_shape.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", unresolved_shape));
  CHECK(joggle::run(env, "onnx.nn.convert", unresolved_shape));
  CHECK(unresolved_shape.verify(env));
  bool retained_flatten = false;
  for (joggle::Op op : unresolved_shape.ops())
    retained_flatten = retained_flatten || op.callee() == "onnx.Flatten";
  CHECK(retained_flatten);

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
