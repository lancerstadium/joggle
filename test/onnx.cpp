#include "joggle/joggle.h"
#include "onnx.pb.h"

#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
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
  split->add_input("x");
  split->add_output("left");
  split->add_output("right");
  std::string bytes;
  if (!model.SerializeToString(&bytes))
    return {};
  return {bytes.begin(), bytes.end()};
}

std::size_t count_calls(const joggle::Mod& mod, std::string_view callee) {
  std::size_t count = 0;
  for (joggle::Fn fn : mod.fns())
    for (joggle::Blk block : fn.blks())
      for (joggle::Op op : block.ops())
        count += op.kind() == joggle::Op::Kind::call && op.callee() == callee;
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
  for (joggle::Blk block : main.blks()) {
    for (joggle::Op op : block.ops()) {
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
