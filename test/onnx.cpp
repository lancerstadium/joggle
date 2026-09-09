#include "joggle/joggle.h"
#include "onnx.pb.h"

#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

void tensor_type(jogonnx::ValueInfoProto* value, std::string name,
                 std::initializer_list<std::int64_t> shape) {
  value->set_name(std::move(name));
  auto* tensor = value->mutable_type()->mutable_tensor_type();
  tensor->set_elem_type(1);
  for (const std::int64_t extent : shape)
    tensor->mutable_shape()->add_dim()->set_dim_value(extent);
}

joggle::Attr::Bytes multi_output_model() {
  jogonnx::ModelProto model;
  model.set_ir_version(8);
  auto* opset = model.add_opset_import();
  opset->set_domain("");
  opset->set_version(13);
  auto* graph = model.mutable_graph();
  graph->set_name("multi");
  tensor_type(graph->add_input(), "x", {2});
  tensor_type(graph->add_value_info(), "left", {1});
  tensor_type(graph->add_value_info(), "right", {1});
  tensor_type(graph->add_output(), "left", {1});
  tensor_type(graph->add_output(), "right", {1});
  auto* split = graph->add_node();
  split->set_op_type("Split");
  split->set_name("split-node");
  split->add_input("x");
  split->add_output("left");
  split->add_output("right");
  auto* axis = split->add_attribute();
  axis->set_name("axis");
  axis->set_type(jogonnx::AttributeProto::INT);
  axis->set_i(0);
  std::string bytes;
  if (!model.SerializeToString(&bytes))
    return {};
  return {bytes.begin(), bytes.end()};
}

std::size_t count_calls(const joggle::Mod& mod, std::string_view callee) {
  std::size_t count = 0;
  for (joggle::Fn fn : mod.fns())
    for (joggle::Blk blk : fn.blks())
      for (joggle::Op op : blk.ops())
        count += op.kind() == joggle::Op::Kind::call && op.callee() == callee;
  return count;
}

