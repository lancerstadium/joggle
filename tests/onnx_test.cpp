#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

joggle::Bytes bytes(const char* path) {
  std::ifstream input(path, std::ios::binary);
  const std::vector<char> source{std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>()};
  joggle::Bytes result;
  for (char value : source) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

bool load(joggle::Compiler& compiler, char** argv) {
  for (int index = 1; index <= 3; ++index) {
    compiler.load(argv[index]);
  }
  return compiler.link() && compiler.load_native("onnx", argv[4]);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 5 && argc != 6) {
    return EXIT_FAILURE;
  }

  joggle::Compiler malformed;
  if (!load(malformed, argv)) {
    malformed.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto rejected = malformed.run<joggle::Mod>(
      "onnx.read", joggle::Bytes{std::byte{0x7f}}, std::string{"bad"});
  bool ok = expect(!rejected &&
                       malformed.diag().issues().back().message.find(
                           "valid ModelProto") != std::string::npos,
                   "malformed ONNX is rejected transactionally");

  const auto shape = malformed.run<std::vector<std::int64_t>>(
      "onnx_schema.conv_shape", std::vector<std::int64_t>{1, 3, 224, 224},
      std::vector<std::int64_t>{64, 3, 3, 3},
      std::vector<std::int64_t>{3, 3}, std::vector<std::int64_t>{2, 2},
      std::vector<std::int64_t>{0, 0, 0, 0},
      std::vector<std::int64_t>{1, 1});
  ok &= expect(shape == std::vector<std::int64_t>({1, 64, 111, 111}),
               "the source schema owns ONNX shape rules");

  if (argc == 5) {
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  const auto model_bytes = bytes(argv[5]);
  joggle::Compiler compiler;
  if (model_bytes.empty() || !load(compiler, argv)) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto model = compiler.run<joggle::Mod>(
      "onnx.read", model_bytes, std::string{"squeezenet"});
  const auto repeated = compiler.run<joggle::Mod>(
      "onnx.read", model_bytes, std::string{"squeezenet"});
  if (!model || !repeated) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto main = model->fn("main");
  const joggle::Fn* body = main ? main->body() : nullptr;
  if (!body) {
    return EXIT_FAILURE;
  }

  std::map<std::string, std::size_t> calls;
  std::size_t located = 0;
  for (const joggle::Op& op : body->ops()) {
    const auto fn = op.callee().referenced_fn();
    if (!fn) {
      continue;
    }
    ++calls[std::string(fn->name())];
    located += static_cast<std::size_t>(op.location().has_value());
    if (fn->name() == "Constant") {
      const auto digest = op.callee().binding<std::string>("content");
      ok &= expect(digest && model->data(*digest).has_value(),
                   "constant payload is owned by the imported mod");
    }
  }
  const auto arguments = body->arguments();
  const auto returned = body->entry().terminator().returned();
  ok &= expect(repeated->digest() == model->digest(),
               "the same model import is deterministic");
  ok &= expect(arguments.size() == 1U &&
                   arguments.front().type().get<std::vector<std::int64_t>>(
                       "shape") == std::vector<std::int64_t>({1, 3, 224, 224}),
               "the model input remains statically typed");
  ok &= expect(returned.size() == 1U &&
                   returned.front().type().get<std::vector<std::int64_t>>(
                       "shape") == std::vector<std::int64_t>({1, 1000}),
               "schema inference reaches the declared output");
  ok &= expect(body->ops().size() == 118U && calls["Constant"] == 52U &&
                   calls["Conv"] == 26U && calls["Relu"] == 26U &&
                   calls["Concat"] == 8U && calls["MaxPool"] == 3U &&
                   calls["AveragePool"] == 1U && calls["Dropout"] == 1U &&
                   calls["Reshape"] == 1U,
               "the audited model graph is imported without op-specific C++"
               " dispatch");
  ok &= expect(located == body->ops().size() && model->data().size() == 54U &&
                   compiler.verify(*model),
               "the imported graph keeps provenance, payloads, and validity");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
