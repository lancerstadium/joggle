#include "joggle/joggle.h"

#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
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

std::size_t count(const joggle::Mod& mod, std::string_view callee) {
  std::size_t result = 0;
  for (joggle::Op op : mod.ops())
    result += op.kind() == joggle::Op::Kind::call && op.callee() == callee;
  return result;
}

bool integers(joggle::Val value,
              std::initializer_list<std::int64_t> expected) {
  const joggle::Op list = value.def();
  if (!list || list.callee() != "base.list" ||
      list.args().size() != expected.size())
    return false;
  std::size_t index = 0;
  for (const std::int64_t item : expected) {
    const joggle::Attr value = list.args()[index++].constant();
    if (value.integer() != item)
      return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 3);
  std::ifstream input(argv[1], std::ios::binary);
  CHECK(input);
  const std::vector<unsigned char> raw{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};

  joggle::Env env;
  env.path(argv[2]);
  CHECK(env.load("tflite"));
  std::vector<joggle::Attr> rejected;
  const std::vector<joggle::Attr> truncated{
      joggle::Attr(joggle::Attr::Bytes(raw.begin(), raw.begin() + 16))};
  CHECK(!env.call("tflite.read", truncated, rejected));
  CHECK(!env.diags().empty());
  env.clear_diags();
  const std::vector<joggle::Attr> args{
      joggle::Attr(joggle::Attr::Bytes(raw.begin(), raw.end()))};
  std::vector<joggle::Attr> returns;
  CHECK(env.call("tflite.read", args, returns));
  CHECK(returns.size() == 1 && returns.front().string());

  joggle::Mod model;
  CHECK(joggle::parse(env, *returns.front().string(), model, argv[1]));
  CHECK(model.verify(env));
  const joggle::Fn main = model.find_fn("main");
  CHECK(main && main.params().size() == 1 && main.returns().size() == 1);
  CHECK(main.params().front().type() ==
        joggle::Ty("tensor<f32, [1, 224, 224, 3]>"));
  CHECK(main.params().front().meta("tflite") &&
        main.params().front().meta("tflite")->dict());
  CHECK(main.returns().front() == joggle::Ty("tensor<f32, [1, 1001]>"));

  CHECK(count(model, "tflite.tensor") == 107);
  CHECK(count(model, "tflite.CONV_2D") == 36);
  CHECK(count(model, "tflite.DEPTHWISE_CONV_2D") == 17);
  CHECK(count(model, "tflite.ADD") == 10);
  CHECK(count(model, "tflite.AVERAGE_POOL_2D") == 1);
  CHECK(count(model, "tflite.RESHAPE") == 1);
  CHECK(count(model, "tflite.SOFTMAX") == 1);

  std::size_t compute = 0;
  std::size_t payload = 0;
  bool checked_options = false;
  bool checked_model = false;
  for (joggle::Op op : main.ops()) {
    if (op.kind() != joggle::Op::Kind::call)
      continue;
    if (op.callee() == "tflite.model") {
      CHECK(op.args().size() == 1 && op.args().front().is_const());
      const joggle::Attr info = op.args().front().constant();
      CHECK(info.dict() && info.dict()->contains("metadata") &&
            info.dict()->contains("signatures"));
      checked_model = true;
    } else if (op.callee() == "tflite.tensor") {
      CHECK(op.args().size() == 1 && op.args().front().is_const());
      const joggle::Attr bytes = op.args().front().constant();
      CHECK(bytes.bytes());
      payload += bytes.bytes()->size();
      CHECK(op.meta("tflite") == nullptr);
      CHECK(op.outs().size() == 1 && op.outs()[0].meta("tflite") &&
            op.outs()[0].meta("tflite")->dict());
    } else if (op.callee().starts_with("tflite.") &&
               op.callee() != "tflite.model") {
      ++compute;
      const joggle::Attr* meta = op.meta("tflite");
      CHECK(meta && meta->dict());
      for (joggle::Val out : op.outs())
        CHECK(out.meta("tflite") && out.meta("tflite")->dict());
      const auto options = meta->dict()->find("options");
      CHECK(options != meta->dict()->end() && options->second.dict());
      if (op.callee() == "tflite.CONV_2D" && !checked_options) {
        CHECK(op.args().size() == 3);
        CHECK(options->second.dict()->contains("stride_h"));
        CHECK(options->second.dict()->contains("stride_w"));
        checked_options = true;
      }
    }
  }
  CHECK(compute == 66 && checked_model && checked_options &&
        payload > 13'000'000);

  CHECK(env.load("opt"));
  joggle::Attr missing;
  CHECK(joggle::query(env, "opt.unresolved", model, missing));
  CHECK(missing.list() && missing.list()->size() == 6);

  const std::string canonical = joggle::print(model);
  joggle::Mod roundtrip;
  CHECK(joggle::parse(env, canonical, roundtrip, "tflite-roundtrip.jog"));
  CHECK(roundtrip.verify(env));
  CHECK(joggle::structurally_equal(model, roundtrip));

  CHECK(env.load("tflite.nn"));
  joggle::Mod semantic;
  CHECK(joggle::parse(env, canonical, semantic, "tflite-semantic.jog"));
  CHECK(semantic.verify(env));
  CHECK(joggle::run(env, "tflite.nn.convert", semantic));
  CHECK(semantic.verify(env));
  CHECK(count(semantic, "tflite.tensor") == 0);
  CHECK(count(semantic, "tflite.model") == 0);
  CHECK(count(semantic, "tensor.literal") == 107);
  CHECK(count(semantic, "tflite.CONV_2D") == 0);
  CHECK(count(semantic, "tflite.DEPTHWISE_CONV_2D") == 0);
  CHECK(count(semantic, "tflite.ADD") == 0);
  CHECK(count(semantic, "tflite.AVERAGE_POOL_2D") == 0);
  CHECK(count(semantic, "tflite.RESHAPE") == 0);
  CHECK(count(semantic, "tflite.SOFTMAX") == 0);
  CHECK(count(semantic, "nn.conv2d") == 53);
  CHECK(count(semantic, "nn.add") == 10);
  CHECK(count(semantic, "nn.avg_pool2d") == 1);
  CHECK(count(semantic, "tensor.reshape") == 1);
  CHECK(count(semantic, "nn.softmax") == 1);
  CHECK(joggle::query(env, "opt.unresolved", semantic, missing));
  CHECK(missing.list() && missing.list()->empty());
  std::size_t standard_layouts = 0;
  std::size_t depthwise_layouts = 0;
  for (joggle::Op op : semantic.ops()) {
    if (op.callee() == "nn.conv2d") {
      CHECK(op.args().size() == 11);
      CHECK(integers(op.args()[7], {0, 2, 3, 1}));
      CHECK(integers(op.args()[9], {0, 2, 3, 1}));
      if (integers(op.args()[8], {0, 2, 3, 1}))
        ++standard_layouts;
      else if (integers(op.args()[8], {-1, 2, 3, 0}))
        ++depthwise_layouts;
      else
        CHECK(false);
      CHECK(op.meta("tflite") == nullptr);
    }
  }
  CHECK(standard_layouts == 36 && depthwise_layouts == 17);
  const std::string converted = joggle::print(semantic);
  CHECK(joggle::run(env, "tflite.nn.convert", semantic));
  CHECK(joggle::print(semantic) == converted);
  joggle::Mod semantic_roundtrip;
  CHECK(joggle::parse(env, converted, semantic_roundtrip,
                      "tflite-semantic-roundtrip.jog"));
  if (!semantic_roundtrip.verify(env))
    return semantic_roundtrip.print_diags(stderr);
  CHECK(joggle::structurally_equal(semantic, semantic_roundtrip));

  std::size_t exposed = 0;
  for (joggle::Op op : semantic_roundtrip.ops()) {
    const std::string_view callee = op.callee();
    const bool selected =
        callee == "nn.conv2d" || callee == "nn.add" ||
        callee == "nn.avg_pool2d" || callee == "tensor.reshape" ||
        callee == "nn.softmax";
    if (!selected)
      continue;
    const joggle::Fn fn = env.resolve(semantic_roundtrip, op);
    CHECK(fn && env.expand(semantic_roundtrip, op, fn));
    ++exposed;
  }
  CHECK(exposed == 66);
  CHECK(semantic_roundtrip.verify(env));
  const std::string exposed_text = joggle::print(semantic_roundtrip);
  joggle::Mod exposed_roundtrip;
  CHECK(joggle::parse(env, exposed_text, exposed_roundtrip,
                      "tflite-exposed-roundtrip.jog"));
  CHECK(exposed_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(semantic_roundtrip, exposed_roundtrip));

  constexpr std::string_view broadcast_source =
      "module broadcast\n"
      "use tflite\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3]>, right: tensor<f32, [2, 1]>\n"
      ") -> tensor<f32, [2, 3]> {\n"
      "  [tflite: {options: {fused_activation_function: \"NONE\"}}]\n"
      "  let out: tensor<f32, [2, 3]> = tflite.ADD(left, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod broadcast;
  CHECK(joggle::parse(env, broadcast_source, broadcast,
                      "tflite-broadcast.jog"));
  CHECK(broadcast.verify(env));
  CHECK(joggle::run(env, "tflite.nn.convert", broadcast));
  CHECK(broadcast.verify(env));
  CHECK(count(broadcast, "tflite.ADD") == 0);
  CHECK(count(broadcast, "nn.add") == 1);
  for (joggle::Op op : broadcast.ops()) {
    if (op.callee() != "nn.add")
      continue;
    const joggle::Fn fn = env.resolve(broadcast, op);
    CHECK(fn && env.expand(broadcast, op, fn));
  }
  CHECK(broadcast.verify(env));

  constexpr std::string_view unsupported_source =
      "module unsupported\n"
      "use tflite\n"
      "fn main(x: tensor<f32, [1, 2]>) -> tensor<f32, [1, 2]> {\n"
      "  [tflite: {options: {fused_activation_function: \"TANH\"}}]\n"
      "  let y: tensor<f32, [1, 2]> = tflite.ADD(x, x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod unsupported;
  CHECK(joggle::parse(env, unsupported_source, unsupported,
                      "tflite-unsupported.jog"));
  CHECK(unsupported.verify(env));
  CHECK(joggle::run(env, "tflite.nn.convert", unsupported));
  CHECK(unsupported.verify(env));
  CHECK(count(unsupported, "tflite.ADD") == 1);
  CHECK(count(unsupported, "nn.add") == 0);
  return 0;
}
