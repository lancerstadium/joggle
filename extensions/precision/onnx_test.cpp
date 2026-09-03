#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include <joggle/joggle.h>

namespace {

std::optional<joggle::Bytes> read(std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
  joggle::Bytes result;
  result.reserve(source.size());
  for (const char value : source) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

bool is_f16_tensor(const joggle::Type& type) {
  if (type.schema().symbol().qualified_name() != "tensor.ranked") {
    return true;
  }
  const auto element = type.get<joggle::Type>("element");
  return element &&
         element->schema().symbol().qualified_name() == "prelude.f16";
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_RESOURCE_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_ONNX_MODULE);
  compiler.load(JOGGLE_PRECISION_MODULE);
  compiler.add(R"(
joggle 1;
module edge_precision@1.0.0 {
  import onnx@2.0.0;
  import precision@1.0.0;
  import resource@1.0.0;

  fn compile(input: bytes) -> (module, resource.set) {
    model, resources = onnx.read(input);
    output, converted = precision.f32_to_f16(model, resources);
    return output, converted;
  }
}
)",
               "edge-precision.joggle");
  if (!compiler.link() ||
      !compiler.load_behavior("onnx", JOGGLE_ONNX_BEHAVIOR) ||
      !compiler.load_behavior("precision", JOGGLE_PRECISION_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto source = read(JOGGLE_PRECISION_ONNX_MODEL);
  using Result = std::tuple<joggle::Module, joggle::ResourceSet>;
  const auto first =
      source ? compiler.run<Result>("edge_precision.compile", *source)
             : std::optional<Result>{};
  const auto second =
      source ? compiler.run<Result>("edge_precision.compile", *source)
             : std::optional<Result>{};
  if (!first || !second) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto& [model, resources] = *first;
  const auto main = model.function("main");
  const auto* body = main ? main->body() : nullptr;
  const auto arguments =
      body ? body->arguments() : std::vector<joggle::Value>{};
  const auto results =
      body ? body->result_types() : std::vector<joggle::Type>{};
  bool valid = body && body->instructions().size() == 91U &&
               arguments.size() == 1U &&
               std::all_of(arguments.begin(), arguments.end(),
                           [](const joggle::Value& value) {
                             return is_f16_tensor(value.type());
                           }) &&
               std::all_of(results.begin(), results.end(), is_f16_tensor);
  if (body) {
    for (const auto& instruction : body->instructions()) {
      const auto instruction_results = instruction.results();
      valid = valid && std::all_of(instruction_results.begin(),
                                   instruction_results.end(),
                                   [](const joggle::Value& value) {
                                     return is_f16_tensor(value.type());
                                   });
    }
  }
  std::size_t bytes = 0;
  for (const auto& [name, payload] : resources) {
    static_cast<void>(name);
    bytes += payload.size();
  }
  valid = valid && resources.size() == 42U && bytes == 23369424U &&
          model.digest() == std::get<0>(*second).digest() &&
          resources == std::get<1>(*second);
  std::cout << "module " << model.name() << '#' << model.digest() << '\n'
            << "instructions " << (body ? body->instructions().size() : 0U)
            << '\n'
            << "resources " << resources.size() << '\n'
            << "resource-bytes " << bytes << '\n';
  return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
