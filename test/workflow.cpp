#include "joggle/joggle.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

bool fold_add_zero(joggle::Mod& mod, joggle::Op* removed = nullptr) {
  for (joggle::Fn fn : mod.fns()) {
    for (joggle::Blk blk : fn.blks()) {
      for (joggle::Op op : blk.ops()) {
        if (op.kind() != joggle::Op::Kind::call || op.callee() != "operator +")
          continue;
        const std::vector<joggle::Val> args = op.args();
        const std::vector<joggle::Val> outs = op.outs();
        if (args.size() != 2 || outs.size() != 1 || !args[1].is_const())
          continue;
        const auto value = args[1].constant().integer();
        if (!value || *value != 0)
          continue;
        if (removed)
          *removed = op;
        return mod.replace(outs[0], args[0]) && mod.erase(op);
      }
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 4);
  const joggle::Ty tensor_type("tensor< f32, [2,N] >");
  CHECK(tensor_type.text() == "tensor<f32, [2, N]>");
  CHECK(tensor_type.name() == "tensor");
  CHECK(tensor_type.args().size() == 2);
  CHECK(tensor_type.args()[1].name() == "[]");
  CHECK(tensor_type.args()[1].args().size() == 2);
  CHECK(!joggle::Ty("tensor<i32,>").valid());

  std::ifstream input(argv[1]);
  CHECK(input);
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  env.path(argv[2]);
  env.path(argv[3]);
  CHECK(env.load("tensor"));
  CHECK(env.loaded("base"));
  CHECK(env.loaded("tensor"));
  CHECK((env.modules() == std::vector<std::string>{"base", "tensor"}));

  std::ifstream base_input(std::string(argv[2]) + "/base/module.jog");
  CHECK(base_input);
  std::ostringstream base_source;
  base_source << base_input.rdbuf();
  joggle::Mod base;
  CHECK(joggle::parse(env, base_source.str(), base, "base.jog"));
  CHECK(base.verify(env));
  joggle::Mod base_roundtrip;
  CHECK(joggle::parse(env, joggle::print(base), base_roundtrip,
                      "base-roundtrip.jog"));
  CHECK(base_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(base, base_roundtrip));

  CHECK(env.load("nn"));
  joggle::Mod network;
  constexpr std::string_view network_source =
      "module network\n"
      "use nn\n"
      "fn stage(x: tensor<f32, [4]>, skip: tensor<f32, [4]>) "
      "-> tensor<f32, [4]> {\n"
      "  return nn.relu(x + skip)\n}\n";
  CHECK(joggle::parse(env, network_source, network, "network.jog"));
  CHECK(network.verify(env));
  joggle::Op tensor_add;
  for (joggle::Op op : network.find_fn("stage").ops())
    if (op.callee() == "operator +")
      tensor_add = op;
  CHECK(tensor_add && env.resolve(network, tensor_add).module() == "tensor");
  const joggle::Fn relu = env.find_fn("nn.relu");
  CHECK(relu && !relu.external());
  bool relu_loop = false;
  bool relu_branch = false;
  for (joggle::Op op : relu.ops()) {
    relu_loop = relu_loop || op.kind() == joggle::Op::Kind::loop;
    relu_branch = relu_branch || op.kind() == joggle::Op::Kind::branch;
  }
  CHECK(relu_loop && relu_branch);
  joggle::Mod network_roundtrip;
  CHECK(joggle::parse(env, joggle::print(network), network_roundtrip,
                      "network-roundtrip.jog"));
  CHECK(network_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(network, network_roundtrip));
  joggle::Mod network_cpp;
  CHECK(joggle::parse(env, network_source, network_cpp, "network-cpp.jog"));
  for (joggle::Op op : network_cpp.ops()) {
    if (op.kind() != joggle::Op::Kind::call)
      continue;
    if (op.callee() != "operator +" && op.callee() != "nn.relu")
      continue;
    const joggle::Fn callee = env.resolve(network_cpp, op);
    CHECK(callee && env.expand(network_cpp, op, callee));
  }
  CHECK(network_cpp.verify(env));
  CHECK(env.load("script"));
  constexpr std::string_view relation_source =
      "module relation\n"
      "fn main(x: i32) -> i32 {\n"
      "  let y: i32 = opaque(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod relation;
  CHECK(joggle::parse(env, relation_source, relation, "relation.jog"));
  CHECK(relation.verify(env));
  CHECK(joggle::run(env, "script.apply_rules", relation));
  CHECK(relation.verify(env));
  bool matched = false;
  for (joggle::Op op : relation.ops()) {
    if (op.callee() == "opaque") {
      const joggle::Attr* value = op.meta("matched");
      matched = value && value->boolean().value_or(false);
    }
  }
  CHECK(matched);
  const std::string relation_before = joggle::print(relation);
  const std::uint64_t relation_revision = relation.revision();
  CHECK(!joggle::run(env, "script.reject_bad_rule", relation));
  CHECK(joggle::print(relation) == relation_before);
  CHECK(relation.revision() == relation_revision);
  env.clear_diags();
  CHECK(env.load("tflite.nn"));
  constexpr std::string_view tflite_relation_source =
      "module tflite.relation\n"
      "use tflite\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3]>, right: tensor<f32, [2, 1]>\n"
      ") -> tensor<f32, [2, 3]> {\n"
      "  [tflite: {options: {fused_activation_function: \"NONE\"}}]\n"
      "  let out: tensor<f32, [2, 3]> = tflite.ADD(left, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod tflite_relation;
  CHECK(joggle::parse(env, tflite_relation_source, tflite_relation,
                      "tflite-relation.jog"));
  CHECK(tflite_relation.verify(env));
  CHECK(joggle::run(env, "tflite.nn.convert", tflite_relation));
  CHECK(tflite_relation.verify(env));
  std::size_t tflite_adds = 0;
  std::size_t nn_adds = 0;
  for (joggle::Op op : tflite_relation.ops()) {
    tflite_adds += op.callee() == "tflite.ADD" ? 1 : 0;
    nn_adds += op.callee() == "nn.add" ? 1 : 0;
  }
  CHECK(tflite_adds == 0 && nn_adds == 1);
  constexpr std::string_view custom_onnx_source =
      "module custom.onnx\n"
      "use onnx\n"
      "fn main(x: i32) -> _ {\n"
      "  [onnx: {}]\n"
      "  let y = onnx.Custom(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod custom_onnx;
  CHECK(joggle::parse(env, custom_onnx_source, custom_onnx,
                      "custom-onnx.jog"));
  CHECK(custom_onnx.verify(env));
  CHECK(joggle::run(env, "script.extend_onnx", custom_onnx));
  CHECK(custom_onnx.verify(env));
  std::size_t custom_calls = 0;
  std::size_t copies = 0;
  for (joggle::Op op : custom_onnx.ops()) {
    custom_calls += op.callee() == "onnx.Custom" ? 1 : 0;
    if (op.callee() == "base.copy") {
      copies += 1;
      CHECK(op.outs().size() == 1 &&
            op.outs()[0].type() == joggle::Ty("i32"));
    }
  }
  CHECK(custom_calls == 0 && copies == 1);
  CHECK(joggle::run(env, "script.tensor_type_probe", network_cpp));
  CHECK(joggle::run(env, "script.text_probe", network_cpp));
  CHECK(joggle::run(env, "script.byte_probe", network_cpp));
  CHECK(joggle::run(env, "script.def_probe", network_cpp));
  CHECK(joggle::run(env, "script.expand_network", network));
  CHECK(network.verify(env));
  bool expanded_loop = false;
  bool expanded_branch = false;
  for (joggle::Op op : network.find_fn("stage").ops()) {
    CHECK(op.callee() != "nn.relu");
    expanded_loop = expanded_loop || op.kind() == joggle::Op::Kind::loop;
    expanded_branch =
        expanded_branch || op.kind() == joggle::Op::Kind::branch;
  }
  CHECK(expanded_loop && expanded_branch);
  joggle::Mod expanded_roundtrip;
  CHECK(joggle::parse(env, joggle::print(network), expanded_roundtrip,
                      "expanded-roundtrip.jog"));
  CHECK(expanded_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(network, expanded_roundtrip));
  CHECK(joggle::structurally_equal(network, network_cpp));

  constexpr std::string_view logical_source =
      "module logical\n"
      "fn choose(a: bool, b: bool, c: bool) -> bool {\n"
      "  let out: bool = a && (b || c)\n"
      "  return out\n"
      "}\n";
  joggle::Mod logical;
  CHECK(joggle::parse(env, logical_source, logical, "logical.jog"));
  CHECK(logical.verify(env));
  std::size_t logical_branches = 0;
  for (joggle::Op op : logical.ops())
    logical_branches += op.kind() == joggle::Op::Kind::branch ? 1 : 0;
  CHECK(logical_branches == 2);
  const std::string logical_text = joggle::print(logical);
  CHECK(logical_text.find("a && (b || c)") != std::string::npos);
  joggle::Mod logical_roundtrip;
  CHECK(joggle::parse(env, logical_text, logical_roundtrip,
                      "logical-roundtrip.jog"));
  CHECK(logical_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(logical, logical_roundtrip));
  joggle::Mod invalid_logical;
  CHECK(joggle::parse(
      env, "module invalid.logical\n"
           "fn bad(a: bool) -> bool { return a && 1 }\n",
      invalid_logical, "invalid-logical.jog"));
  CHECK(!invalid_logical.verify(env));
  CHECK(!invalid_logical.diags().empty());
  CHECK(invalid_logical.diags().front().message.find("yield type") !=
        std::string::npos);

  joggle::Mod generic_matmul;
  constexpr std::string_view generic_matmul_source =
      "module generic.matmul\n"
      "use tensor\n"
      "fn main(a: tensor<f32, [2, 3]>, b: tensor<f32, [3, 4]>) "
      "-> tensor<f32, [2, 4]> {\n"
      "  return tensor.matmul(a, b)\n}\n";
  CHECK(joggle::parse(env, generic_matmul_source, generic_matmul,
                      "generic-matmul.jog"));
  joggle::Op matmul_call;
  for (joggle::Op op : generic_matmul.ops())
    if (op.callee() == "tensor.matmul")
      matmul_call = op;
  CHECK(matmul_call);
  const joggle::Fn matmul_fn = env.resolve(generic_matmul, matmul_call);
  CHECK(matmul_fn && env.expand(generic_matmul, matmul_call, matmul_fn));
  CHECK(generic_matmul.verify(env));
  std::size_t matmul_loops = 0;
  for (joggle::Op op : generic_matmul.ops()) {
    CHECK(op.callee() != "tensor.matmul");
    matmul_loops += op.kind() == joggle::Op::Kind::loop ? 1 : 0;
  }
  CHECK(matmul_loops == 2);
  joggle::Mod generic_matmul_roundtrip;
  CHECK(joggle::parse(env, joggle::print(generic_matmul),
                      generic_matmul_roundtrip,
                      "generic-matmul-roundtrip.jog"));
  CHECK(generic_matmul_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(generic_matmul,
                                   generic_matmul_roundtrip));

  joggle::Mod conv_network;
  constexpr std::string_view conv_network_source =
      "module conv.network\n"
      "use nn\n"
      "fn main(\n"
      "  x: tensor<f32, [1, 3, 5, 5]>,\n"
      "  weight: tensor<f32, [4, 3, 3, 3]>,\n"
      "  stride: list<int>, pad: list<int>, dilation: list<int>\n"
      ") -> tensor<f32, [1, 4, 3, 3]> {\n"
      "  let y: tensor<f32, [1, 4, 3, 3]> = nn.conv2d(\n"
      "    x, weight, stride, pad, dilation, 1\n"
      "  )\n"
      "  return y\n"
      "}\n";
  CHECK(joggle::parse(env, conv_network_source, conv_network,
                      "conv-network.jog"));
  CHECK(conv_network.verify(env));
  joggle::Op conv_call;
  for (joggle::Op op : conv_network.ops())
    if (op.callee() == "nn.conv2d")
      conv_call = op;
  CHECK(conv_call);
  const joggle::Fn conv_fn = env.resolve(conv_network, conv_call);
  CHECK(conv_fn && env.expand(conv_network, conv_call, conv_fn));
  CHECK(conv_network.verify(env));
  joggle::Op layout_conv;
  for (joggle::Op op : conv_network.ops())
    if (op.callee() == "conv2d" || op.callee() == "nn.conv2d")
      layout_conv = op;
  CHECK(layout_conv);
  const joggle::Fn layout_conv_fn = env.resolve(conv_network, layout_conv);
  CHECK(layout_conv_fn &&
        env.expand(conv_network, layout_conv, layout_conv_fn));
  CHECK(conv_network.verify(env));
  std::size_t conv_loops = 0;
  for (joggle::Op op : conv_network.ops()) {
    CHECK(op.callee() != "nn.conv2d");
    conv_loops += op.kind() == joggle::Op::Kind::loop ? 1 : 0;
  }
  CHECK(conv_loops == 2);
  joggle::Mod conv_roundtrip;
  CHECK(joggle::parse(env, joggle::print(conv_network), conv_roundtrip,
                      "conv-network-roundtrip.jog"));
  CHECK(conv_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(conv_network, conv_roundtrip));

  joggle::Mod pool_network;
  constexpr std::string_view pool_network_source =
      "module pool.network\n"
      "use nn\n"
      "fn main(x: tensor<f32, [1, 8, 4, 4]>) "
      "-> tensor<f32, [1, 8, 1, 1]> {\n"
      "  return nn.global_avg_pool2d(x)\n"
      "}\n";
  CHECK(joggle::parse(env, pool_network_source, pool_network,
                      "pool-network.jog"));
  CHECK(pool_network.verify(env));
  joggle::Op pool_call;
  for (joggle::Op op : pool_network.ops())
    if (op.callee() == "nn.global_avg_pool2d")
      pool_call = op;
  CHECK(pool_call);
  const joggle::Fn pool_fn = env.resolve(pool_network, pool_call);
  CHECK(pool_fn && env.expand(pool_network, pool_call, pool_fn));
  CHECK(pool_network.verify(env));
  for (joggle::Op op : pool_network.ops())
    CHECK(op.callee() != "nn.global_avg_pool2d");
  joggle::Mod pool_roundtrip;
  CHECK(joggle::parse(env, joggle::print(pool_network), pool_roundtrip,
                      "pool-network-roundtrip.jog"));
  CHECK(pool_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(pool_network, pool_roundtrip));

  joggle::Mod norm_network;
  constexpr std::string_view norm_network_source =
      "module norm.network\n"
      "use nn\n"
      "fn main(\n"
      "  x: tensor<f32, [1, 8, 4, 4]>,\n"
      "  scale: tensor<f32, [8]>, bias: tensor<f32, [8]>,\n"
      "  mean: tensor<f32, [8]>, variance: tensor<f32, [8]>, epsilon: f64\n"
      ") -> tensor<f32, [1, 8, 4, 4]> {\n"
      "  return nn.batch_norm(x, scale, bias, mean, variance, epsilon)\n"
      "}\n";
  CHECK(joggle::parse(env, norm_network_source, norm_network,
                      "norm-network.jog"));
  CHECK(norm_network.verify(env));
  joggle::Op norm_call;
  for (joggle::Op op : norm_network.ops())
    if (op.callee() == "nn.batch_norm")
      norm_call = op;
  CHECK(norm_call);
  const joggle::Fn norm_fn = env.resolve(norm_network, norm_call);
  CHECK(norm_fn && env.expand(norm_network, norm_call, norm_fn));
  CHECK(norm_network.verify(env));
  std::size_t sqrt_calls = 0;
  for (joggle::Op op : norm_network.ops()) {
    CHECK(op.callee() != "nn.batch_norm");
    sqrt_calls += op.callee() == "math.sqrt" ? 1 : 0;
  }
  CHECK(sqrt_calls == 1);
  joggle::Mod norm_roundtrip;
  CHECK(joggle::parse(env, joggle::print(norm_network), norm_roundtrip,
                      "norm-network-roundtrip.jog"));
  CHECK(norm_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(norm_network, norm_roundtrip));

  joggle::Mod reshape_network;
  constexpr std::string_view reshape_network_source =
      "module reshape.network\n"
      "use tensor\n"
      "fn main(x: tensor<f32, [1, 2, 3]>) -> tensor<f32, [1, 6]> {\n"
      "  let y: tensor<f32, [1, 6]> = tensor.reshape(x)\n"
      "  return y\n"
      "}\n";
  CHECK(joggle::parse(env, reshape_network_source, reshape_network,
                      "reshape-network.jog"));
  CHECK(reshape_network.verify(env));
  joggle::Op reshape_call;
  for (joggle::Op op : reshape_network.ops())
    if (op.callee() == "tensor.reshape")
      reshape_call = op;
  CHECK(reshape_call);
  const joggle::Fn reshape_fn = env.resolve(reshape_network, reshape_call);
  CHECK(reshape_fn && env.expand(reshape_network, reshape_call, reshape_fn));
  CHECK(reshape_network.verify(env));
  for (joggle::Op op : reshape_network.ops())
    CHECK(op.callee() != "tensor.reshape");
  joggle::Mod reshape_roundtrip;
  CHECK(joggle::parse(env, joggle::print(reshape_network), reshape_roundtrip,
                      "reshape-network-roundtrip.jog"));
  CHECK(reshape_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(reshape_network, reshape_roundtrip));

  joggle::Mod linear_network;
  constexpr std::string_view linear_network_source =
      "module linear.network\n"
      "use nn\n"
      "fn stage(\n"
      "  x: tensor<f32, [2, 3]>,\n"
      "  weight: tensor<f32, [3, 4]>,\n"
      "  bias: tensor<f32, [4]>\n"
      ") -> tensor<f32, [2, 4]> {\n"
      "  return nn.relu(nn.linear(x, weight, bias))\n}\n";
  CHECK(joggle::parse(env, linear_network_source, linear_network,
                      "linear-network.jog"));
  CHECK(linear_network.verify(env));
  for (joggle::Op op : linear_network.ops()) {
    if (op.callee() != "nn.linear" && op.callee() != "nn.relu")
      continue;
    const joggle::Fn callee = env.resolve(linear_network, op);
    CHECK(callee && env.expand(linear_network, op, callee));
  }
  CHECK(linear_network.verify(env));
  joggle::Op exposed_matmul;
  for (joggle::Op op : linear_network.ops()) {
    CHECK(op.callee() != "nn.linear" && op.callee() != "nn.relu");
    if (op.callee() == "tensor.matmul")
      exposed_matmul = op;
  }
  CHECK(exposed_matmul);
  const joggle::Fn exposed_matmul_fn =
      env.resolve(linear_network, exposed_matmul);
  CHECK(exposed_matmul_fn &&
        env.expand(linear_network, exposed_matmul, exposed_matmul_fn));
  CHECK(linear_network.verify(env));
  for (joggle::Op op : linear_network.ops())
    CHECK(op.callee() != "tensor.matmul");
  joggle::Mod linear_network_roundtrip;
  const std::string linear_network_text = joggle::print(linear_network);
  CHECK(joggle::parse(env, linear_network_text,
                      linear_network_roundtrip,
                      "linear-network-roundtrip.jog"));
  CHECK(linear_network_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(linear_network,
                                   linear_network_roundtrip));

  joggle::Mod local_expand;
  constexpr std::string_view local_expand_source =
      "module local.expand\n"
      "fn pair(x: i32) -> (i32, i32) {\n"
      "  return x, x + 1\n}\n"
      "fn main(x: i32) -> i32 {\n"
      "  let first, second = pair(x)\n"
      "  return first + second\n}\n";
  CHECK(joggle::parse(env, local_expand_source, local_expand,
                      "local-expand.jog"));
  CHECK(local_expand.verify(env));
  joggle::Op pair_call;
  for (joggle::Op op : local_expand.find_fn("main").ops())
    if (op.callee() == "pair")
      pair_call = op;
  const joggle::Fn pair_fn = env.resolve(local_expand, pair_call);
  CHECK(pair_call && pair_fn && env.expand(local_expand, pair_call, pair_fn));
  CHECK(local_expand.verify(env));
  CHECK(joggle::print(local_expand).find("pair(x)") == std::string::npos);
  joggle::Mod local_expand_roundtrip;
  CHECK(joggle::parse(env, joggle::print(local_expand),
                      local_expand_roundtrip, "local-expand-roundtrip.jog"));
  CHECK(local_expand_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(local_expand, local_expand_roundtrip));

  joggle::Mod imported_expand;
  constexpr std::string_view imported_expand_source =
      "module imported.expand\n"
      "use tensor\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  let y: tensor<f32, [4]> = relu(x)\n"
      "  return y\n}\n";
  CHECK(joggle::parse(env, imported_expand_source, imported_expand,
                      "imported-expand.jog"));
  CHECK(imported_expand.verify(env));
  joggle::Op imported_relu;
  for (joggle::Op op : imported_expand.ops())
    if (op.callee() == "relu")
      imported_relu = op;
  const std::uint64_t imported_revision = imported_expand.revision();
  CHECK(imported_relu && env.expand(imported_expand, imported_relu, relu));
  CHECK(imported_expand.revision() == imported_revision + 1);
  CHECK(imported_expand.uses() ==
        std::vector<std::string>({"tensor", "nn"}));
  CHECK(imported_expand.verify(env));

  joggle::Mod detached_implementation;
  CHECK(joggle::parse(env,
                      "module detached.impl\n"
                      "fn source(x: i32) -> i32 { return x }\n",
                      detached_implementation, "detached-impl.jog"));
  CHECK(detached_implementation.verify(env));
  joggle::Mod detached_user;
  CHECK(joggle::parse(env,
                      "module detached.user\n"
                      "fn main(x: i32) -> i32 {\n"
                      "  let y: i32 = source(x)\n"
                      "  return y\n"
                      "}\n",
                      detached_user, "detached-user.jog"));
  CHECK(detached_user.verify(env));
  const joggle::Op detached_call =
      detached_user.find_fn("main").body().ops()[0];
  const std::string detached_before = joggle::print(detached_user);
  const std::uint64_t detached_revision = detached_user.revision();
  CHECK(!env.expand(detached_user, detached_call,
                    detached_implementation.find_fn("source")));
  CHECK(joggle::print(detached_user) == detached_before);
  CHECK(detached_user.revision() == detached_revision);
  CHECK(detached_user.uses().empty());
  detached_user.clear_diags();
  CHECK(detached_user.verify(env));

  joggle::Mod rejected_expand;
  constexpr std::string_view rejected_expand_source =
      "module rejected.expand\n"
      "use tensor\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  [keep]\n"
      "  let y: tensor<f32, [4]> = relu(x)\n"
      "  return y\n}\n";
  CHECK(joggle::parse(env, rejected_expand_source, rejected_expand,
                      "rejected-expand.jog"));
  joggle::Op rejected_relu;
  for (joggle::Op op : rejected_expand.ops())
    if (op.callee() == "relu")
      rejected_relu = op;
  CHECK(rejected_relu && rejected_relu.meta("keep"));
  const std::string rejected_text = joggle::print(rejected_expand);
  const std::uint64_t rejected_revision = rejected_expand.revision();
  CHECK(!env.expand(rejected_expand, rejected_relu, relu));
  CHECK(joggle::print(rejected_expand) == rejected_text);
  CHECK(rejected_expand.revision() == rejected_revision);
  CHECK(rejected_expand.uses() == std::vector<std::string>{"tensor"});
  rejected_expand.clear_diags();
  CHECK(rejected_expand.verify(env));

  CHECK(env.load("onnx"));
  joggle::Mod bridged;
  constexpr std::string_view bridged_source =
      "module bridged\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  let y: tensor<f32, [4]> = onnx.Relu(x)\n"
      "  return y\n}\n";
  CHECK(joggle::parse(env, bridged_source, bridged, "bridged.jog"));
  CHECK(bridged.verify(env));
  CHECK(joggle::run(env, "script.bridge_relu", bridged));
  CHECK(bridged.verify(env));
  bool uses_nn = false;
  for (const std::string& module : bridged.uses())
    uses_nn = uses_nn || module == "nn";
  CHECK(uses_nn);
  joggle::Attr has_nn;
  const std::vector<joggle::Attr> nn_query{joggle::Attr("nn")};
  CHECK(joggle::query(env, "script.has_use", bridged, has_nn, nn_query));
  CHECK(has_nn.boolean() && *has_nn.boolean());
  joggle::Op bridged_relu;
  for (joggle::Op op : bridged.ops())
    if (op.callee() == "nn.relu")
      bridged_relu = op;
  CHECK(bridged_relu && env.resolve(bridged, bridged_relu) == relu);
  const std::uint64_t bridged_revision = bridged.revision();
  CHECK(joggle::run(env, "script.bridge_relu", bridged));
  CHECK(bridged.revision() == bridged_revision);
  joggle::Mod bridged_roundtrip;
  CHECK(joggle::parse(env, joggle::print(bridged), bridged_roundtrip,
                      "bridged-roundtrip.jog"));
  CHECK(bridged_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(bridged, bridged_roundtrip));

  joggle::Mod dependencies;
  CHECK(joggle::parse(env, "module dependencies\nfn main() -> int { "
                           "return 0 }\n",
                      dependencies, "dependencies.jog"));
  const std::uint64_t dependencies_revision = dependencies.revision();
  CHECK(dependencies.use(env, "nn"));
  CHECK(dependencies.revision() == dependencies_revision + 1);
  CHECK(dependencies.use(env, "nn"));
  CHECK(dependencies.revision() == dependencies_revision + 1);
  const std::string dependencies_text = joggle::print(dependencies);
  CHECK(!dependencies.use(env, "not-a-module"));
  CHECK(joggle::print(dependencies) == dependencies_text);
  CHECK(dependencies.revision() == dependencies_revision + 1);
  CHECK(!dependencies.use(env, "missing"));
  CHECK(joggle::print(dependencies) == dependencies_text);
  CHECK(dependencies.revision() == dependencies_revision + 1);
  dependencies.clear_diags();
  CHECK(dependencies.verify(env));

  joggle::Mod indirect_cycle;
  CHECK(joggle::parse(env,
                      "module opt\n"
                      "fn main() -> int { return 0 }\n",
                      indirect_cycle, "indirect-cycle.jog"));
  const std::string indirect_cycle_text = joggle::print(indirect_cycle);
  const std::uint64_t indirect_cycle_revision = indirect_cycle.revision();
  CHECK(!indirect_cycle.use(env, "script"));
  CHECK(joggle::print(indirect_cycle) == indirect_cycle_text);
  CHECK(indirect_cycle.uses().empty());
  CHECK(indirect_cycle.revision() == indirect_cycle_revision);
  CHECK(!indirect_cycle.diags().empty());
  CHECK(indirect_cycle.diags().back().message.find("dependency cycle") !=
        std::string::npos);

  joggle::Mod parsed_indirect_cycle;
  CHECK(joggle::parse(env,
                      "module opt\n"
                      "use script\n"
                      "fn main() -> int { return 0 }\n",
                      parsed_indirect_cycle, "parsed-indirect-cycle.jog"));
  CHECK(!parsed_indirect_cycle.verify(env));
  CHECK(std::any_of(
      parsed_indirect_cycle.diags().begin(),
      parsed_indirect_cycle.diags().end(), [](const joggle::Diag& diag) {
        return diag.message.find("dependency cycle through: script") !=
               std::string::npos;
      }));

  joggle::Mod missing_dependency;
  CHECK(joggle::parse(env,
                      "module missing_dependency\n"
                      "use missing\n"
                      "fn main() -> int { return 0 }\n",
                      missing_dependency, "missing-dependency.jog"));
  CHECK(!missing_dependency.verify(env));
  CHECK(!missing_dependency.diags().empty());
  CHECK(missing_dependency.diags().front().message.find(
            "dependency module is not loaded") != std::string::npos);

  joggle::Mod self_dependency;
  CHECK(joggle::parse(env,
                      "module self_dependency\n"
                      "use self_dependency\n"
                      "fn main() -> int { return 0 }\n",
                      self_dependency, "self-dependency.jog"));
  CHECK(!self_dependency.verify(env));
  CHECK(std::any_of(
      self_dependency.diags().begin(), self_dependency.diags().end(),
      [](const joggle::Diag& diag) {
        return diag.message.find("cannot depend on itself") !=
               std::string::npos;
      }));

  joggle::Mod duplicate_dependency;
  CHECK(joggle::parse(env,
                      "module duplicate_dependency\n"
                      "use base\n"
                      "use base\n"
                      "fn main() -> int { return 0 }\n",
                      duplicate_dependency, "duplicate-dependency.jog"));
  CHECK(!duplicate_dependency.verify(env));
  CHECK(std::any_of(
      duplicate_dependency.diags().begin(), duplicate_dependency.diags().end(),
      [](const joggle::Diag& diag) {
        return diag.message.find("duplicate module dependency") !=
               std::string::npos;
      }));

  constexpr std::string_view inferred_source =
      "module inferred\n"
      "use tensor\n"
      "fn main(x: i32) -> tensor<f32, [2, 3]> {\n"
      "  var y = opaque(x)\n"
      "  for i in 0..1 {\n"
      "    observe(y)\n"
      "  }\n"
      "  return y\n}\n";
  joggle::Mod inferred;
  joggle::Mod inferred_cpp;
  CHECK(joggle::parse(env, inferred_source, inferred, "inferred.jog"));
  CHECK(joggle::parse(env, inferred_source, inferred_cpp,
                      "inferred-cpp.jog"));
  CHECK(inferred.verify(env) && inferred_cpp.verify(env));
  CHECK(joggle::run(env, "script.type_opaque", inferred));
  joggle::Op opaque_cpp;
  for (joggle::Op op : inferred_cpp.ops())
    if (op.callee() == "opaque")
      opaque_cpp = op;
  const joggle::Ty inferred_type("tensor<f32, [2, 3]>");
  CHECK(opaque_cpp && inferred_cpp.type(opaque_cpp.outs()[0], inferred_type));
  CHECK(inferred.verify(env) && inferred_cpp.verify(env));
  CHECK(joggle::structurally_equal(inferred, inferred_cpp));
  joggle::Attr elements;
  const std::vector<joggle::Attr> opaque_query{joggle::Attr("opaque")};
  CHECK(joggle::query(env, "script.elements", inferred, elements,
                      opaque_query));
  CHECK(elements.integer() == 6);
  const std::string inferred_text = joggle::print(inferred);
  CHECK(inferred_text.find("var y: tensor<f32, [2, 3]> = opaque(x)") !=
        std::string::npos);
  joggle::Mod inferred_roundtrip;
  CHECK(joggle::parse(env, inferred_text, inferred_roundtrip,
                      "inferred-roundtrip.jog"));
  CHECK(inferred_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(inferred, inferred_roundtrip));
  const std::uint64_t inferred_revision = inferred_cpp.revision();
  const std::string inferred_cpp_text = joggle::print(inferred_cpp);
  CHECK(!inferred_cpp.type(opaque_cpp.outs()[0], joggle::Ty("tensor<")));
  CHECK(inferred_cpp.revision() == inferred_revision);
  CHECK(joggle::print(inferred_cpp) == inferred_cpp_text);
  inferred_cpp.clear_diags();
  CHECK(inferred_cpp.verify(env));

  joggle::Mod precedence;
  constexpr std::string_view precedence_source =
      "module precedence\n"
      "use base\n"
      "fn logic(a: bool, b: bool, c: bool) -> bool {\n"
      "  return a && (b || c)\n}\n"
      "fn math(a: int, b: int, c: int) -> int {\n"
      "  return (a + b) * (c - (a - b))\n}\n";
  CHECK(joggle::parse(env, precedence_source, precedence, "precedence.jog"));
  CHECK(precedence.verify(env));
  const std::string precedence_text = joggle::print(precedence);
  CHECK(precedence_text.find("a && (b || c)") != std::string::npos);
  CHECK(precedence_text.find("(a + b) * (c - (a - b))") !=
        std::string::npos);
  joggle::Mod precedence_roundtrip;
  CHECK(joggle::parse(env, precedence_text, precedence_roundtrip,
                      "precedence-roundtrip.jog"));
  CHECK(precedence_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(precedence, precedence_roundtrip));

  joggle::Mod types;
  constexpr std::string_view type_source =
      "module types\n"
      "use tensor\n"
      "fn id<E: Ty, S: list<int>>(x: tensor<E, S>) -> tensor<E, S>;\n"
      "fn make<T: Ty>(x: i32) -> T;\n"
      "fn apply(x: tensor<f32, [2, 3]>) -> tensor<f32, [2, 3]> {\n"
      "  return id(x)\n"
      "}\n"
      "fn choose(x: i32) -> f32 {\n"
      "  let y: f32 = make(x)\n"
      "  return y\n"
      "}\n"
      "fn last(xs: list<int>) -> int {\n"
      "  var out = 0\n"
      "  for x in xs { out = x }\n"
      "  return out\n"
      "}\n";
  CHECK(joggle::parse(env, type_source, types, "types.jog"));
  CHECK(types.verify(env));
  const joggle::Val applied =
      types.find_fn("apply").body().ops().back().args().front();
  CHECK(applied.type() == joggle::Ty("tensor<f32, [2, 3]>"));
  const joggle::Op make = types.find_fn("choose").body().ops().front();
  CHECK(make.callee() == "make");
  CHECK(env.resolve(types, make));
  CHECK(make.outs().front().type() == joggle::Ty("f32"));
  const std::vector<joggle::Blk> last_blks = types.find_fn("last").blks();
  CHECK(last_blks.size() == 2);
  CHECK(last_blks[1].args().front().type() == joggle::Ty("int"));
  joggle::Mod types_roundtrip;
  CHECK(joggle::parse(env, joggle::print(types), types_roundtrip,
                      "types-roundtrip.jog"));
  CHECK(types_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(types, types_roundtrip));

  joggle::Mod type_composition;
  constexpr std::string_view type_composition_source =
      "module type_composition\n"
      "use tensor\n"
      "fn tensor(x: i32) -> i32;\n"
      "fn scalar(x: i32) -> i32 { return tensor(x) }\n"
      "fn shaped(x: tensor<f32, [4]>) -> tensor<f32, [4]> { return x }\n";
  CHECK(joggle::parse(env, type_composition_source, type_composition,
                      "type-composition.jog"));
  CHECK(type_composition.verify(env));
  const joggle::Op scalar_call =
      type_composition.find_fn("scalar").body().ops().front();
  CHECK(env.resolve(type_composition, scalar_call).module() ==
        "type_composition");
  CHECK(type_composition.find_fn("shaped").params().front().type() ==
        joggle::Ty("tensor<f32, [4]>"));

  joggle::Mod wrong_shape;
  CHECK(joggle::parse(
      env,
      "module wrong_shape\nuse tensor\n"
      "fn bad(x: tensor<f32, 4>) -> tensor<f32, 4> { return x }\n",
      wrong_shape, "wrong-shape.jog"));
  CHECK(!wrong_shape.verify(env));
  CHECK(!wrong_shape.diags().empty());
  CHECK(wrong_shape.diags().front().message.find("expected 'list<int>'") !=
        std::string::npos);

  joggle::Mod multi;
  constexpr std::string_view multi_source =
      "module multi\n"
      "fn split(x: i32) -> (i32, bool);\n"
      "fn first(x: i32) -> i32 {\n"
      "  let value: i32, valid: bool = split(x)\n"
      "  return value\n"
      "}\n";
  CHECK(joggle::parse(env, multi_source, multi, "multi.jog"));
  CHECK(multi.verify(env));
  const joggle::Op split = multi.find_fn("first").body().ops().front();
  CHECK(split.outs().size() == 2);
  CHECK(split.outs()[0].type() == joggle::Ty("i32"));
  CHECK(split.outs()[1].type() == joggle::Ty("bool"));
  const std::string multi_text = joggle::print(multi);
  CHECK(multi_text.find("let value: i32, valid: bool = split(x)") !=
        std::string::npos);
  joggle::Mod multi_roundtrip;
  CHECK(joggle::parse(env, multi_text, multi_roundtrip, "multi-roundtrip.jog"));
  CHECK(multi_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(multi, multi_roundtrip));

  joggle::Mod annotated;
  constexpr std::string_view annotated_source =
      "module annotated\n"
      "fn add(x: i32) -> i32 {\n"
      "  [cost: 3, place: \"edge\"]\n"
      "  let y = x + 1\n"
      "  [trace]\n"
      "  return y\n"
      "}\n";
  CHECK(joggle::parse(env, annotated_source, annotated, "annotated.jog"));
  CHECK(annotated.verify(env));
  const joggle::Fn annotated_fn = annotated.find_fn("add");
  const std::vector<joggle::Op> annotated_ops = annotated_fn.body().ops();
  CHECK(annotated_ops.size() == 3);
  CHECK(annotated_ops[1].meta("place") &&
        annotated_ops[1].meta("place")->string() == "edge");
  CHECK(annotated_ops.back().meta("trace") &&
        annotated_ops.back().meta("trace")->boolean() == true);
  CHECK(annotated.set(annotated_ops[1], "layout", joggle::Attr("packed")));
  CHECK(annotated.unset(annotated_ops[1], "cost"));
  CHECK(!annotated_ops[1].meta("cost"));
  CHECK(annotated.set(annotated_fn, "pipeline", joggle::Attr("fast")));
  CHECK(annotated_fn.meta("pipeline") &&
        annotated_fn.meta("pipeline")->string() == "fast");
  const std::string annotated_text = joggle::print(annotated);
  CHECK(annotated_text.find("[layout: \"packed\", place: \"edge\"]") !=
        std::string::npos);
  joggle::Mod annotated_roundtrip;
  CHECK(joggle::parse(env, annotated_text, annotated_roundtrip,
                      "annotated-roundtrip.jog"));
  CHECK(annotated_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(annotated, annotated_roundtrip));

  joggle::Mod selective;
  constexpr std::string_view selective_source =
      "module selective\n"
      "fn choose(x: i32) -> i32 {\n"
      "  let shared = x + 0\n"
      "  let left = shared + 1\n"
      "  let right = shared + 2\n"
      "  return left + right\n"
      "}\n";
  CHECK(joggle::parse(env, selective_source, selective, "selective.jog"));
  CHECK(selective.verify(env));
  const joggle::Fn choose = selective.find_fn("choose");
  CHECK(choose.ops().size() == choose.body().ops().size());
  CHECK(selective.ops().size() == choose.ops().size());
  const joggle::Val shared = choose.body().ops()[1].outs().front();
  const std::vector<joggle::Op> shared_users = shared.users();
  CHECK(shared_users.size() == 2);
  const std::uint64_t before_replace = selective.revision();
  CHECK(selective.replace(shared, choose.params().front(), shared_users[0]));
  CHECK(selective.revision() == before_replace + 1);
  CHECK(selective.verify(env));
  const std::string selective_text = joggle::print(selective);
  CHECK(selective_text.find("let left = x + 1") != std::string::npos);
  CHECK(selective_text.find("let right = shared + 2") != std::string::npos);

  joggle::Mod cloned_loop;
  constexpr std::string_view loop_source =
      "module looped\n"
      "fn sum(n: int) -> int {\n"
      "  var total = 0\n"
      "  for i in 0..n {\n"
      "    total += i\n"
      "  }\n"
      "  return total\n"
      "}\n";
  CHECK(joggle::parse(env, loop_source, cloned_loop, "looped.jog"));
  CHECK(cloned_loop.verify(env));
  joggle::Op old_loop;
  for (joggle::Op op : cloned_loop.ops())
    if (op.kind() == joggle::Op::Kind::loop)
      old_loop = op;
  CHECK(old_loop && old_loop.outs().size() == 1);
  const joggle::Blk old_body = old_loop.blks().front();
  const joggle::Op copied_loop = cloned_loop.clone(old_loop, old_loop);
  CHECK(copied_loop && copied_loop.blks().size() == 1);
  CHECK(cloned_loop.replace(old_loop.outs()[0], copied_loop.outs()[0]));
  CHECK(cloned_loop.erase(old_loop));
  CHECK(!old_loop.valid() && !old_body.valid());
  CHECK(cloned_loop.verify(env));
  joggle::Mod cloned_roundtrip;
  CHECK(joggle::parse(env, joggle::print(cloned_loop), cloned_roundtrip,
                      "cloned-roundtrip.jog"));
  CHECK(cloned_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(cloned_loop, cloned_roundtrip));

  joggle::Mod scheduled;
  constexpr std::string_view schedule_source =
      "module scheduled\n"
      "fn schedule(x: i32) -> i32 {\n"
      "  let a = first(x)\n"
      "  let b = second(x)\n"
      "  return a + b\n"
      "}\n";
  CHECK(joggle::parse(env, schedule_source, scheduled, "scheduled.jog"));
  CHECK(scheduled.verify(env));
  const std::vector<joggle::Op> schedule_ops =
      scheduled.find_fn("schedule").body().ops();
  const std::uint64_t before_move = scheduled.revision();
  CHECK(scheduled.move(schedule_ops[1], schedule_ops[0]));
  CHECK(scheduled.revision() == before_move + 1);
  CHECK(scheduled.verify(env));
  const std::string scheduled_text = joggle::print(scheduled);
  CHECK(scheduled_text.find("let b = second(x)") <
        scheduled_text.find("let a = first(x)"));
  const std::uint64_t before_bad_move = scheduled.revision();
  CHECK(!scheduled.move(schedule_ops[2], schedule_ops[0]));
  CHECK(scheduled.revision() == before_bad_move);
  scheduled.clear_diags();
  CHECK(scheduled.verify(env));

  joggle::Mod cloned_branch;
  constexpr std::string_view branch_source =
      "module branched\n"
      "fn choose(x: i32, flag: bool) -> i32 {\n"
      "  var result = x\n"
      "  if flag {\n"
      "    result += 1\n"
      "  } else {\n"
      "    result += 2\n"
      "  }\n"
      "  return result\n"
      "}\n";
  CHECK(joggle::parse(env, branch_source, cloned_branch, "branched.jog"));
  CHECK(cloned_branch.verify(env));
  joggle::Op old_branch;
  for (joggle::Op op : cloned_branch.ops())
    if (op.kind() == joggle::Op::Kind::branch)
      old_branch = op;
  CHECK(old_branch && old_branch.blks().size() == 2);
  const joggle::Op copied_branch = cloned_branch.clone(old_branch, old_branch);
  CHECK(copied_branch && copied_branch.blks().size() == 2);
  CHECK(cloned_branch.replace(old_branch.outs()[0],
                              copied_branch.outs()[0]));
  CHECK(cloned_branch.erase(old_branch));
  CHECK(cloned_branch.verify(env));

  joggle::Mod built_control;
  constexpr std::string_view control_seed =
      "module control\n"
      "fn build(n: int, flag: bool) -> int { return n }\n";
  CHECK(joggle::parse(env, control_seed, built_control, "control.jog"));
  CHECK(built_control.verify(env));
  const joggle::Fn build_fn = built_control.find_fn("build");
  const joggle::Op build_ret = build_fn.body().ops().back();
  const joggle::Val zero =
      built_control.constant(build_ret, joggle::Attr(std::int64_t{0}),
                             joggle::Ty("int"));
  CHECK(zero && built_control.rename(zero, "total"));
  const std::vector<joggle::Val> range_args{zero, build_fn.params().front()};
  const joggle::Val range = built_control.call(
      build_ret, "operator ..", range_args, joggle::Ty("range"));
  CHECK(range);
  const std::vector<std::string> iter_names{"i"};
  const std::vector<joggle::Val> sources{range};
  const std::vector<joggle::Val> carried{zero};
  const joggle::Op built_loop =
      built_control.loop(build_ret, iter_names, sources, carried);
  CHECK(built_loop && built_loop.blks().size() == 1);
  const joggle::Blk built_body = built_loop.blks().front();
  CHECK(built_body.args().size() == 2);
  const joggle::Op built_yield = built_body.ops().back();
  const std::vector<joggle::Val> inner_range_args{zero,
                                                  built_body.args()[0]};
  const joggle::Val inner_range = built_control.call(
      built_yield, "operator ..", inner_range_args, joggle::Ty("range"));
  CHECK(inner_range);
  const std::vector<std::string> inner_names{"j"};
  const std::vector<joggle::Val> inner_sources{inner_range};
  const std::vector<joggle::Val> inner_carried{built_body.args()[1]};
  const joggle::Op inner_loop = built_control.loop(
      built_yield, inner_names, inner_sources, inner_carried);
  CHECK(inner_loop && inner_loop.blks().size() == 1);
  const joggle::Blk inner_body = inner_loop.blks().front();
  const joggle::Op inner_yield = inner_body.ops().back();
  const std::vector<joggle::Val> sum_args{inner_body.args()[1],
                                          inner_body.args()[0]};
  const joggle::Val sum = built_control.call(
      inner_yield, "operator +", sum_args, joggle::Ty("int"));
  CHECK(sum && built_control.rename(sum, "total"));
  const std::vector<joggle::Val> inner_values{sum};
  CHECK(built_control.args(env, inner_yield, inner_values));
  CHECK(built_control.args(env, built_yield, inner_loop.outs()));
  const joggle::Op built_branch = built_control.branch(
      build_ret, build_fn.params()[1], built_loop.outs());
  CHECK(built_branch && built_branch.blks().size() == 2);
  const joggle::Blk then_blk = built_branch.blks().front();
  const joggle::Op then_yield = then_blk.ops().back();
  const joggle::Val one =
      built_control.constant(then_yield, joggle::Attr(std::int64_t{1}),
                             joggle::Ty("int"));
  CHECK(one && built_control.rename(one, "one"));
  const std::vector<joggle::Val> then_args{then_blk.args().front(), one};
  const joggle::Val increment = built_control.call(
      then_yield, "operator +", then_args, joggle::Ty("int"));
  CHECK(increment && built_control.rename(increment, "total"));
  const std::vector<joggle::Val> then_values{increment};
  CHECK(built_control.args(env, then_yield, then_values));
  CHECK(built_control.args(env, build_ret, built_branch.outs()));
  CHECK(built_control.verify(env));
  const std::string built_control_text = joggle::print(built_control);
  CHECK(built_control_text.find("for i in total..n") != std::string::npos);
  CHECK(built_control_text.find("for j in total..i") != std::string::npos);
  CHECK(built_control_text.find("if flag") != std::string::npos);
  joggle::Mod control_roundtrip;
  CHECK(joggle::parse(env, built_control_text, control_roundtrip,
                      "control-roundtrip.jog"));
  CHECK(control_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(built_control, control_roundtrip));

  joggle::Mod renamed_control;
  CHECK(joggle::parse(env, built_control_text, renamed_control,
                      "renamed-control.jog"));
  std::vector<joggle::Op> renamed_loops;
  joggle::Op renamed_branch;
  for (joggle::Op op : renamed_control.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      renamed_loops.push_back(op);
    else if (op.kind() == joggle::Op::Kind::branch)
      renamed_branch = op;
  }
  CHECK(renamed_loops.size() == 2 && renamed_branch);
  const std::uint64_t rename_revision = renamed_control.revision();
  CHECK(!renamed_control.rename(renamed_loops[0].blks()[0].args()[0],
                                "return"));
  CHECK(renamed_control.revision() == rename_revision);
  renamed_control.clear_diags();
  CHECK(!renamed_control.rename(renamed_loops[0].blks()[0].args()[0],
                                "total"));
  CHECK(renamed_control.revision() == rename_revision);
  renamed_control.clear_diags();
  CHECK(renamed_control.rename(renamed_loops[0].blks()[0].args()[0],
                               "outer"));
  CHECK(renamed_control.rename(renamed_loops[1].blks()[0].args()[0],
                               "inner"));
  CHECK(renamed_control.rename(renamed_branch.blks()[0].args()[0], "acc"));
  CHECK(renamed_control.verify(env));
  const std::string renamed_control_text = joggle::print(renamed_control);
  CHECK(renamed_control_text.find("var acc: int = 0") != std::string::npos);
  CHECK(renamed_control_text.find("for outer in acc..n") != std::string::npos);
  CHECK(renamed_control_text.find("for inner in acc..outer") !=
        std::string::npos);
  CHECK(renamed_control_text.find("acc + inner") != std::string::npos);
  joggle::Mod renamed_roundtrip;
  CHECK(joggle::parse(env, renamed_control_text, renamed_roundtrip,
                      "renamed-control-roundtrip.jog"));
  CHECK(renamed_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(renamed_control, renamed_roundtrip));

  joggle::Mod wrong_result_type;
  CHECK(joggle::parse(env,
                      "module wrong_result\n"
                      "fn split(x: i32) -> (i32, bool);\n"
                      "fn bad(x: i32) -> str {\n"
                      "  let value: str, ok: bool = split(x)\n"
                      "  return value\n}\n",
                      wrong_result_type, "wrong-result.jog"));
  CHECK(!wrong_result_type.verify(env));
  CHECK(!wrong_result_type.diags().empty());
  CHECK(wrong_result_type.diags().front().message.find("requires type 'str'") !=
        std::string::npos);

  constexpr std::string_view failed_inference_source =
      "module failed.inference\n"
      "use base\n"
      "fn main(x: i32) -> i64 {\n"
      "  let y = base.copy(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod failed_inference;
  CHECK(joggle::parse(env, failed_inference_source, failed_inference,
                      "failed-inference.jog"));
  joggle::Val inferred_on_failure;
  for (joggle::Op op : failed_inference.ops()) {
    if (op.kind() == joggle::Op::Kind::call &&
        op.callee() == "base.copy")
      inferred_on_failure = op.outs().front();
  }
  CHECK(inferred_on_failure && inferred_on_failure.type() == joggle::Ty("_"));
  const std::string failed_inference_before = joggle::print(failed_inference);
  const std::uint64_t failed_inference_revision = failed_inference.revision();
  CHECK(!failed_inference.verify(env));
  CHECK(inferred_on_failure.type() == joggle::Ty("_"));
  CHECK(joggle::print(failed_inference) == failed_inference_before);
  CHECK(failed_inference.revision() == failed_inference_revision);
  CHECK(!failed_inference.diags().empty());

  joggle::Mod built_multi;
  CHECK(joggle::parse(env,
                      "module built\n"
                      "fn split(x: i32) -> (i32, bool);\n"
                      "fn observe(x: i32) -> ();\n"
                      "fn first(x: i32) -> i32 { return x }\n",
                      built_multi, "built-multi.jog"));
  const joggle::Fn built_first = built_multi.find_fn("first");
  const joggle::Op before = built_first.body().ops().back();
  const std::vector<joggle::Val> split_args{built_first.params().front()};
  const std::vector<joggle::Ty> split_types{joggle::Ty("i32"),
                                            joggle::Ty("bool")};
  const std::uint64_t before_calls = built_multi.revision();
  const joggle::Op built_split =
      built_multi.call(before, "split", split_args, split_types);
  CHECK(built_split && built_split.outs().size() == 2);
  CHECK(built_multi.revision() == before_calls + 1);
  const std::vector<joggle::Ty> no_types;
  const joggle::Op observe =
      built_multi.call(before, "observe", split_args, no_types);
  CHECK(observe && observe.outs().empty());
  CHECK(built_multi.rename(built_split.outs()[0], "value"));
  CHECK(built_multi.rename(built_split.outs()[1], "valid"));
  CHECK(built_multi.verify(env));
  CHECK(joggle::print(built_multi)
            .find("let value: i32, valid: bool = split(x)") !=
        std::string::npos);
  CHECK(joggle::print(built_multi).find("observe(x)") != std::string::npos);

  joggle::Mod overloaded;
  constexpr std::string_view overload_source =
      "module overloads\n"
      "fn choose(x: int) -> str;\n"
      "fn choose(x: str) -> int;\n"
      "fn select<T>(x: T) -> int;\n"
      "fn select(x: str) -> str;\n"
      "fn +<T>(a: T, b: T) -> T;\n"
      "fn from_int(x: int) -> str { return choose(x) }\n"
      "fn from_str(x: str) -> int { return choose(x) }\n"
      "fn specific(x: str) -> str { return select(x) }\n"
      "fn plus(a: i32, b: i32) -> i32 { return a + b }\n";
  CHECK(joggle::parse(env, overload_source, overloaded, "overloads.jog"));
  CHECK(overloaded.verify(env));
  CHECK(!overloaded.find_fn("choose"));
  CHECK(overloaded.find_fns("choose").size() == 2);
  CHECK(overloaded.find_fn("from_int")
            .body()
            .ops()
            .back()
            .args()
            .front()
            .type() == joggle::Ty("str"));
  CHECK(overloaded.find_fn("specific")
            .body()
            .ops()
            .back()
            .args()
            .front()
            .type() == joggle::Ty("str"));
  joggle::Mod overload_roundtrip;
  CHECK(joggle::parse(env, joggle::print(overloaded), overload_roundtrip,
                      "overloads-roundtrip.jog"));
  CHECK(overload_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(overloaded, overload_roundtrip));

  constexpr std::string_view result_context_source =
      "module result_context\n"
      "fn choose<T: Ty>(x: i32) -> T;\n"
      "fn choose(x: i32) -> i32;\n"
      "fn use(x: i32) -> f32 { return choose(x) }\n";
  constexpr std::string_view reversed_result_context_source =
      "module reversed_result_context\n"
      "fn choose(x: i32) -> i32;\n"
      "fn choose<T: Ty>(x: i32) -> T;\n"
      "fn use(x: i32) -> f32 { return choose(x) }\n";
  for (const auto& [source, file] :
       {std::pair{result_context_source, "result-context.jog"},
        std::pair{reversed_result_context_source,
                  "reversed-result-context.jog"}}) {
    joggle::Mod result_context;
    CHECK(joggle::parse(env, source, result_context, file));
    CHECK(result_context.verify(env));
    const joggle::Val returned =
        result_context.find_fn("use").body().ops().back().args().front();
    CHECK(returned.type() == joggle::Ty("f32"));
  }

  joggle::Mod ambiguous;
  constexpr std::string_view ambiguous_source =
      "module ambiguous\n"
      "fn pick<A>(x: pair<A, i32>) -> int;\n"
      "fn pick<B>(x: pair<i32, B>) -> int;\n"
      "fn use(x: pair<i32, i32>) -> int { return pick(x) }\n";
  CHECK(joggle::parse(env, ambiguous_source, ambiguous, "ambiguous.jog"));
  CHECK(!ambiguous.verify(env));
  CHECK(!ambiguous.diags().empty());
  CHECK(ambiguous.diags().front().message.find("ambiguous") !=
        std::string::npos);

  joggle::Mod duplicate_overload;
  CHECK(!joggle::parse(env,
                       "module duplicate\n"
                       "fn same(x: int) -> int;\n"
                       "fn same(x: int) -> str;\n",
                       duplicate_overload, "duplicate-overload.jog"));
  CHECK(!duplicate_overload.diags().empty());

  joggle::Mod duplicate_generic;
  CHECK(!joggle::parse(env,
                       "module duplicate\n"
                       "fn same<T>(x: T) -> int;\n"
                       "fn same<U>(x: U) -> int;\n",
                       duplicate_generic, "duplicate-generic.jog"));
  CHECK(!duplicate_generic.diags().empty());

  joggle::Mod duplicate_parameter;
  CHECK(!joggle::parse(env,
                       "module duplicate\n"
                       "fn same<T, T>(x: int) -> int;\n",
                       duplicate_parameter, "duplicate-parameter.jog"));
  CHECK(!duplicate_parameter.diags().empty());

  joggle::Mod wrong_generic_kind;
  CHECK(joggle::parse(env,
                      "module kinds\n"
                      "fn width<W: int>(x: int) -> int;\n"
                      "fn bad(x: int) -> int { return width<f32>(x) }\n",
                      wrong_generic_kind, "wrong-generic-kind.jog"));
  CHECK(!wrong_generic_kind.verify(env));
  CHECK(!wrong_generic_kind.diags().empty());
  CHECK(wrong_generic_kind.diags().front().message.find("expected 'int'") !=
        std::string::npos);

  joggle::Mod wrong_result;
  CHECK(joggle::parse(env,
                      "module wrong_result\n"
                      "fn make<T: Ty>() -> T;\n"
                      "fn bad() -> i32 { return make<f32>() }\n",
                      wrong_result, "wrong-result.jog"));
  CHECK(!wrong_result.verify(env));
  CHECK(!wrong_result.diags().empty());
  CHECK(wrong_result.diags().front().message.find(
            "requires type 'i32'") != std::string::npos);

  joggle::Mod wrong_result_count;
  CHECK(joggle::parse(env,
                      "module wrong_result_count\n"
                      "fn split() -> (i32, bool);\n"
                      "fn bad() -> i32 { return split() }\n",
                      wrong_result_count, "wrong-result-count.jog"));
  CHECK(!wrong_result_count.verify(env));
  CHECK(!wrong_result_count.diags().empty());
  CHECK(wrong_result_count.diags().front().message.find(
            "expects 2 results, got 1") != std::string::npos);

  joggle::Mod statement_calls;
  constexpr std::string_view statement_call_source =
      "module statement_calls\n"
      "fn observe(x: i32) -> ();\n"
      "fn value(x: i32) -> i32;\n"
      "fn run(x: i32) -> () {\n"
      "  observe(x)\n"
      "  value(x)\n"
      "  return\n"
      "}\n";
  CHECK(joggle::parse(env, statement_call_source, statement_calls,
                      "statement-calls.jog"));
  const std::uint64_t statement_revision = statement_calls.revision();
  const std::vector<joggle::Attr> value_query{joggle::Attr("value")};
  joggle::Attr statement_count;
  bool statement_cached = true;
  CHECK(joggle::query(env, "opt.count", statement_calls, statement_count,
                      value_query, &statement_cached));
  CHECK(!statement_cached && statement_count.integer() == 1);
  CHECK(joggle::query(env, "opt.count", statement_calls, statement_count,
                      value_query, &statement_cached));
  CHECK(statement_cached);
  CHECK(statement_calls.verify(env));
  CHECK(statement_calls.revision() == statement_revision + 1);
  CHECK(joggle::query(env, "opt.count", statement_calls, statement_count,
                      value_query, &statement_cached));
  CHECK(!statement_cached && statement_count.integer() == 1);
  const std::uint64_t verified_statement_revision =
      statement_calls.revision();
  CHECK(statement_calls.verify(env));
  CHECK(statement_calls.revision() == verified_statement_revision);
  const std::vector<joggle::Op> statement_ops =
      statement_calls.find_fn("run").body().ops();
  CHECK(statement_ops.size() == 3);
  CHECK(statement_ops[0].callee() == "observe");
  CHECK(statement_ops[0].outs().empty());
  CHECK(statement_ops[1].callee() == "value");
  CHECK(statement_ops[1].outs().size() == 1);
  CHECK(statement_ops[1].outs().front().type() == joggle::Ty("i32"));
  joggle::Mod statement_roundtrip;
  CHECK(joggle::parse(env, joggle::print(statement_calls), statement_roundtrip,
                      "statement-roundtrip.jog"));
  CHECK(statement_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(statement_calls, statement_roundtrip));

  joggle::Mod incompatible_result;
  CHECK(joggle::parse(env,
                      "module incompatible_result\n"
                      "fn implementation(x: i32) -> f32;\n"
                      "fn apply(x: i32) -> i32 {\n"
                      "  let y: i32 = source(x)\n"
                      "  return y\n"
                      "}\n",
                      incompatible_result, "incompatible-result.jog"));
  CHECK(incompatible_result.verify(env));
  const joggle::Op source_call =
      incompatible_result.find_fn("apply").body().ops().front();
  CHECK(!env.accepts(source_call,
                     incompatible_result.find_fn("implementation")));
  const std::string before_retarget = joggle::print(incompatible_result);
  const std::uint64_t before_retarget_revision =
      incompatible_result.revision();
  CHECK(!incompatible_result.retarget(env, source_call, "implementation",
                                      source_call.args()));
  CHECK(joggle::print(incompatible_result) == before_retarget);
  CHECK(incompatible_result.revision() == before_retarget_revision);
  CHECK(!incompatible_result.diags().empty());
  CHECK(incompatible_result.diags().back().message.find(
            "does not accept the call signature") != std::string::npos);

  joggle::Mod explicit_match;
  CHECK(joggle::parse(env,
                      "module explicit_match\n"
                      "fn candidate<T: Ty>(x: T) -> T;\n"
                      "fn incompatible(x: i32) -> i32 {\n"
                      "  let y: i32 = source<f32>(x)\n"
                      "  return y\n"
                      "}\n"
                      "fn compatible(x: i32) -> i32 {\n"
                      "  let y: i32 = source<i32>(x)\n"
                      "  return y\n"
                      "}\n",
                      explicit_match, "explicit-match.jog"));
  CHECK(explicit_match.verify(env));
  const joggle::Fn explicit_candidate = explicit_match.find_fn("candidate");
  CHECK(!env.accepts(explicit_match.find_fn("incompatible").body().ops()[0],
                     explicit_candidate));
  CHECK(env.accepts(explicit_match.find_fn("compatible").body().ops()[0],
                    explicit_candidate));

  joggle::Mod rename_safety;
  CHECK(joggle::parse(env,
                      "module rename_safety\n"
                      "fn incompatible(x: i32) -> f32;\n"
                      "fn compatible(x: i32) -> i32;\n"
                      "fn main(x: i32) -> i32 {\n"
                      "  let y: i32 = source(x)\n"
                      "  return y\n"
                      "}\n",
                      rename_safety, "rename-safety.jog"));
  CHECK(rename_safety.verify(env));
  const joggle::Op renamed_call =
      rename_safety.find_fn("main").body().ops()[0];
  const std::string rename_before = joggle::print(rename_safety);
  const std::uint64_t rename_safety_revision = rename_safety.revision();
  CHECK(!rename_safety.rename(env, renamed_call, "bad callee"));
  CHECK(joggle::print(rename_safety) == rename_before);
  CHECK(rename_safety.revision() == rename_safety_revision);
  CHECK(!rename_safety.rename(env, renamed_call, "incompatible"));
  CHECK(joggle::print(rename_safety) == rename_before);
  CHECK(rename_safety.revision() == rename_safety_revision);
  CHECK(rename_safety.rename(env, renamed_call, "compatible"));
  CHECK(rename_safety.verify(env));

  joggle::Mod constant_safety;
  CHECK(joggle::parse(env,
                      "module constant_safety\n"
                      "fn main() -> int { return 0 }\n",
                      constant_safety, "constant-safety.jog"));
  CHECK(constant_safety.verify(env));
  const joggle::Op constant_ret =
      constant_safety.find_fn("main").body().ops().back();
  const std::string constant_before = joggle::print(constant_safety);
  const std::uint64_t constant_revision = constant_safety.revision();
  CHECK(!constant_safety.call(constant_ret, "bad callee", {},
                              joggle::Ty("int")));
  CHECK(joggle::print(constant_safety) == constant_before);
  CHECK(constant_safety.revision() == constant_revision);
  CHECK(!constant_safety.constant(constant_ret, joggle::Attr("not an int"),
                                  joggle::Ty("i32")));
  CHECK(joggle::print(constant_safety) == constant_before);
  CHECK(constant_safety.revision() == constant_revision);
  const joggle::Attr::List mixed{joggle::Attr(std::int64_t{1}),
                                 joggle::Attr("two")};
  CHECK(!constant_safety.constant(constant_ret, joggle::Attr(mixed),
                                  joggle::Ty("list<int>")));
  CHECK(joggle::print(constant_safety) == constant_before);
  CHECK(constant_safety.revision() == constant_revision);
  CHECK(constant_safety.constant(constant_ret, joggle::Attr("encoded"),
                                 joggle::Ty("format<8>")));
  CHECK(constant_safety.verify(env));

  joggle::Mod args_safety;
  CHECK(joggle::parse(env,
                      "module args_safety\n"
                      "fn take(x: i32) -> i32 { return x }\n"
                      "fn apply(x: i32, other: f32) -> i32 {\n"
                      "  return take(x)\n"
                      "}\n"
                      "fn open(x: i32, other: f32) -> i32 {\n"
                      "  let y: i32 = source(x)\n"
                      "  return y\n"
                      "}\n",
                      args_safety, "args-safety.jog"));
  CHECK(args_safety.verify(env));
  const joggle::Fn args_apply = args_safety.find_fn("apply");
  const joggle::Op take_call =
      args_apply.body().ops().back().args().front().def();
  const std::string before_args = joggle::print(args_safety);
  const std::uint64_t before_args_revision = args_safety.revision();
  const std::vector<joggle::Val> no_args;
  const std::vector<joggle::Val> wrong_args{args_apply.params()[1]};
  const std::vector<joggle::Val> same_args{args_apply.params()[0]};
  CHECK(!args_safety.args(env, take_call, no_args));
  CHECK(!args_safety.args(env, take_call, wrong_args));
  CHECK(joggle::print(args_safety) == before_args);
  CHECK(args_safety.revision() == before_args_revision);
  CHECK(args_safety.args(env, take_call, same_args));
  CHECK(args_safety.revision() == before_args_revision);
  const joggle::Fn open_args = args_safety.find_fn("open");
  const joggle::Op open_call = open_args.body().ops().front();
  const std::vector<joggle::Val> open_values{open_args.params()[1]};
  CHECK(args_safety.args(env, open_call, open_values));
  CHECK(args_safety.revision() == before_args_revision + 1);
  CHECK(args_safety.verify(env));

  joggle::Mod replace_safety;
  CHECK(joggle::parse(env,
                      "module replace_safety\n"
                      "fn consume(x: i32) -> i32;\n"
                      "fn apply() -> i32 {\n"
                      "  let open = source()\n"
                      "  let concrete: f32 = value()\n"
                      "  return consume(open)\n"
                      "}\n",
                      replace_safety, "replace-safety.jog"));
  CHECK(replace_safety.verify(env));
  const std::vector<joggle::Op> replace_ops =
      replace_safety.find_fn("apply").body().ops();
  const joggle::Val open_value = replace_ops[0].outs().front();
  const joggle::Val concrete_value = replace_ops[1].outs().front();
  CHECK(open_value.type() == joggle::Ty("_"));
  CHECK(concrete_value.type() == joggle::Ty("f32"));
  const std::string before_unsafe_replace = joggle::print(replace_safety);
  const std::uint64_t before_unsafe_replace_revision =
      replace_safety.revision();
  CHECK(!replace_safety.replace(open_value, concrete_value));
  CHECK(joggle::print(replace_safety) == before_unsafe_replace);
  CHECK(replace_safety.revision() == before_unsafe_replace_revision);

  joggle::Attr load_count;
  const std::vector<joggle::Attr> choose_query{joggle::Attr("choose")};
  bool load_cached = true;
  CHECK(joggle::query(env, "opt.count", overloaded, load_count,
                      choose_query, &load_cached));
  CHECK(!load_cached && load_count.integer() == 2);
  CHECK(joggle::query(env, "opt.count", overloaded, load_count,
                      choose_query, &load_cached));
  CHECK(load_cached);

  CHECK(!env.loaded("sample"));
  CHECK(!env.load("bad"));
  CHECK(!env.loaded("bad"));
  CHECK(!env.loaded("sample"));
  CHECK(!env.bound("sample.ping"));
  CHECK(!env.diags().empty());
  env.clear_diags();
  CHECK(joggle::query(env, "opt.count", overloaded, load_count,
                      choose_query, &load_cached));
  CHECK(load_cached && load_count.integer() == 2);

  CHECK(env.load("sample"));
  CHECK(env.bound("sample.ping"));
  const joggle::Fn ping = env.find_fn("sample.ping");
  CHECK(ping && ping.external());
  CHECK(ping.module() == "sample");
  CHECK(!ping.meta("host"));
  CHECK(ping.meta("role") && ping.meta("role")->string() == "test");
  const std::vector<joggle::Fn> echoes = env.find_fns("sample.echo");
  CHECK(echoes.size() == 2);
  CHECK(!env.find_fn("sample.echo"));
  CHECK(echoes[0].external() && echoes[1].external());
  CHECK(echoes[0].meta().empty() && echoes[1].meta().empty());
  CHECK(env.bound("sample.echo"));
  const std::vector<joggle::Attr> arguments{joggle::Attr(std::int64_t{41})};
  std::vector<joggle::Attr> returns;
  CHECK(env.call("sample.ping", arguments, returns));
  CHECK(returns.size() == 1);
  CHECK(returns[0].integer() == 42);
  const std::vector<joggle::Attr> bytes{
      joggle::Attr(joggle::Attr::Bytes{0, 127, 255})};
  CHECK(env.call("sample.echo", bytes, returns));
  CHECK(returns.size() == 1);
  CHECK(returns[0].bytes() && *returns[0].bytes() == *bytes[0].bytes());
  const std::vector<joggle::Attr> text{joggle::Attr("hello")};
  CHECK(env.call("sample.echo", text, returns));
  CHECK(returns.size() == 1 && returns[0].string() == "hello");
  joggle::Mod mod;
  CHECK(joggle::parse(env, source.str(), mod, argv[1]));
  CHECK(mod.verify(env));
  CHECK(mod.name() == "test.linear");
  CHECK(mod.uses() == std::vector<std::string>{"tensor"});
  CHECK(mod.fns().size() == 3);

  joggle::Fn matmul = mod.find_fn("matmul");
  CHECK(matmul);
  CHECK(!env.loaded("test.linear"));
  CHECK(env.resolve(matmul, "add_zero") == mod.find_fn("add_zero"));
  CHECK(env.resolve(matmul, "test.linear.add_zero") ==
        mod.find_fn("add_zero"));
  const joggle::Fn tensor_valid = env.resolve(matmul, "tensor.valid");
  CHECK(tensor_valid && tensor_valid.module() == "tensor");
  CHECK(matmul.generics().size() == 4);
  CHECK(matmul.generics()[0].type() == joggle::Ty("Ty"));
  CHECK(matmul.generics()[1].type() == joggle::Ty("int"));
  CHECK(matmul.params().size() == 2);
  CHECK(matmul.blks().size() == 3);
  CHECK(matmul.ops().size() > matmul.body().ops().size());

  const std::string canonical = joggle::print(mod);
  CHECK(canonical.find("for i in 0..M, j in 0..N") != std::string::npos);
  CHECK(canonical.find("for k in 0..K") != std::string::npos);
  CHECK(canonical.find("sum += a[i, k] * b[k, j]") != std::string::npos);
  CHECK(canonical.find("if flag") != std::string::npos);

  joggle::Mod reparsed;
  CHECK(joggle::parse(env, canonical, reparsed, "canonical.jog"));
  CHECK(reparsed.verify(env));
  CHECK(joggle::structurally_equal(mod, reparsed));

  joggle::Op removed;
  CHECK(fold_add_zero(mod, &removed));
  CHECK(!removed.valid());
  CHECK(mod.verify(env));
  const std::string folded = joggle::print(mod);
  CHECK(folded.find("let y = x + 0") == std::string::npos);
  CHECK(folded.find("return x") != std::string::npos);

  CHECK(env.load("opt"));
  CHECK(env.loaded("ir"));
  joggle::Mod scripted;
  CHECK(joggle::parse(env, source.str(), scripted, argv[1]));
  if (!joggle::run(env, "opt.fold_add_zero", scripted))
    return env.print_diags(stderr);
  const std::string scripted_text = joggle::print(scripted);
  CHECK(scripted_text.find("let y = x + 0") == std::string::npos);
  CHECK(scripted_text.find("return x") != std::string::npos);

  CHECK(env.load("script"));
  joggle::Mod reflected;
  CHECK(joggle::parse(env, source.str(), reflected, "matmul-reflection.jog"));
  CHECK(reflected.verify(env));
  joggle::Attr reflected_result;
  CHECK(joggle::query(env, "script.reflect_fn", reflected, reflected_result));
  CHECK(reflected_result.boolean() == true);
  joggle::Mod embedded_sequence;
  joggle::Mod source_sequence;
  CHECK(joggle::parse(env, source.str(), embedded_sequence, argv[1]));
  CHECK(joggle::parse(env, source.str(), source_sequence, argv[1]));
  constexpr std::string_view sequence[]{"opt.fold_add_zero",
                                        "script.mark_add"};
  joggle::Attr sequence_report;
  CHECK(joggle::run(env, sequence, embedded_sequence, sequence_report));
  CHECK(joggle::run(env, "script.prepare", source_sequence));
  CHECK(joggle::print(embedded_sequence) == joggle::print(source_sequence));
  const joggle::Attr::Dict* sequence_summary = sequence_report.dict();
  CHECK(sequence_summary &&
        sequence_summary->at("changed").boolean() == true);
  CHECK(sequence_summary->at("steps").list() &&
        sequence_summary->at("steps").list()->size() == 2);
  joggle::Mod failed_sequence;
  CHECK(joggle::parse(env, source.str(), failed_sequence, argv[1]));
  const std::string before_sequence = joggle::print(failed_sequence);
  const std::uint64_t before_sequence_revision = failed_sequence.revision();
  constexpr std::string_view invalid_sequence[]{"opt.fold_add_zero",
                                                "script.bad_entry"};
  CHECK(!joggle::run(env, invalid_sequence, failed_sequence));
  CHECK(joggle::print(failed_sequence) == before_sequence);
  CHECK(failed_sequence.revision() == before_sequence_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();

  joggle::Mod cleaned;
  constexpr std::string_view clean_source =
      "module clean\n"
      "fn work(x: i32) -> i32 {\n"
      "  let first: i32 = pure(x)\n"
      "  let same: i32 = pure(x)\n"
      "  let dead: i32 = pure(first)\n"
      "  return same\n}\n";
  CHECK(joggle::parse(env, clean_source, cleaned, "clean.jog"));
  CHECK(cleaned.verify(env));
  const std::vector<joggle::Attr> pure_query{joggle::Attr("pure")};
  joggle::Attr count;
  bool cached = true;
  CHECK(joggle::query(env, "opt.count", cleaned, count, pure_query, &cached));
  CHECK(!cached && count.integer() == 3);
  CHECK(joggle::query(env, "opt.count", cleaned, count, pure_query, &cached));
  CHECK(cached && count.integer() == 3);
  const std::string before_bad_query = joggle::print(cleaned);
  const std::uint64_t before_bad_query_revision = cleaned.revision();
  CHECK(!joggle::query(env, "script.mutating_query", cleaned, count,
                       pure_query, &cached));
  CHECK(joggle::print(cleaned) == before_bad_query);
  CHECK(cleaned.revision() == before_bad_query_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();
  CHECK(!joggle::query(env, "script.handle_query", cleaned, count));
  CHECK(joggle::print(cleaned) == before_bad_query);
  CHECK(cleaned.revision() == before_bad_query_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();
  const joggle::Attr::Dict attr_map{
      {"axis", joggle::Attr(std::int64_t{2})},
      {"mode", joggle::Attr("nearest")},
      {"values", joggle::Attr(joggle::Attr::List{
                     joggle::Attr(std::int64_t{2}),
                     joggle::Attr(std::int64_t{3}),
                     joggle::Attr(std::int64_t{5})})}};
  const std::vector<joggle::Attr> attr_args{joggle::Attr(attr_map)};
  CHECK(joggle::query(env, "script.attr_query", cleaned, count, attr_args));
  CHECK(count.integer() == 2);
  joggle::Attr mode;
  CHECK(joggle::query(env, "script.attr_mode", cleaned, mode, attr_args));
  CHECK(mode.string() == "nearest");
  joggle::Attr keys;
  CHECK(joggle::query(env, "script.attr_keys", cleaned, keys, attr_args));
  CHECK(keys.list() && keys.list()->size() == 3);
  CHECK(joggle::query(env, "script.attr_sum", cleaned, count, attr_args));
  CHECK(count.integer() == 10);
  const std::vector<joggle::Attr> missing_attr{
      joggle::Attr(joggle::Attr::Dict{})};
  CHECK(joggle::query(env, "script.attr_query", cleaned, count,
                       missing_attr));
  CHECK(count.integer() == -1);
  constexpr std::string_view deep_dead_source =
      "module deep.dead\n"
      "fn work(x: i32) -> i32 {\n"
      "  let a: i32 = pure(x)\n"
      "  let b: i32 = pure(a)\n"
      "  let c: i32 = pure(b)\n"
      "  let d: i32 = pure(c)\n"
      "  return x\n"
      "}\n";
  joggle::Mod deep_dead;
  CHECK(joggle::parse(env, deep_dead_source, deep_dead, "deep-dead.jog"));
  CHECK(deep_dead.verify(env));
  const std::string deep_before = joggle::print(deep_dead);
  const std::uint64_t deep_revision = deep_dead.revision();
  CHECK(!joggle::run(env, "script.clean_tight", deep_dead));
  CHECK(joggle::print(deep_dead) == deep_before);
  CHECK(deep_dead.revision() == deep_revision);
  CHECK(!env.diags().empty());
  CHECK(env.diags().front().message.find("did not converge") !=
        std::string::npos);
  env.clear_diags();
  CHECK(joggle::run(env, "script.clean_pure", deep_dead));
  CHECK(deep_dead.verify(env));
  CHECK(joggle::query(env, "opt.count", deep_dead, count, pure_query));
  CHECK(count.integer() == 0);
  joggle::Attr clean_report;
  CHECK(joggle::run(env, "script.clean_pure", cleaned, clean_report));
  CHECK(cleaned.verify(env));
  const joggle::Attr::Dict* clean_summary = clean_report.dict();
  CHECK(clean_summary && clean_summary->at("ok").boolean() == true);
  CHECK(clean_summary->at("fn").string() == "script.clean_pure");
  CHECK(clean_summary->at("changed").boolean() == true);
  CHECK(clean_summary->at("reported").boolean() == true);
  CHECK(clean_summary->at("edits").integer() &&
        *clean_summary->at("edits").integer() > 0);
  CHECK(clean_summary->at("steps").list() &&
        !clean_summary->at("steps").list()->empty());
  std::size_t pure_calls = 0;
  for (joggle::Op op : cleaned.ops())
    pure_calls += op.callee() == "pure" ? 1 : 0;
  CHECK(pure_calls == 1);
  CHECK(joggle::query(env, "opt.count", cleaned, count, pure_query, &cached));
  CHECK(!cached && count.integer() == 1);
  CHECK(joggle::query(env, "opt.count", cleaned, count, pure_query, &cached));
  CHECK(cached && count.integer() == 1);
  const std::vector<joggle::Attr> join_query{joggle::Attr("join")};
  CHECK(joggle::query(env, "opt.count", cleaned, count, join_query, &cached));
  CHECK(!cached && count.integer() == 0);
  const std::uint64_t clean_revision = cleaned.revision();
  joggle::Attr stable_report;
  CHECK(joggle::run(env, "script.clean_pure", cleaned, stable_report));
  CHECK(cleaned.revision() == clean_revision);
  const joggle::Attr::Dict* stable_summary = stable_report.dict();
  CHECK(stable_summary && stable_summary->at("changed").boolean() == false);
  CHECK(stable_summary->at("reported").boolean() == false);
  CHECK(stable_summary->at("edits").integer() == 0);
  CHECK(joggle::run(env, "script.selected_entry", cleaned));
  CHECK(!joggle::run(env, "script.bad_entry", cleaned));
  CHECK(!env.diags().empty());
  env.clear_diags();

  joggle::Mod distinct_meta;
  constexpr std::string_view distinct_meta_source =
      "module distinct\n"
      "fn work(x: i32) -> i32 {\n"
      "  [variant: 0]\n"
      "  let first: i32 = pure(x)\n"
      "  [variant: 1]\n"
      "  let second: i32 = pure(x)\n"
      "  return join(first, second)\n}\n";
  CHECK(joggle::parse(env, distinct_meta_source, distinct_meta,
                      "distinct-meta.jog"));
  CHECK(distinct_meta.verify(env));
  CHECK(joggle::run(env, "script.clean_pure", distinct_meta));
  pure_calls = 0;
  for (joggle::Op op : distinct_meta.ops())
    pure_calls += op.callee() == "pure" ? 1 : 0;
  CHECK(pure_calls == 2);

  joggle::Mod overload_execution;
  CHECK(joggle::parse(env, source.str(), overload_execution, argv[1]));
  CHECK(joggle::run(env, "script.overload_probe", overload_execution));
  CHECK(joggle::run(env, "script.short_circuit_probe", overload_execution));
  CHECK(joggle::run(env, "script.short_circuit_edit", overload_execution));
  const std::vector<std::string> short_uses = overload_execution.uses();
  CHECK(std::find(short_uses.begin(), short_uses.end(), "skipped") ==
        short_uses.end());
  CHECK(joggle::run(env, "script.generic_probe", overload_execution));
  CHECK(joggle::run(env, "script.multi_probe", overload_execution));
  CHECK(joggle::run(env, "script.compound_probe", overload_execution));
  CHECK(joggle::run(env, "script.collection_update_probe",
                    overload_execution));
  CHECK(joggle::run(env, "script.numel_probe", overload_execution));
  CHECK(joggle::run(env, "script.make_pair", overload_execution));
  CHECK(joggle::print(overload_execution)
            .find("let left: i32, right: i32 = test.pair(x, 0)") !=
        std::string::npos);
  joggle::Mod marked;
  CHECK(joggle::parse(env, source.str(), marked, argv[1]));
  CHECK(joggle::run(env, "script.mark_add", marked));
  const joggle::Op marked_add = marked.find_fn("add_zero").body().ops()[1];
  CHECK(marked_add.meta("place") &&
        marked_add.meta("place")->string() == "edge");
  CHECK(marked_add.meta("tile") && marked_add.meta("tile")->list() &&
        marked_add.meta("tile")->list()->size() == 2);
  joggle::Mod scripted_selective;
  CHECK(joggle::parse(env, selective_source, scripted_selective,
                      "scripted-selective.jog"));
  CHECK(joggle::run(env, "script.replace_first_use", scripted_selective));
  CHECK(joggle::print(scripted_selective).find("let left = x + 1") !=
        std::string::npos);
  CHECK(joggle::print(scripted_selective)
            .find("let right = shared + 2") != std::string::npos);
  joggle::Mod scripted_loop;
  CHECK(joggle::parse(env, loop_source, scripted_loop, "scripted-loop.jog"));
  CHECK(joggle::run(env, "script.clone_loop", scripted_loop));
  CHECK(scripted_loop.verify(env));
  CHECK(joggle::print(scripted_loop) == joggle::print(cloned_loop));
  joggle::Mod returned_constant;
  CHECK(joggle::parse(env,
                      "module returned\n"
                      "fn value(x: i32) -> i32 { return x }\n",
                      returned_constant, "returned.jog"));
  CHECK(joggle::run(env, "script.return_three", returned_constant));
  CHECK(joggle::print(returned_constant).find("let three: i32 = 3") !=
        std::string::npos);
  CHECK(joggle::print(returned_constant).find("return three") !=
        std::string::npos);
  joggle::Mod scripted_schedule;
  CHECK(joggle::parse(env, schedule_source, scripted_schedule,
                      "scripted-schedule.jog"));
  CHECK(joggle::run(env, "script.move_second_first", scripted_schedule));
  CHECK(joggle::print(scripted_schedule) == scheduled_text);
  joggle::Mod scripted_control;
  CHECK(joggle::parse(env, control_seed, scripted_control,
                      "scripted-control.jog"));
  CHECK(joggle::run(env, "script.build_control", scripted_control));
  CHECK(scripted_control.verify(env));
  CHECK(joggle::print(scripted_control) == built_control_text);
  joggle::Mod control_rollback;
  CHECK(joggle::parse(env, built_control_text, control_rollback,
                      "control-rollback.jog"));
  const std::uint64_t control_revision = control_rollback.revision();
  CHECK(!joggle::run(env, "script.fail_after_control_rename",
                     control_rollback));
  CHECK(joggle::print(control_rollback) == built_control_text);
  CHECK(control_rollback.revision() == control_revision);
  control_rollback.clear_diags();
  CHECK(joggle::run(env, "script.rename_control", scripted_control));
  CHECK(scripted_control.verify(env));
  CHECK(joggle::print(scripted_control) == renamed_control_text);
  joggle::Mod rolled_back;
  CHECK(joggle::parse(env, source.str(), rolled_back, argv[1]));
  const std::string before_failure = joggle::print(rolled_back);
  const std::uint64_t before_failure_revision = rolled_back.revision();
  CHECK(!joggle::run(env, "script.fail_after_edit", rolled_back));
  CHECK(joggle::print(rolled_back) == before_failure);
  CHECK(rolled_back.revision() == before_failure_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();
  joggle::Mod invalid_run;
  CHECK(joggle::parse(env,
                      "module invalid.run\n"
                      "fn main(x: i32) -> i32 {\n"
                      "  let y: i32 = pure(x)\n"
                      "  return y\n"
                      "}\n",
                      invalid_run, "invalid-run.jog"));
  CHECK(invalid_run.verify(env));
  const std::string before_invalid_run = joggle::print(invalid_run);
  const std::uint64_t before_invalid_run_revision = invalid_run.revision();
  CHECK(!joggle::run(env, "script.invalidate_call", invalid_run));
  CHECK(joggle::print(invalid_run) == before_invalid_run);
  CHECK(invalid_run.revision() == before_invalid_run_revision);
  bool kept_verify_detail = false;
  for (const joggle::Diag& diag : env.diags())
    kept_verify_detail = kept_verify_detail ||
                         diag.message.find("base.len") != std::string::npos;
  CHECK(kept_verify_detail);

  joggle::Mod immutable;
  CHECK(!joggle::parse(env,
                       "module bad\nfn f(x: i32) -> i32 {\n"
                       "  let y = x\n  y = 1\n  return y\n}\n",
                       immutable, "immutable.jog"));
  CHECK(!immutable.diags().empty());
  CHECK(immutable.diags().front().loc.line == 4);

  joggle::Mod invalid_number;
  CHECK(!joggle::parse(
      env, "module bad\nfn f() -> int { return 999999999999999999999999 }\n",
      invalid_number, "number.jog"));
  CHECK(!invalid_number.diags().empty());

  joggle::Mod invalid_type;
  CHECK(!joggle::parse(
      env, "module bad\nfn f(x: tensor<i32,>) -> i32 { return 0 }\n",
      invalid_type, "type.jog"));
  CHECK(!invalid_type.diags().empty());

  joggle::Mod attrs;
  constexpr std::string_view attr_source =
      "module attrs\n"
      "[entry]\n"
      "[policy: {name: \"roundtrip\", levels: [1, 2]}]\n"
      "fn payload() -> dict {\n"
      "  return {axis: 1, epsilon: 9.9999997473787516e-06, "
      "pads: [0, -1], raw: hex\"007fff\", "
      "words: [\"-\", \"[\", \"]\"]}\n}\n";
  CHECK(joggle::parse(env, attr_source, attrs, "attrs.jog"));
  CHECK(attrs.verify(env));
  const joggle::Val payload =
      attrs.find_fn("payload").body().ops().back().args()[0];
  const joggle::Attr payload_attr = payload.constant();
  const joggle::Attr::Dict* dict = payload_attr.dict();
  CHECK(dict && dict->size() == 5);
  CHECK(dict->at("axis").integer() == 1);
  CHECK(dict->at("epsilon").real() == 9.9999997473787516e-06);
  CHECK(dict->at("pads").list() && dict->at("pads").list()->size() == 2);
  CHECK(dict->at("raw").bytes() && dict->at("raw").bytes()->size() == 3);
  CHECK(dict->at("words").list() && dict->at("words").list()->size() == 3);
  const joggle::Fn payload_fn = attrs.find_fn("payload");
  CHECK(payload_fn.meta().size() == 2);
  CHECK(payload_fn.meta("entry") &&
        payload_fn.meta("entry")->boolean() == true);
  CHECK(payload_fn.meta("policy") && payload_fn.meta("policy")->dict());
  joggle::Mod attrs_roundtrip;
  CHECK(joggle::parse(env, joggle::print(attrs), attrs_roundtrip,
                      "attrs-roundtrip.jog"));
  CHECK(joggle::structurally_equal(attrs, attrs_roundtrip));

  joggle::Mod value_attrs;
  constexpr std::string_view value_attr_source =
      "module value_attrs\n"
      "use base\n"
      "fn carry<[role: \"extent\"] N: int>(\n"
      "  [range: {min: 0}] seed: i32\n"
      ") -> i32 {\n"
      "  [place: \"edge\"]\n"
      "  var [format: \"q8\"] value: i32 = seed\n"
      "  for i in 0..N {\n"
      "    value += 1\n"
      "  }\n"
      "  let [range: {min: 0, max: 255}] output: i32 = value\n"
      "  return output\n"
      "}\n"
      "fn inner(x: i32) -> i32 {\n"
      "  let [space: \"body\"] y: i32 = x\n"
      "  return y\n"
      "}\n"
      "fn conflict(x: i32) -> i32 {\n"
      "  let [space: \"call\"] y: i32 = inner(x)\n"
      "  return y\n"
      "}\n";
  CHECK(joggle::parse(env, value_attr_source, value_attrs,
                      "value-attrs.jog"));
  CHECK(value_attrs.verify(env));
  const joggle::Fn carry = value_attrs.find_fn("carry");
  CHECK(carry && carry.generics().size() == 1 && carry.params().size() == 1);
  CHECK(carry.generics()[0].meta("role") &&
        carry.generics()[0].meta("role")->string() == "extent");
  CHECK(carry.params()[0].meta("range") &&
        carry.params()[0].meta("range")->dict());
  joggle::Op value_loop;
  joggle::Val value;
  joggle::Val output;
  for (joggle::Op op : carry.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      value_loop = op;
    for (joggle::Val out : op.outs()) {
      if (out.name() == "value" && out.meta("format"))
        value = out;
      if (out.name() == "output")
        output = out;
    }
  }
  CHECK(value_loop && value && output);
  CHECK(output.meta("range") && output.meta("range")->dict());
  const joggle::Op conflict_call =
      value_attrs.find_fn("conflict").body().ops().front();
  const std::string before_conflict = joggle::print(value_attrs);
  CHECK(!env.expand(value_attrs, conflict_call,
                    env.resolve(value_attrs, conflict_call)));
  CHECK(joggle::print(value_attrs) == before_conflict);
  value_attrs.clear_diags();
  CHECK(value_attrs.set(value_loop.blks()[0].args()[1], "bank",
                        joggle::Attr(std::int64_t{2})));
  for (joggle::Val member : value_loop.blks()[0].args())
    if (member.name() == "value")
      CHECK(member.meta("bank") && member.meta("bank")->integer() == 2);
  CHECK(value_attrs.unset(value, "bank"));
  CHECK(joggle::run(env, "script.mark_first_param", value_attrs));
  CHECK(carry.params()[0].meta("layout") &&
        carry.params()[0].meta("layout")->string() == "packed");
  joggle::Mod value_attrs_roundtrip;
  const std::string value_attrs_text = joggle::print(value_attrs);
  if (!joggle::parse(env, value_attrs_text, value_attrs_roundtrip,
                     "value-attrs-roundtrip.jog")) {
    std::fwrite(value_attrs_text.data(), 1, value_attrs_text.size(), stderr);
    value_attrs_roundtrip.print_diags(stderr);
    return 1;
  }
  CHECK(value_attrs_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(value_attrs, value_attrs_roundtrip));

  joggle::Mod tagged;
  constexpr std::string_view tagged_source =
      "module tagged\n"
      "[entry, rename: \"custom.add\"]\n"
      "fn add(x: i32, y: i32) -> i32 { return x + y }\n"
      "fn plain(x: i32, y: i32) -> i32 { return x + y }\n";
  CHECK(joggle::parse(env, tagged_source, tagged, "tagged.jog"));
  CHECK(tagged.verify(env));
  CHECK(joggle::run(env, "script.rebuild_tagged", tagged));
  const joggle::Op renamed =
      tagged.find_fn("add").body().ops().back().args().front().def();
  const joggle::Op plain =
      tagged.find_fn("plain").body().ops().back().args().front().def();
  CHECK(renamed.callee() == "custom.add");
  CHECK(plain.callee() == "operator +");

  joggle::Mod unsafe_fusion;
  constexpr std::string_view unsafe_source =
      "module unsafe\n"
      "fn main(x: i32) -> i32 {\n"
      "  let first = test.first(x)\n"
      "  let unrelated = test.unrelated(x)\n"
      "  let last = test.last(first)\n"
      "  return last\n}\n";
  CHECK(joggle::parse(env, unsafe_source, unsafe_fusion, "unsafe.jog"));
  CHECK(unsafe_fusion.verify(env));
  const std::string unsafe_before = joggle::print(unsafe_fusion);
  std::vector<joggle::Op> unsafe_group;
  for (joggle::Op op : unsafe_fusion.find_fn("main").body().ops()) {
    if (op.callee() == "test.first" || op.callee() == "test.last")
      unsafe_group.push_back(op);
  }
  CHECK(unsafe_group.size() == 2);
  CHECK(!unsafe_fusion.fuse(env, unsafe_group, "test.fused"));
  CHECK(joggle::print(unsafe_fusion) == unsafe_before);
  CHECK(!unsafe_fusion.diags().empty());

  joggle::Mod typed_fusion;
  constexpr std::string_view typed_fusion_source =
      "module typed_fusion\n"
      "fn incompatible(x: i32) -> f32;\n"
      "fn compatible(x: i32) -> i32;\n"
      "fn main(x: i32) -> i32 {\n"
      "  let first: i32 = experiment.first(x)\n"
      "  let last: i32 = experiment.last(first)\n"
      "  return last\n}\n";
  CHECK(joggle::parse(env, typed_fusion_source, typed_fusion,
                      "typed-fusion.jog"));
  CHECK(typed_fusion.verify(env));
  std::vector<joggle::Op> typed_group;
  for (joggle::Op op : typed_fusion.find_fn("main").body().ops())
    if (op.kind() == joggle::Op::Kind::call)
      typed_group.push_back(op);
  CHECK(typed_group.size() == 2);
  const std::string typed_before = joggle::print(typed_fusion);
  const std::uint64_t typed_revision = typed_fusion.revision();
  CHECK(!typed_fusion.fuse(env, typed_group, "incompatible"));
  CHECK(joggle::print(typed_fusion) == typed_before);
  CHECK(typed_fusion.revision() == typed_revision);

  typed_group.clear();
  for (joggle::Op op : typed_fusion.find_fn("main").body().ops())
    if (op.kind() == joggle::Op::Kind::call)
      typed_group.push_back(op);
  CHECK(typed_fusion.fuse(env, typed_group, "compatible"));
  CHECK(typed_fusion.verify(env));
  CHECK(joggle::print(typed_fusion).find("compatible(x)") !=
        std::string::npos);

  joggle::Mod duplicate_meta;
  CHECK(!joggle::parse(env,
                       "module bad\n[a, a: 1]\n"
                       "fn f() -> int { return 0 }\n",
                       duplicate_meta, "duplicate-meta.jog"));
  CHECK(!duplicate_meta.diags().empty());

  joggle::Mod missing_return;
  CHECK(joggle::parse(env, "module bad\nfn f(x: i32) -> i32 { x + 1 }\n",
                      missing_return, "return.jog"));
  CHECK(!missing_return.verify(env));
  CHECK(!missing_return.diags().empty());

  env.clear_diags();
  const std::vector<joggle::Attr> wrong{joggle::Attr("not an integer")};
  CHECK(!env.call("sample.ping", wrong, returns));
  CHECK(!env.diags().empty());
  return 0;
}
