#include "joggle/joggle.h"

#include <cstdio>
#include <fstream>
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
      CHECK(op.meta("tflite") && op.meta("tflite")->dict());
    } else if (op.callee().starts_with("tflite.") &&
               op.callee() != "tflite.model") {
      ++compute;
      const joggle::Attr* meta = op.meta("tflite");
      CHECK(meta && meta->dict());
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

  const std::string canonical = joggle::print(model);
  joggle::Mod roundtrip;
  CHECK(joggle::parse(env, canonical, roundtrip, "tflite-roundtrip.jog"));
  CHECK(roundtrip.verify(env));
  CHECK(joggle::structurally_equal(model, roundtrip));
  return 0;
}
