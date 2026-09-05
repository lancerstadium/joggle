#include <algorithm>
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

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

std::string element_name(const joggle::Type& tensor) {
  const auto element = tensor.get<joggle::Type>("element");
  return element ? std::string(element->schema().name()) : std::string{};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 8) {
    return EXIT_FAILURE;
  }
  const auto bytes = read_bytes(argv[7]);
  if (bytes.empty()) {
    return EXIT_FAILURE;
  }

  bool ok = true;
  joggle::Compiler compiler;
  for (int index = 1; index <= 5; ++index) {
    compiler.load(argv[index]);
  }
  if (!compiler.link() || !compiler.load_native("onnx", argv[6])) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto model = compiler.run<joggle::Mod>("onnx.read", bytes,
                                               std::string{"squeezenet_qdq"});
  const auto repeated = compiler.run<joggle::Mod>(
      "onnx.read", bytes, std::string{"squeezenet_qdq"});
  if (!model || !repeated) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto main = model->fn("main");
  const auto body = main ? main->body() : nullptr;
  if (!body) {
    return EXIT_FAILURE;
  }

  ok &= expect(model->digest() == repeated->digest(),
               "repeated QDQ import has one deterministic identity");
  const auto arguments = body->arguments();
  const auto returned = body->entry().terminator().returned();
  ok &= expect(arguments.size() == 1U &&
                   element_name(arguments.front().type()) == "f32" &&
                   arguments.front().type().get<std::vector<std::int64_t>>(
                       "shape") == std::vector<std::int64_t>({1, 3, 224, 224}),
               "QDQ model keeps its typed floating-point input");
  ok &= expect(
      returned.size() == 1U && element_name(returned.front().type()) == "f32" &&
          returned.front().type().get<std::vector<std::int64_t>>("shape") ==
              std::vector<std::int64_t>({1, 1000, 1, 1}),
      "QDQ propagation reaches the declared output");

  std::map<std::string, std::size_t> calls;
  std::map<std::string, std::size_t> constants;
  std::size_t located = 0;
  bool payloads_resolve = true;
  for (const auto& op : body->ops()) {
    located += static_cast<std::size_t>(op.location().has_value());
    const auto symbol = op.callee().referenced_fn()->symbol();
    const auto mod_name = symbol.mod_name();
    const auto name = symbol.local_name();
    if (mod_name == "tensor" && name == "constant") {
      ++constants[element_name(op.value().type())];
      const auto digest = op.callee().binding<std::string>("content");
      payloads_resolve &= digest && model->data(*digest).has_value();
    } else {
      ++calls[std::string(mod_name) + "." + std::string(name)];
    }
  }
  ok &= expect(body->ops().size() == 399U && located == body->ops().size(),
               "all 228 constants and 171 calls retain provenance");
  ok &= expect(constants["f32"] == 88U && constants["u8"] == 36U &&
                   constants["i8"] == 52U && constants["i32"] == 52U &&
                   payloads_resolve,
               "all typed QDQ initializers are Mod-owned constants");
  ok &= expect(
      calls["quant.quantize_linear"] == 39U &&
          calls["quant.dequantize_linear"] == 91U &&
          calls["nn.conv"] == 26U && calls["nn.max_pool"] == 3U &&
          calls["nn.global_average_pool"] == 1U &&
          calls["nn.concat"] == 8U && calls["nn.reshape"] == 1U &&
          calls["nn.flatten"] == 1U && calls["nn.softmax"] == 1U &&
          calls.size() == 9U,
      "the complete standard QDQ graph preserves ordinary ONNX fn calls");

  bool source_found = false;
  for (const auto& digest : model->data()) {
    const auto data = model->data(digest);
    if (!data) {
      continue;
    }
    const std::string_view view{reinterpret_cast<const char*>(data->data()),
                                data->size()};
    source_found |=
        data->size() == 1345213U &&
        joggle::sha256(view) ==
            "4a567dd7542ef440890d57268fabf47211174c593d7a1837bd7f16a1067169e7";
  }
  ok &= expect(source_found,
               "the exact hash-pinned Model Zoo QDQ source is retained");

  const auto dependencies = model->dependencies();
  const auto has_dependency = [&](std::string_view name) {
    return std::any_of(
        dependencies.begin(), dependencies.end(),
        [&](const auto& dependency) { return dependency.name == name; });
  };
  ok &= expect(has_dependency("tensor") && has_dependency("nn") &&
                   has_dependency("quant") && !has_dependency("onnx"),
               "the generated Mod records semantic rather than format "
               "dependencies");
  if (!ok) {
    compiler.diag().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
