#include "joggle/joggle.h"
#include "schema_generated.h"

#include "flatbuffers/flatbuffer_builder.h"

#include <cstdio>
#include <cstdint>
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

flatbuffers::Offset<tflite::QuantizationParameters>
quant(flatbuffers::FlatBufferBuilder& builder, float scale,
      std::int64_t zero) {
  const std::vector<float> scales{scale};
  const std::vector<std::int64_t> zeros{zero};
  return tflite::CreateQuantizationParametersDirect(
      builder, nullptr, nullptr, &scales, &zeros);
}

std::vector<std::uint8_t> model() {
  flatbuffers::FlatBufferBuilder builder;
  const std::vector<std::int32_t> shape{1, 2};
  std::vector<flatbuffers::Offset<tflite::Tensor>> tensors;
  tensors.push_back(tflite::CreateTensorDirect(
      builder, &shape, tflite::TensorType_INT8, 0, "left",
      quant(builder, 0.5F, -1)));
  tensors.push_back(tflite::CreateTensorDirect(
      builder, &shape, tflite::TensorType_INT8, 0, "right",
      quant(builder, 0.25F, 0)));
  tensors.push_back(tflite::CreateTensorDirect(
      builder, &shape, tflite::TensorType_INT8, 0, "sum",
      quant(builder, 0.75F, 1)));

  const auto options = tflite::CreateAddOptions(builder);
  const std::vector<std::int32_t> inputs{0, 1};
  const std::vector<std::int32_t> outputs{2};
  const auto add = tflite::CreateOperatorDirect(
      builder, 0, &inputs, &outputs, tflite::BuiltinOptions_AddOptions,
      options.Union());
  const std::vector<flatbuffers::Offset<tflite::Operator>> ops{add};
  const auto graph = tflite::CreateSubGraphDirect(
      builder, &tensors, &inputs, &outputs, &ops, "quantized_add");
  const std::vector<flatbuffers::Offset<tflite::SubGraph>> graphs{graph};
  const auto code = tflite::CreateOperatorCode(
      builder, static_cast<std::int8_t>(tflite::BuiltinOperator_ADD), 0, 1,
      tflite::BuiltinOperator_ADD);
  const std::vector<flatbuffers::Offset<tflite::OperatorCode>> codes{code};
  const auto buffer = tflite::CreateBuffer(builder);
  const std::vector<flatbuffers::Offset<tflite::Buffer>> buffers{buffer};
  const auto root = tflite::CreateModelDirect(
      builder, 3, &codes, &graphs, "value metadata test", &buffers);
  tflite::FinishModelBuffer(builder, root);
  return {builder.GetBufferPointer(),
          builder.GetBufferPointer() + builder.GetSize()};
}

const joggle::Attr::Dict* source(joggle::Val value) {
  const joggle::Attr* meta = value.meta("tflite");
  return meta ? meta->dict() : nullptr;
}

bool quant_matches(joggle::Val value, std::string_view name, double scale,
                   std::int64_t zero) {
  const joggle::Attr::Dict* meta = source(value);
  if (!meta || meta->at("name").string() != name)
    return false;
  const joggle::Attr::Dict* quantization =
      meta->at("quantization").dict();
  if (!quantization)
    return false;
  const joggle::Attr::List* scales = quantization->at("scale").list();
  const joggle::Attr::List* zeros = quantization->at("zero_point").list();
  return scales && scales->size() == 1 && zeros && zeros->size() == 1 &&
         (*scales)[0].real() == scale && (*zeros)[0].integer() == zero;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 2);
  joggle::Env env;
  env.path(argv[1]);
  CHECK(env.load("tflite"));

  const std::vector<std::uint8_t> bytes = model();
  const std::vector<joggle::Attr> args{
      joggle::Attr(joggle::Attr::Bytes(bytes.begin(), bytes.end()))};
  std::vector<joggle::Attr> returns;
  CHECK(env.call("tflite.read", args, returns));
  CHECK(returns.size() == 1 && returns[0].string());

  joggle::Mod mod;
  CHECK(joggle::parse(env, *returns[0].string(), mod, "quantized.tflite"));
  CHECK(mod.verify(env));
  const joggle::Fn main = mod.find_fn("main");
  CHECK(main && main.params().size() == 2);
  CHECK(quant_matches(main.params()[0], "left", 0.5, -1));
  CHECK(quant_matches(main.params()[1], "right", 0.25, 0));

  joggle::Op add;
  for (joggle::Op op : main.ops())
    if (op.callee() == "tflite.ADD")
      add = op;
  CHECK(add && add.outs().size() == 1 && add.meta("tflite"));
  CHECK(quant_matches(add.outs()[0], "sum", 0.75, 1));

  const std::string canonical = joggle::print(mod);
  joggle::Mod roundtrip;
  CHECK(joggle::parse(env, canonical, roundtrip, "quantized-roundtrip.jog"));
  CHECK(roundtrip.verify(env));
  CHECK(joggle::structurally_equal(mod, roundtrip));

  CHECK(env.load("tflite.nn"));
  CHECK(joggle::run(env, "tflite.nn.convert", mod));
  CHECK(mod.verify(env));
  joggle::Op lowered;
  for (joggle::Op op : mod.ops())
    if (op.callee() == "nn.add")
      lowered = op;
  CHECK(lowered && lowered.outs().size() == 1);
  CHECK(quant_matches(lowered.outs()[0], "sum", 0.75, 1));
  const joggle::Fn callee = env.resolve(mod, lowered);
  CHECK(callee && mod.expand(lowered, callee));
  CHECK(mod.verify(env));
  const joggle::Op returned = main.body().ops().back();
  CHECK(returned.kind() == joggle::Op::Kind::ret &&
        returned.args().size() == 1);
  CHECK(quant_matches(returned.args()[0], "sum", 0.75, 1));
  return 0;
}
