#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
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

joggle::Bytes read_bytes(const char* path) {
  std::ifstream input(path, std::ios::binary);
  const std::vector<char> characters{std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>()};
  joggle::Bytes result;
  result.reserve(characters.size());
  for (const char value : characters) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

bool load(joggle::Compiler& compiler, const char* tensor, const char* onnx,
          const char* native) {
  compiler.load(tensor);
  compiler.load(onnx);
  return compiler.link() && compiler.load_native("onnx", native);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 && argc != 5) {
    return EXIT_FAILURE;
  }

  joggle::Compiler malformed;
  if (!load(malformed, argv[1], argv[2], argv[3])) {
    malformed.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto rejected = malformed.run<joggle::Module>(
      "onnx.read", joggle::Bytes{std::byte{0x7f}}, std::string{"bad"});
  bool ok = expect(!rejected && !malformed.diagnostics().ok() &&
                       malformed.diagnostics().entries().back().message.find(
                           "valid ModelProto") != std::string::npos,
                   "malformed Protobuf input is rejected transactionally");

  if (argc == 4) {
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  const auto bytes = read_bytes(argv[4]);
  ok &= expect(!bytes.empty(), "reference model bytes are readable");

  joggle::Compiler compiler;
  if (!load(compiler, argv[1], argv[2], argv[3])) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto model =
      compiler.run<joggle::Module>("onnx.read", bytes,
                                   std::string{"squeezenet"});
  if (!model) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto main = model->function("main");
  const auto body = main ? main->body() : nullptr;
  if (!body) {
    return EXIT_FAILURE;
  }

  const auto ops = body->ops();
  std::size_t constants = 0;
  std::size_t located = 0;
  for (const auto& op : ops) {
    if (op.callee().symbol().module_name() == "tensor" &&
        op.callee().symbol().local_name() == "constant") {
      ++constants;
      const auto digest = op.property<std::string>("content");
      ok &= expect(digest && model->data(*digest).has_value(),
                   "constant content names Module-owned bytes");
    }
    located += static_cast<std::size_t>(op.location().has_value());
  }

  const auto arguments = body->arguments();
  const auto returned = body->entry().terminator().returned();
  ok &= expect(arguments.size() == 1U &&
                   arguments.front().type().get<std::vector<std::int64_t>>(
                       "shape") ==
                       std::vector<std::int64_t>({1, 3, 224, 224}),
               "reference model has one typed runtime input");
  ok &= expect(returned.size() == 1U &&
                   returned.front().type().get<std::vector<std::int64_t>>(
                       "shape") ==
                       std::vector<std::int64_t>({1, 1000}),
               "static propagation reaches the declared output shape");
  ok &= expect(ops.size() == 117U && constants == 52U &&
                   ops.size() - constants == 65U,
               "reference model has 52 constants and 65 semantic calls");
  ok &= expect(located == ops.size(),
               "every imported call retains deterministic provenance");
  ok &= expect(model->data().size() == 54U,
               "original model and all 53 initializers are retained");
  ok &= expect(compiler.verify(*model),
               "the imported Module satisfies ordinary IR verification");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
