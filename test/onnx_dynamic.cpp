#include "joggle/joggle.h"
#include "onnx.pb.h"

#include <cstdio>
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

void dynamic_type(jogonnx::ValueInfoProto* value, std::string name) {
  value->set_name(std::move(name));
  auto* tensor = value->mutable_type()->mutable_tensor_type();
  tensor->set_elem_type(1);
  tensor->mutable_shape()->add_dim()->set_dim_param("batch-size");
  tensor->mutable_shape()->add_dim()->set_dim_param("batch_size");
  tensor->mutable_shape()->add_dim();
}

std::string model() {
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

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 2);
  joggle::Env env;
  env.path(argv[1]);
  CHECK(env.load("onnx"));

  const std::string bytes = model();
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
  const joggle::Ty dynamic(
      "tensor<f32, [batch_size, batch_size_1, _]>");
  CHECK(main.params()[0].type() == dynamic);
  CHECK(main.returns().size() == 1 && main.returns()[0] == dynamic);

  joggle::Mod roundtrip;
  CHECK(joggle::parse(env, joggle::print(mod), roundtrip,
                      "dynamic-roundtrip.jog"));
  CHECK(roundtrip.verify(env));
  CHECK(joggle::structurally_equal(mod, roundtrip));
  return 0;
}
