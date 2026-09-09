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
  CHECK(semantic_add && semantic_add.args().size() == 2);
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
    CHECK(op.args().size() == 2 && op.meta().empty());
    const joggle::Fn fn = env.resolve(binary_bridge, op);
    CHECK(fn && binary_bridge.expand(op, fn));
    ++binaries;
  }
  CHECK(binaries == 2 && binary_bridge.verify(env));
  for (joggle::Op op : binary_bridge.ops())
    CHECK(op.callee() != "nn.mul" && op.callee() != "nn.sub");

  constexpr std::string_view scalar_bridge_source =
      "module scalar.bridge\n"
      "use onnx\n"
      "fn main(values: tensor<f32, [768]>) -> tensor<f32, [768]> {\n"
      "  let scalar: tensor<f32, []> = source()\n"
      "  let out: tensor<f32, [768]> = onnx.Mul(scalar, values)\n"
      "  return out\n"
      "}\n";
  joggle::Mod scalar_bridge;
  CHECK(joggle::parse(env, scalar_bridge_source, scalar_bridge,
                      "scalar-bridge.jog"));
  CHECK(scalar_bridge.verify(env));
  joggle::Op scalar_mul;
  for (joggle::Op op : scalar_bridge.ops())
    if (op.callee() == "onnx.Mul")
      scalar_mul = op;
  CHECK(scalar_mul && scalar_bridge.use("nn"));
  const std::string before_bad_retarget = joggle::print(scalar_bridge);
  CHECK(!scalar_bridge.retarget(env, scalar_mul, "nn.relu",
                                scalar_mul.args()));
  CHECK(joggle::print(scalar_bridge) == before_bad_retarget);
  CHECK(joggle::run(env, "onnx.nn.convert", scalar_bridge));
  CHECK(scalar_bridge.verify(env));
  CHECK(scalar_mul.callee() == "nn.mul");

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

  constexpr std::string_view qdq_source =
      "module qdq.shape\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 3]>, scale: tensor<f32, [3]>,\n"
      "  zero: tensor<u8, [3]>\n"
      ") -> tensor<f32, [N, 3]> {\n"
      "  [onnx: {axis: 1}]\n"
      "  let q = onnx.QuantizeLinear(x, scale, zero)\n"
      "  [onnx: {axis: 1}]\n"
      "  let out: tensor<f32, [N, 3]> = "
      "onnx.DequantizeLinear(q, scale, zero)\n"
      "  return out\n"
      "}\n";
  joggle::Mod qdq;
  CHECK(joggle::parse(env, qdq_source, qdq, "qdq-shape.jog"));
  CHECK(qdq.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", qdq));
  CHECK(qdq.verify(env));
  joggle::Op quant;
  joggle::Op dequant;
  for (joggle::Op op : qdq.ops()) {
    if (op.callee() == "onnx.QuantizeLinear")
      quant = op;
    if (op.callee() == "onnx.DequantizeLinear")
      dequant = op;
  }
  CHECK(quant && quant.outs()[0].type() ==
                     joggle::Ty("tensor<u8, [N, 3]>"));
  CHECK(dequant && dequant.outs()[0].type() ==
                       joggle::Ty("tensor<f32, [N, 3]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", qdq));
  CHECK(qdq.verify(env));
  CHECK(quant.callee() == "quant.quantize");
  CHECK(dequant.callee() == "quant.dequantize");
  const joggle::Fn quant_fn = env.resolve(qdq, quant);
  const joggle::Fn dequant_fn = env.resolve(qdq, dequant);
  CHECK(quant_fn && dequant_fn);
  CHECK(qdq.expand(quant, quant_fn));
  CHECK(qdq.expand(dequant, dequant_fn));
  CHECK(qdq.verify(env));
  joggle::Mod qdq_roundtrip;
  CHECK(joggle::parse(env, joggle::print(qdq), qdq_roundtrip,
                      "qdq-roundtrip.jog"));
  if (!qdq_roundtrip.verify(env))
    return env.print_diags(stderr);
  CHECK(joggle::structurally_equal(qdq, qdq_roundtrip));

  constexpr std::string_view symbolic_conv_source =
      "module symbolic.conv\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 3, 8, 8]>,\n"
      "  weight: tensor<f32, [4, 3, 3, 3]>,\n"
      "  bias: tensor<f32, [4]>\n"
      ") -> tensor<f32, [N, 4, 1, 1]> {\n"
      "  [onnx: {auto_pad: \"NOTSET\", pads: [1, 1, 1, 1]}]\n"
      "  let convolved = onnx.Conv(x, weight, bias)\n"
      "  let out = onnx.GlobalAveragePool(convolved)\n"
      "  return out\n"
      "}\n";
  joggle::Mod symbolic_conv;
  CHECK(joggle::parse(env, symbolic_conv_source, symbolic_conv,
                      "symbolic-conv.jog"));
  CHECK(symbolic_conv.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", symbolic_conv));
  CHECK(symbolic_conv.verify(env));
  joggle::Op source_conv;
  for (joggle::Op op : symbolic_conv.ops())
    if (op.callee() == "onnx.Conv")
      source_conv = op;
  CHECK(source_conv && source_conv.outs()[0].type() ==
                           joggle::Ty("tensor<f32, [N, 4, 8, 8]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", symbolic_conv));
  CHECK(symbolic_conv.verify(env));
  joggle::Op semantic_conv;
  joggle::Op semantic_global_pool;
  for (joggle::Op op : symbolic_conv.ops()) {
    if (op.callee() == "nn.conv2d")
      semantic_conv = op;
    if (op.callee() == "nn.global_avg_pool2d")
      semantic_global_pool = op;
  }
  CHECK(semantic_conv && semantic_conv.args().size() == 11);
  CHECK(semantic_global_pool);
  const joggle::Fn symbolic_conv_fn = env.resolve(symbolic_conv, semantic_conv);
  CHECK(symbolic_conv_fn && symbolic_conv.expand(semantic_conv,
                                                 symbolic_conv_fn));
  CHECK(symbolic_conv.verify(env));
  const joggle::Fn symbolic_pool_fn =
      env.resolve(symbolic_conv, semantic_global_pool);
  CHECK(symbolic_pool_fn &&
        symbolic_conv.expand(semantic_global_pool, symbolic_pool_fn));
  CHECK(symbolic_conv.verify(env));

  constexpr std::string_view invalid_conv_source =
      "module invalid.conv\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 3, 8, 8]>,\n"
      "  weight: tensor<f32, [4, 3, 3, 3]>,\n"
      "  bias: tensor<f32, [5]>\n"
      ") -> tensor<f32, [N, 4, 8, 8]> {\n"
      "  let out: tensor<f32, [N, 4, 8, 8]> = "
      "onnx.Conv(x, weight, bias)\n"
      "  return out\n"
      "}\n";
  joggle::Mod invalid_conv;
  CHECK(joggle::parse(env, invalid_conv_source, invalid_conv,
                      "invalid-conv.jog"));
  CHECK(invalid_conv.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", invalid_conv));
  CHECK(joggle::run(env, "onnx.nn.convert", invalid_conv));
  CHECK(invalid_conv.verify(env));
  bool retained_conv = false;
  for (joggle::Op op : invalid_conv.ops())
    retained_conv = retained_conv || op.callee() == "onnx.Conv";
  CHECK(retained_conv);

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

  constexpr std::string_view shape_program_source =
      "module shape.program\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [N, 3, 4]>) "
      "-> tensor<f32, [N, 12]> {\n"
      "  let inferred_target: tensor<i64, [3]> = onnx.tensor(\n"
      "    7, [3], "
      "hex\"ffffffffffffffff03000000000000000400000000000000\"\n"
      "  )\n"
      "  let expanded = onnx.Reshape(x, inferred_target)\n"
      "  let shape = onnx.Shape(expanded)\n"
      "  let index: tensor<i64, [1]> = "
      "onnx.tensor(7, [1], hex\"0000000000000000\")\n"
      "  [onnx: {axis: 0}]\n"
      "  let batch = onnx.Gather(shape, index)\n"
      "  [onnx: {axes: [0]}]\n"
      "  let vector = onnx.Unsqueeze(batch)\n"
      "  let tail_data: tensor<i64, [2]> = onnx.tensor(\n"
      "    7, [2], hex\"0c000000000000006300000000000000\"\n"
      "  )\n"
      "  let start: tensor<i64, [1]> = "
      "onnx.tensor(7, [1], hex\"0000000000000000\")\n"
      "  let end: tensor<i64, [1]> = "
      "onnx.tensor(7, [1], hex\"0100000000000000\")\n"
      "  let axis: tensor<i64, [1]> = "
      "onnx.tensor(7, [1], hex\"0000000000000000\")\n"
      "  let tail = onnx.Slice(tail_data, start, end, axis)\n"
      "  [onnx: {axis: 0}]\n"
      "  let target = onnx.Concat(vector, tail)\n"
      "  [onnx: {to: 7}]\n"
      "  let cast = onnx.Cast(target)\n"
      "  let out = onnx.Reshape(expanded, cast)\n"
      "  return out\n"
      "}\n";
  joggle::Mod shape_program;
  CHECK(joggle::parse(env, shape_program_source, shape_program,
                      "shape-program.jog"));
  CHECK(shape_program.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", shape_program));
  CHECK(shape_program.verify(env));
  CHECK(shape_program.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [N, 12]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", shape_program));
  CHECK(shape_program.verify(env));
  joggle::Op semantic_reshape;
  for (joggle::Op op : shape_program.ops())
    if (op.callee() == "tensor.reshape")
      semantic_reshape = op;
  CHECK(semantic_reshape && semantic_reshape.args().size() == 1);
  const joggle::Fn shape_reshape_fn =
      env.resolve(shape_program, semantic_reshape);
  CHECK(shape_reshape_fn &&
        shape_program.expand(semantic_reshape, shape_reshape_fn));
  CHECK(shape_program.verify(env));

  constexpr std::string_view transformer_source =
      "module transformer.shape\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  query: tensor<f32, [N, 12, 256, 64]>,\n"
      "  key: tensor<f32, [N, 12, 256, 64]>\n"
      ") -> tensor<f32, [N, 12, 256, 256]> {\n"
      "  let shape = onnx.Shape(query)\n"
      "  [onnx: {value: {type: 1}}]\n"
      "  let filled: tensor<f32, [_, 12, 256, 64]> = "
      "onnx.ConstantOfShape(shape)\n"
      "  [onnx: {alpha: 0.125, transA: 0, transB: 1}]\n"
      "  let scores = com_microsoft.FusedMatMul(query, key)\n"
      "  return scores\n"
      "}\n";
  joggle::Mod transformer;
  CHECK(joggle::parse(env, transformer_source, transformer,
                      "transformer-shape.jog"));
  CHECK(transformer.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", transformer));
  CHECK(transformer.verify(env));
  joggle::Op filled;
  joggle::Op scores;
  for (joggle::Op op : transformer.ops()) {
    if (op.callee() == "onnx.ConstantOfShape")
      filled = op;
    if (op.callee() == "com_microsoft.FusedMatMul")
      scores = op;
  }
  CHECK(filled && filled.outs()[0].type() ==
                      joggle::Ty("tensor<f32, [N, 12, 256, 64]>"));
  CHECK(scores && scores.outs()[0].type() ==
                      joggle::Ty("tensor<f32, [N, 12, 256, 256]>"));

  constexpr std::string_view split_source =
      "module split.shape\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [2, N, 256]>) "
      "-> (tensor<f32, [1, N, 256]>, tensor<f32, [1, N, 256]>) {\n"
      "  [onnx: {axis: 0}]\n"
      "  let left, right = onnx.Split(x)\n"
      "  return left, right\n"
      "}\n";
  joggle::Mod split;
  CHECK(joggle::parse(env, split_source, split, "split-shape.jog"));
  CHECK(split.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", split));
  CHECK(split.verify(env));
  joggle::Op split_call;
  for (joggle::Op op : split.ops())
    if (op.callee() == "onnx.Split")
      split_call = op;
  CHECK(split_call && split_call.outs().size() == 2);
  for (joggle::Val value : split_call.outs())
    CHECK(value.type() == joggle::Ty("tensor<f32, [1, N, 256]>"));

  constexpr std::string_view invalid_reshape_source =
      "module invalid.reshape\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [2, 3]>) -> tensor<f32, [5, 5]> {\n"
      "  let shape: tensor<i64, [2]> = onnx.tensor(\n"
      "    7, [2], hex\"05000000000000000500000000000000\"\n"
      "  )\n"
      "  let out: tensor<f32, [5, 5]> = onnx.Reshape(x, shape)\n"
      "  return out\n"
      "}\n";
  joggle::Mod invalid_reshape;
  CHECK(joggle::parse(env, invalid_reshape_source, invalid_reshape,
                      "invalid-reshape.jog"));
  CHECK(invalid_reshape.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", invalid_reshape));
  CHECK(joggle::run(env, "onnx.nn.convert", invalid_reshape));
  bool retained_reshape = false;
  for (joggle::Op op : invalid_reshape.ops())
    retained_reshape = retained_reshape || op.callee() == "onnx.Reshape";
  CHECK(retained_reshape && invalid_reshape.verify(env));

  constexpr std::string_view batched_matmul_source =
      "module batched.matmul\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  left: tensor<f32, [N, 3, 4]>,\n"
      "  right: tensor<f32, [2, 1, 4, 5]>\n"
      ") -> tensor<f32, [2, N, 3, 5]> {\n"
      "  let out = onnx.MatMul(left, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod batched_matmul;
  CHECK(joggle::parse(env, batched_matmul_source, batched_matmul,
                      "batched-matmul.jog"));
  CHECK(batched_matmul.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", batched_matmul));
  CHECK(batched_matmul.verify(env));
  joggle::Op source_batched_matmul;
  for (joggle::Op op : batched_matmul.ops())
    if (op.callee() == "onnx.MatMul")
      source_batched_matmul = op;
  CHECK(source_batched_matmul && source_batched_matmul.outs()[0].type() ==
                                       joggle::Ty(
                                           "tensor<f32, [2, N, 3, 5]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", batched_matmul));
  CHECK(batched_matmul.verify(env));
  joggle::Op semantic_batched_matmul;
  for (joggle::Op op : batched_matmul.ops())
    if (op.callee() == "tensor.matmul")
      semantic_batched_matmul = op;
  CHECK(semantic_batched_matmul);
  const joggle::Fn batched_matmul_fn =
      env.resolve(batched_matmul, semantic_batched_matmul);
  CHECK(batched_matmul_fn &&
        batched_matmul.expand(semantic_batched_matmul, batched_matmul_fn));
  CHECK(batched_matmul.verify(env));

  constexpr std::string_view mixed_matmul_source =
      "module mixed.matmul\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  left: tensor<f32, [3, 4]>,\n"
      "  right: tensor<f32, [N, 4, 5]>\n"
      ") -> tensor<f32, [N, 3, 5]> {\n"
      "  return onnx.MatMul(left, right)\n"
      "}\n";
  joggle::Mod mixed_matmul;
  CHECK(joggle::parse(env, mixed_matmul_source, mixed_matmul,
                      "mixed-matmul.jog"));
  CHECK(mixed_matmul.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", mixed_matmul));
  CHECK(joggle::run(env, "onnx.nn.convert", mixed_matmul));
  CHECK(mixed_matmul.verify(env));
  joggle::Op mixed_call;
  for (joggle::Op op : mixed_matmul.ops())
    if (op.callee() == "tensor.matmul")
      mixed_call = op;
  CHECK(mixed_call && mixed_call.outs()[0].type() ==
                          joggle::Ty("tensor<f32, [N, 3, 5]>"));

  constexpr std::string_view softmax_source =
      "module axis.softmax\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [N, 2, 3]>) "
      "-> tensor<f32, [N, 2, 3]> {\n"
      "  [onnx: {axis: 1}]\n"
      "  let out = onnx.Softmax(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod softmax;
  CHECK(joggle::parse(env, softmax_source, softmax, "axis-softmax.jog"));
  CHECK(softmax.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", softmax));
  CHECK(joggle::run(env, "onnx.nn.convert", softmax));
  CHECK(softmax.verify(env));
  joggle::Op semantic_softmax;
  for (joggle::Op op : softmax.ops())
    if (op.callee() == "nn.softmax")
      semantic_softmax = op;
  CHECK(semantic_softmax && semantic_softmax.args().size() == 3);
  CHECK(semantic_softmax.args()[1].constant().integer() == 1);
  const joggle::Fn softmax_fn = env.resolve(softmax, semantic_softmax);
  CHECK(softmax_fn && softmax.expand(semantic_softmax, softmax_fn));
  CHECK(softmax.verify(env));

  constexpr std::string_view mean_source =
      "module axis.mean\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [N, 2, 3, 4]>) "
      "-> tensor<f32, [N, 3]> {\n"
      "  [onnx: {axes: [1, -1], keepdims: 0}]\n"
      "  let out = onnx.ReduceMean(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod mean;
  CHECK(joggle::parse(env, mean_source, mean, "axis-mean.jog"));
  CHECK(mean.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", mean));
  CHECK(mean.verify(env));
  CHECK(mean.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [N, 3]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", mean));
  CHECK(mean.verify(env));
  joggle::Op semantic_mean;
  for (joggle::Op op : mean.ops())
    if (op.callee() == "tensor.mean")
      semantic_mean = op;
  CHECK(semantic_mean && semantic_mean.args().size() == 2);
  const joggle::Fn mean_fn = env.resolve(mean, semantic_mean);
  CHECK(mean_fn && mean.expand(semantic_mean, mean_fn));
  CHECK(mean.verify(env));

  constexpr std::string_view invalid_mean_source =
      "module invalid.mean\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [2, 3]>) -> tensor<f32, [2, 1]> {\n"
      "  [onnx: {axes: [1, 1]}]\n"
      "  let out: tensor<f32, [2, 1]> = onnx.ReduceMean(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod invalid_mean;
  CHECK(joggle::parse(env, invalid_mean_source, invalid_mean,
                      "invalid-mean.jog"));
  CHECK(invalid_mean.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", invalid_mean));
  CHECK(joggle::run(env, "onnx.nn.convert", invalid_mean));
  bool retained_mean = false;
  for (joggle::Op op : invalid_mean.ops())
    retained_mean = retained_mean || op.callee() == "onnx.ReduceMean";
  CHECK(retained_mean && invalid_mean.verify(env));

  constexpr std::string_view norm_source =
      "module norm.chain\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 2, 4]>, epsilon: tensor<f32, [1]>,\n"
      "  exponent: tensor<f32, [1]>\n"
      ") -> tensor<f32, [N, 2, 4]> {\n"
      "  [onnx: {axes: [-1], keepdims: 1}]\n"
      "  let mean = onnx.ReduceMean(x)\n"
      "  let centered = onnx.Sub(x, mean)\n"
      "  let squared = onnx.Mul(centered, centered)\n"
      "  [onnx: {axes: [-1], keepdims: 1}]\n"
      "  let variance = onnx.ReduceMean(squared)\n"
      "  let shifted = onnx.Add(variance, epsilon)\n"
      "  let scale = onnx.Sqrt(shifted)\n"
      "  let normalized = onnx.Div(centered, scale)\n"
      "  let powered = onnx.Pow(normalized, exponent)\n"
      "  let out = onnx.Tanh(powered)\n"
      "  return out\n"
      "}\n";
  joggle::Mod norm;
  CHECK(joggle::parse(env, norm_source, norm, "norm-chain.jog"));
  CHECK(norm.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", norm));
  CHECK(norm.verify(env));
  CHECK(norm.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [N, 2, 4]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", norm));
  CHECK(norm.verify(env));
  const std::string norm_once = joggle::print(norm);
  CHECK(joggle::run(env, "onnx.nn.convert", norm));
  CHECK(joggle::print(norm) == norm_once);
  std::vector<joggle::Op> norm_calls;
  for (joggle::Op op : norm.ops()) {
    const std::string_view callee = op.callee();
    if (callee == "tensor.mean" || callee == "nn.sub" ||
        callee == "nn.mul" || callee == "nn.add" ||
        callee == "nn.sqrt" || callee == "nn.div" ||
        callee == "nn.pow" || callee == "nn.tanh")
      norm_calls.push_back(op);
  }
  CHECK(norm_calls.size() == 9);
  for (joggle::Op op : norm_calls) {
    const joggle::Fn fn = env.resolve(norm, op);
    CHECK(fn && norm.expand(op, fn));
  }
  CHECK(norm.verify(env));

  constexpr std::string_view implicit_softmax_source =
      "module implicit.softmax\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [2, 3]>) -> tensor<f32, [2, 3]> {\n"
      "  let out: tensor<f32, [2, 3]> = onnx.Softmax(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod implicit_softmax;
  CHECK(joggle::parse(env, implicit_softmax_source, implicit_softmax,
                      "implicit-softmax.jog"));
  CHECK(implicit_softmax.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", implicit_softmax));
  bool retained_softmax = false;
  for (joggle::Op op : implicit_softmax.ops())
    retained_softmax = retained_softmax || op.callee() == "onnx.Softmax";
  CHECK(retained_softmax && implicit_softmax.verify(env));

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
