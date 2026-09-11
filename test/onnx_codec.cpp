#include "joggle/joggle.h"
#include "onnx.pb.h"

#include <cstdio>
#include <initializer_list>
#include <string>
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

namespace {

void tensor_type(jogonnx::ValueInfoProto* value, std::string name,
                 std::initializer_list<std::int64_t> shape, int element = 1) {
  value->set_name(std::move(name));
  auto* tensor = value->mutable_type()->mutable_tensor_type();
  tensor->set_elem_type(element);
  for (const std::int64_t extent : shape)
    tensor->mutable_shape()->add_dim()->set_dim_value(extent);
}

void dynamic_type(jogonnx::ValueInfoProto* value, std::string name) {
  value->set_name(std::move(name));
  auto* tensor = value->mutable_type()->mutable_tensor_type();
  tensor->set_elem_type(1);
  tensor->mutable_shape()->add_dim()->set_dim_param("batch-size");
  tensor->mutable_shape()->add_dim()->set_dim_param("batch_size");
  tensor->mutable_shape()->add_dim();
}

std::string dynamic_model() {
  jogonnx::ModelProto model;
  model.set_ir_version(9);
  auto* opset = model.add_opset_import();
  opset->set_version(21);
  auto* graph = model.mutable_graph();
  dynamic_type(graph->add_input(), "batch_size");
  dynamic_type(graph->add_output(), "output");
  auto* identity = graph->add_node();
  identity->set_op_type("Identity");
  identity->add_input("batch_size");
  identity->add_output("output");
  std::string bytes;
  return model.SerializeToString(&bytes) ? bytes : std::string{};
}

std::string schema_model() {
  jogonnx::ModelProto model;
  model.set_ir_version(8);
  auto* opset = model.add_opset_import();
  opset->set_domain("");
  opset->set_version(9);
  auto* graph = model.mutable_graph();
  graph->set_name("schema-forms");
  tensor_type(graph->add_input(), "x", {2, 3});
  graph->add_output()->set_name("y");

  auto* constant = graph->add_node();
  constant->set_op_type("Constant");
  constant->add_output("shape");
  auto* value = constant->add_attribute();
  value->set_name("value");
  value->set_type(jogonnx::AttributeProto::TENSOR);
  auto* tensor = value->mutable_t();
  tensor->set_data_type(7);
  tensor->add_dims(2);
  tensor->add_int64_data(3);
  tensor->add_int64_data(2);

  auto* reshape = graph->add_node();
  reshape->set_op_type("Reshape");
  reshape->add_input("x");
  reshape->add_input("shape");
  reshape->add_output("reshaped");

  auto* slice = graph->add_node();
  slice->set_op_type("Slice");
  slice->add_input("reshaped");
  slice->add_output("y");
  auto ints = [slice](std::string name,
                      std::initializer_list<std::int64_t> values) {
    auto* attr = slice->add_attribute();
    attr->set_name(std::move(name));
    attr->set_type(jogonnx::AttributeProto::INTS);
    for (const std::int64_t value : values)
      attr->add_ints(value);
  };
  ints("starts", {1});
  ints("ends", {3});
  ints("axes", {0});

  std::string bytes;
  return model.SerializeToString(&bytes) ? bytes : std::string{};
}

std::size_t calls(const joggle::Mod& mod, std::string_view callee) {
  std::size_t count = 0;
  for (joggle::Op op : mod.ops())
    count += op.callee() == callee ? 1 : 0;
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 2);
  joggle::Env env;
  env.path(argv[1]);
  CHECK(env.load("onnx"));

  const std::string bytes = dynamic_model();
  CHECK(!bytes.empty());
  const auto* first = reinterpret_cast<const std::uint8_t*>(bytes.data());
  const joggle::Attr::Bytes payload(first, first + bytes.size());
  const std::vector<joggle::Attr> args{joggle::Attr(payload)};
  std::vector<joggle::Attr> returns;
  CHECK(env.call("onnx.read", args, returns));
  CHECK(returns.size() == 1 && returns[0].string());

  const std::string source(*returns[0].string());
  CHECK(source.find(
            "fn main<batch_size: int, batch_size_1: int>(") !=
        std::string::npos);
  CHECK(source.find(
            "batch_size_2: tensor<f32, [batch_size, batch_size_1, _]>") !=
        std::string::npos);

