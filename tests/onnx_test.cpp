#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

bool load(joggle::Compiler& compiler, const char* tensor, const char* onnx,
          const char* native) {
  compiler.load(tensor);
  compiler.load(onnx);
  return compiler.link() && compiler.load_native("onnx", native);
}

using ExpectedCall = std::pair<std::string_view, std::vector<std::int64_t>>;

const std::vector<ExpectedCall> expected_calls{
    {"conv", {1, 64, 111, 111}},    {"relu", {1, 64, 111, 111}},
    {"max_pool", {1, 64, 55, 55}},  {"conv", {1, 16, 55, 55}},
    {"relu", {1, 16, 55, 55}},      {"conv", {1, 64, 55, 55}},
    {"relu", {1, 64, 55, 55}},      {"conv", {1, 64, 55, 55}},
    {"relu", {1, 64, 55, 55}},      {"concat", {1, 128, 55, 55}},
    {"conv", {1, 16, 55, 55}},      {"relu", {1, 16, 55, 55}},
    {"conv", {1, 64, 55, 55}},      {"relu", {1, 64, 55, 55}},
    {"conv", {1, 64, 55, 55}},      {"relu", {1, 64, 55, 55}},
    {"concat", {1, 128, 55, 55}},   {"max_pool", {1, 128, 27, 27}},
    {"conv", {1, 32, 27, 27}},      {"relu", {1, 32, 27, 27}},
    {"conv", {1, 128, 27, 27}},     {"relu", {1, 128, 27, 27}},
    {"conv", {1, 128, 27, 27}},     {"relu", {1, 128, 27, 27}},
    {"concat", {1, 256, 27, 27}},   {"conv", {1, 32, 27, 27}},
    {"relu", {1, 32, 27, 27}},      {"conv", {1, 128, 27, 27}},
    {"relu", {1, 128, 27, 27}},     {"conv", {1, 128, 27, 27}},
    {"relu", {1, 128, 27, 27}},     {"concat", {1, 256, 27, 27}},
    {"max_pool", {1, 256, 13, 13}}, {"conv", {1, 48, 13, 13}},
    {"relu", {1, 48, 13, 13}},      {"conv", {1, 192, 13, 13}},
    {"relu", {1, 192, 13, 13}},     {"conv", {1, 192, 13, 13}},
    {"relu", {1, 192, 13, 13}},     {"concat", {1, 384, 13, 13}},
    {"conv", {1, 48, 13, 13}},      {"relu", {1, 48, 13, 13}},
    {"conv", {1, 192, 13, 13}},     {"relu", {1, 192, 13, 13}},
    {"conv", {1, 192, 13, 13}},     {"relu", {1, 192, 13, 13}},
    {"concat", {1, 384, 13, 13}},   {"conv", {1, 64, 13, 13}},
    {"relu", {1, 64, 13, 13}},      {"conv", {1, 256, 13, 13}},
    {"relu", {1, 256, 13, 13}},     {"conv", {1, 256, 13, 13}},
    {"relu", {1, 256, 13, 13}},     {"concat", {1, 512, 13, 13}},
    {"conv", {1, 64, 13, 13}},      {"relu", {1, 64, 13, 13}},
    {"conv", {1, 256, 13, 13}},     {"relu", {1, 256, 13, 13}},
    {"conv", {1, 256, 13, 13}},     {"relu", {1, 256, 13, 13}},
    {"concat", {1, 512, 13, 13}},   {"conv", {1, 1000, 13, 13}},
    {"relu", {1, 1000, 13, 13}},    {"average_pool", {1, 1000, 1, 1}},
    {"reshape", {1, 1000}},
};

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
  const auto rejected = malformed.run<joggle::Mod>(
      "onnx.read", joggle::Bytes{std::byte{0x7f}}, std::string{"bad"});
  joggle::Compiler malformed_again;
  if (!load(malformed_again, argv[1], argv[2], argv[3])) {
    malformed_again.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto rejected_again = malformed_again.run<joggle::Mod>(
      "onnx.read", joggle::Bytes{std::byte{0x7f}}, std::string{"bad"});
  const auto malformed_message =
      malformed.diagnostics().entries().back().message;
  bool ok = expect(
      !rejected && !rejected_again &&
          malformed_message.find("valid ModelProto") != std::string::npos &&
          malformed_message ==
              malformed_again.diagnostics().entries().back().message,
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
      compiler.run<joggle::Mod>("onnx.read", bytes, std::string{"squeezenet"});
  if (!model) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto repeated =
      compiler.run<joggle::Mod>("onnx.read", bytes, std::string{"squeezenet"});
  ok &= expect(repeated && repeated->digest() == model->digest(),
               "the same bytes and name produce the same Mod identity");
  const auto main = model->fn("main");
  const auto body = main ? main->body() : nullptr;
  if (!body) {
    return EXIT_FAILURE;
  }

  const auto ops = body->ops();
  std::size_t constants = 0;
  std::size_t located = 0;
  for (const auto& op : ops) {
    if (op.callee().symbol().mod_name() == "tensor" &&
        op.callee().symbol().local_name() == "constant") {
      ++constants;
      const auto digest = op.property<std::string>("content");
      ok &= expect(digest && model->data(*digest).has_value(),
                   "constant content names Mod-owned bytes");
    }
    located += static_cast<std::size_t>(op.location().has_value());
  }

  const auto arguments = body->arguments();
  const auto returned = body->entry().terminator().returned();
  ok &= expect(arguments.size() == 1U &&
                   arguments.front().type().get<std::vector<std::int64_t>>(
                       "shape") == std::vector<std::int64_t>({1, 3, 224, 224}),
               "reference model has one typed runtime input");
  ok &= expect(returned.size() == 1U &&
                   returned.front().type().get<std::vector<std::int64_t>>(
                       "shape") == std::vector<std::int64_t>({1, 1000}),
               "static propagation reaches the declared output shape");
  ok &= expect(ops.size() == 117U && constants == 52U &&
                   ops.size() - constants == 65U,
               "reference model has 52 constants and 65 semantic calls");
  ok &= expect(located == ops.size(),
               "every imported call retains deterministic provenance");
  ok &= expect(model->data().size() == 54U,
               "original model and all 53 initializers are retained");

  bool data_digests_match = true;
  bool source_digest_found = false;
  for (const auto& digest : model->data()) {
    const auto data = model->data(digest);
    if (!data) {
      data_digests_match = false;
      continue;
    }
    const std::string_view view{reinterpret_cast<const char*>(data->data()),
                                data->size()};
    data_digests_match &= digest == "sha256:" + joggle::sha256(view);
    source_digest_found |=
        data->size() == 4956208U &&
        joggle::sha256(view) ==
            "1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088";
  }
  ok &= expect(data_digests_match && source_digest_found,
               "every payload digest and the exact source-model digest match");

  bool sequence_matches = ops.size() == constants + expected_calls.size();
  std::size_t convs = 0;
  std::size_t relus = 0;
  std::size_t pools = 0;
  std::size_t concats = 0;
  for (std::size_t index = 0; sequence_matches && index < expected_calls.size();
       ++index) {
    const auto& op = ops[constants + index];
    const auto& expected = expected_calls[index];
    sequence_matches &=
        op.callee().symbol().local_name() == expected.first &&
        op.value().type().get<std::vector<std::int64_t>>("shape") ==
            expected.second &&
        op.location() && op.location()->source.starts_with("onnx:main/node/");
    if (expected.first == "conv") {
      ++convs;
      const auto strides = op.property<std::vector<std::int64_t>>("strides");
      const auto pads = op.property<std::vector<std::int64_t>>("pads");
      const auto dilations =
          op.property<std::vector<std::int64_t>>("dilations");
      sequence_matches &= strides && pads && dilations &&
                          (*strides == std::vector<std::int64_t>{1, 1} ||
                           *strides == std::vector<std::int64_t>{2, 2}) &&
                          (*pads == std::vector<std::int64_t>{0, 0, 0, 0} ||
                           *pads == std::vector<std::int64_t>{1, 1, 1, 1}) &&
                          *dilations == std::vector<std::int64_t>{1, 1} &&
                          op.property<std::int64_t>("group") == 1;
    } else if (expected.first == "relu") {
      ++relus;
    } else if (expected.first == "max_pool" ||
               expected.first == "average_pool") {
      ++pools;
      sequence_matches &= op.property<bool>("ceil_mode") == false &&
                          op.property<std::vector<std::int64_t>>("pads") ==
                              std::vector<std::int64_t>({0, 0, 0, 0});
    } else if (expected.first == "concat") {
      ++concats;
      sequence_matches &= op.property<std::int64_t>("axis") == 1;
    } else if (expected.first == "reshape") {
      sequence_matches &= op.property<std::vector<std::int64_t>>("shape") ==
                          std::vector<std::int64_t>({0, -1});
    }
  }
  ok &= expect(sequence_matches && convs == 26U && relus == 26U &&
                   pools == 4U && concats == 8U,
               "all 65 semantic calls, shapes, properties, and locations "
               "match the independently audited graph");
  ok &= expect(compiler.verify(*model),
               "the imported Mod satisfies ordinary IR verification");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