std::size_t count_unknown_node_outputs(const joggle::Mod& mod) {
  std::size_t count = 0;
  for (joggle::Op op : mod.ops()) {
    if (!op.callee().starts_with("onnx.") ||
        op.callee() == "onnx.model" || op.callee() == "onnx.tensor")
      continue;
    for (joggle::Val output : op.outs())
      count += output.type().text() == "_" ? 1 : 0;
  }
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 3);
  std::ifstream input(argv[1], std::ios::binary);
  CHECK(input);
  const std::vector<unsigned char> raw{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
  joggle::Attr::Bytes bytes(raw.begin(), raw.end());

  joggle::Env env;
  env.path(argv[2]);
  CHECK(env.load("onnx"));
  const std::vector<joggle::Attr> args{joggle::Attr(std::move(bytes))};
  std::vector<joggle::Attr> returns;
  CHECK(env.call("onnx.read", args, returns));
  CHECK(returns.size() == 1 && returns.front().string());

  joggle::Mod model;
  CHECK(joggle::parse(env, *returns.front().string(), model, argv[1]));
  CHECK(model.verify(env));
  const joggle::Fn main = model.find_fn("main");
  CHECK(main && main.params().size() == 1);

  std::size_t tensors = 0;
  std::size_t nodes = 0;
  std::size_t weight_bytes = 0;
  for (joggle::Blk blk : main.blks()) {
    for (joggle::Op op : blk.ops()) {
      if (op.kind() != joggle::Op::Kind::call)
        continue;
      if (op.callee() == "onnx.tensor") {
        ++tensors;
        const std::vector<joggle::Val> values = op.args();
        CHECK(values.size() == 3);
        const joggle::Attr data = values[2].constant();
        CHECK(data.bytes());
        weight_bytes += data.bytes()->size();
      } else if (op.callee().starts_with("onnx.") &&
                 op.callee() != "onnx.model")
        ++nodes;
    }
  }
  CHECK(tensors == 267);
  CHECK(nodes == 155);
  CHECK(weight_bytes == 14156560);

  const std::string canonical = joggle::print(model);
  joggle::Mod roundtrip;
  CHECK(joggle::parse(env, canonical, roundtrip, "mobilenet-roundtrip.jog"));
  CHECK(roundtrip.verify(env));
  CHECK(joggle::structurally_equal(model, roundtrip));

  for (joggle::Op op : model.ops())
    if (op.callee() == "onnx.Relu") {
      CHECK(op.args().size() == 1);
      CHECK(op.meta("onnx") && op.meta("onnx")->dict());
    }

  CHECK(env.load("onnx.nn"));
  joggle::Mod semantic;
  CHECK(joggle::parse(env, canonical, semantic, "mobilenet-semantic.jog"));
  CHECK(count_unknown_node_outputs(semantic) > 0);
  CHECK(joggle::run(env, "onnx.nn.infer", semantic));
  CHECK(semantic.verify(env));
  CHECK(count_unknown_node_outputs(semantic) == 0);
  bool checked_first_conv = false;
  bool checked_pool = false;
  for (joggle::Op op : semantic.ops()) {
    if (op.callee() == "onnx.Conv" && !checked_first_conv) {
      CHECK(op.outs().front().type() ==
            joggle::Ty("tensor<f32, [1, 32, 112, 112]>"));
      checked_first_conv = true;
    }
    if (op.callee() == "onnx.GlobalAveragePool") {
      CHECK(op.outs().front().type() ==
            joggle::Ty("tensor<f32, [1, 1280, 1, 1]>"));
      checked_pool = true;
    }
  }
  CHECK(checked_first_conv && checked_pool);
  const std::string typed_text = joggle::print(semantic);
  CHECK(joggle::run(env, "onnx.nn.infer", semantic));
  CHECK(joggle::print(semantic) == typed_text);
  const std::size_t network_convs = count_calls(semantic, "onnx.Conv");
  const std::size_t network_norms =
      count_calls(semantic, "onnx.BatchNormalization");
  const std::size_t network_relus = count_calls(semantic, "onnx.Relu");
  const std::size_t network_adds = count_calls(semantic, "onnx.Add");
  const std::size_t network_pools =
      count_calls(semantic, "onnx.GlobalAveragePool");
  const std::size_t network_reshapes = count_calls(semantic, "onnx.Reshape");
  CHECK(network_convs > 0 && network_norms > 0 && network_relus > 0 &&
        network_adds > 0 && network_pools > 0 && network_reshapes > 0);
  CHECK(joggle::run(env, "onnx.nn.convert", semantic));
  CHECK(semantic.verify(env));
  CHECK(count_calls(semantic, "onnx.Conv") == 0);
  CHECK(count_calls(semantic, "onnx.BatchNormalization") == 0);
  CHECK(count_calls(semantic, "onnx.Relu") == 0);
  CHECK(count_calls(semantic, "onnx.Add") == 0);
  CHECK(count_calls(semantic, "onnx.GlobalAveragePool") == 0);
  CHECK(count_calls(semantic, "onnx.Reshape") == 0);
  CHECK(count_calls(semantic, "nn.conv2d") == network_convs);
  CHECK(count_calls(semantic, "nn.batch_norm") == network_norms);
  CHECK(count_calls(semantic, "nn.relu") == network_relus);
  CHECK(count_calls(semantic, "nn.add") == network_adds);
  CHECK(count_calls(semantic, "nn.global_avg_pool2d") == network_pools);
  CHECK(count_calls(semantic, "tensor.reshape") == network_reshapes);
  std::size_t remaining_nodes = 0;
  for (joggle::Op op : semantic.ops())
    if (op.callee().starts_with("onnx.") &&
        op.callee() != "onnx.model" && op.callee() != "onnx.tensor")
      ++remaining_nodes;
  CHECK(remaining_nodes == 0);
  const std::string semantic_text = joggle::print(semantic);
  CHECK(joggle::run(env, "onnx.nn.convert", semantic));
  CHECK(joggle::print(semantic) == semantic_text);
  joggle::Mod semantic_roundtrip;
  CHECK(joggle::parse(env, semantic_text, semantic_roundtrip,
                      "mobilenet-semantic-roundtrip.jog"));
  CHECK(semantic_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(semantic, semantic_roundtrip));

  const std::set<std::string, std::less<>> expandable{
      "nn.conv2d", "nn.batch_norm", "nn.relu", "nn.add",
      "nn.global_avg_pool2d", "tensor.reshape"};
  std::size_t expanded = 0;
  for (joggle::Op op : semantic_roundtrip.ops()) {
    if (!expandable.contains(op.callee()))
      continue;
    CHECK(!op.meta("onnx"));
    const joggle::Fn callee = env.resolve(semantic_roundtrip, op);
    CHECK(callee && semantic_roundtrip.expand(op, callee));
    ++expanded;
  }
  CHECK(expanded == nodes);
  CHECK(semantic_roundtrip.verify(env));
  for (joggle::Op op : semantic_roundtrip.ops()) {
    CHECK(op.callee() != "nn.conv2d");
    CHECK(op.callee() != "nn.batch_norm");
    CHECK(op.callee() != "nn.relu");
    CHECK(op.callee() != "nn.global_avg_pool2d");
    CHECK(op.callee() != "tensor.reshape");
  }
  const std::string expanded_text = joggle::print(semantic_roundtrip);
  joggle::Mod expanded_roundtrip;
  CHECK(joggle::parse(env, expanded_text, expanded_roundtrip,
                      "mobilenet-expanded-roundtrip.jog"));
  CHECK(expanded_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(semantic_roundtrip, expanded_roundtrip));

  const std::vector<joggle::Attr> multi_args{
      joggle::Attr(multi_output_model())};
  CHECK(env.call("onnx.read", multi_args, returns));
  CHECK(returns.size() == 1 && returns.front().string());
  joggle::Mod multi;
  CHECK(joggle::parse(env, *returns.front().string(), multi,
                      "multi-output.onnx"));
  CHECK(multi.verify(env));
  const joggle::Fn multi_main = multi.find_fn("main");
  CHECK(multi_main && multi_main.returns().size() == 2);
  joggle::Op split;
  for (joggle::Op op : multi_main.ops())
    if (op.callee() == "onnx.Split")
      split = op;
  CHECK(split && split.outs().size() == 2);
  CHECK(split.args().size() == 1);
  const joggle::Attr* onnx = split.meta("onnx");
  CHECK(onnx && onnx->dict());
  CHECK(onnx->dict()->at("$node").string() == "split-node");
  CHECK(onnx->dict()->at("axis").integer() == 0);
  CHECK(split.outs()[0].type() == joggle::Ty("tensor<f32, [1]>"));
  CHECK(split.outs()[1].type() == joggle::Ty("tensor<f32, [1]>"));
  const std::string multi_text = joggle::print(multi);
  joggle::Mod multi_roundtrip;
  CHECK(joggle::parse(env, multi_text, multi_roundtrip,
                      "multi-output-roundtrip.jog"));
  CHECK(multi_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(multi, multi_roundtrip));

  const std::size_t convs = count_calls(model, "onnx.Conv");
  const std::size_t norms = count_calls(model, "onnx.BatchNormalization");
  const std::size_t relus = count_calls(model, "onnx.Relu");
  CHECK(convs > 0 && norms > 0 && relus > 0);
  CHECK(env.load("script"));
  CHECK(joggle::run(env, "script.fuse_onnx", model));
  const std::size_t fused = count_calls(model, "test.conv_bn_relu");
  CHECK(fused == 36);
  CHECK(count_calls(model, "onnx.Conv") == convs - fused);
  CHECK(count_calls(model, "onnx.BatchNormalization") == norms - fused);
  CHECK(count_calls(model, "onnx.Relu") == relus - fused);
  const std::string optimized = joggle::print(model);
  joggle::Mod optimized_roundtrip;
  CHECK(joggle::parse(env, optimized, optimized_roundtrip,
                      "mobilenet-optimized.jog"));
  CHECK(optimized_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(model, optimized_roundtrip));
  CHECK(joggle::run(env, "script.fuse_onnx", model));
  CHECK(joggle::print(model) == optimized);
  return 0;
}