  joggle::Mod mod;
  CHECK(joggle::parse(env, source, mod, "dynamic.onnx"));
  CHECK(mod.verify(env));
  const joggle::Fn main = mod.find_fn("main");
  CHECK(main && main.generics().size() == 2 && main.params().size() == 1);
  CHECK(main.generics()[0].name() == "batch_size");
  CHECK(main.generics()[0].type() == joggle::Ty("int"));
  CHECK(main.generics()[1].name() == "batch_size_1");
  CHECK(main.generics()[1].type() == joggle::Ty("int"));
  CHECK(main.params()[0].name() == "batch_size_2");
  CHECK(main.params()[0].meta("onnx") &&
        main.params()[0].meta("onnx")->dict() &&
        main.params()[0].meta("onnx")->dict()->at("name").string() ==
            "batch_size");
  const joggle::Ty dynamic(
      "tensor<f32, [batch_size, batch_size_1, _]>");
  CHECK(main.params()[0].type() == dynamic);
  CHECK(main.returns().size() == 1 && main.returns()[0] == dynamic);

  joggle::Mod roundtrip;
  CHECK(joggle::parse(env, joggle::print(mod), roundtrip,
                      "dynamic-roundtrip.jog"));
  CHECK(roundtrip.verify(env));
  CHECK(joggle::structurally_equal(mod, roundtrip));

  const std::string constant_bytes = schema_model();
  CHECK(!constant_bytes.empty());
  const auto* constant_first =
      reinterpret_cast<const std::uint8_t*>(constant_bytes.data());
  const joggle::Attr::Bytes constant_payload(
      constant_first, constant_first + constant_bytes.size());
  const std::vector<joggle::Attr> constant_args{
      joggle::Attr(constant_payload)};
  std::vector<joggle::Attr> constant_returns;
  CHECK(env.call("onnx.read", constant_args, constant_returns));
  CHECK(constant_returns.size() == 1 && constant_returns[0].string());

  joggle::Mod constant;
  CHECK(joggle::parse(env, *constant_returns[0].string(), constant,
                      "schema-forms.onnx"));
  CHECK(constant.verify(env));
  joggle::Op literal;
  joggle::Op reshape;
  joggle::Op slice;
  for (joggle::Op op : constant.ops()) {
    if (op.callee() == "onnx.Constant")
      literal = op;
    if (op.callee() == "onnx.Reshape")
      reshape = op;
    if (op.callee() == "onnx.Slice")
      slice = op;
  }
  CHECK(literal && literal.outs().size() == 1);
  CHECK(reshape && reshape.outs().size() == 1);
  CHECK(slice && slice.outs().size() == 1);
  CHECK(literal.outs()[0].type() == joggle::Ty("_"));
  CHECK(reshape.outs()[0].type() == joggle::Ty("_"));
  CHECK(slice.outs()[0].type() == joggle::Ty("_"));
  CHECK(env.load("onnx.nn"));
  CHECK(joggle::run(env, "onnx.nn.infer", constant));
  CHECK(literal.outs()[0].type() == joggle::Ty("tensor<i64, [2]>"));
  CHECK(reshape.outs()[0].type() == joggle::Ty("tensor<f32, [3, 2]>"));
  CHECK(slice.outs()[0].type() == joggle::Ty("tensor<f32, [2, 2]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", constant));
  CHECK(constant.verify(env));
  CHECK(calls(constant, "onnx.Constant") == 0);
  CHECK(calls(constant, "onnx.Reshape") == 0);
  CHECK(calls(constant, "onnx.Slice") == 0);
  CHECK(calls(constant, "onnx.tensor") == 0);
  CHECK(calls(constant, "tensor.literal") == 1);
  CHECK(calls(constant, "tensor.reshape") == 1);
  CHECK(calls(constant, "tensor.slice") == 1);
  joggle::Mod constant_roundtrip;
  CHECK(joggle::parse(env, joggle::print(constant), constant_roundtrip,
                      "schema-forms-roundtrip.jog"));
  CHECK(constant_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(constant, constant_roundtrip));
  return 0;
}
