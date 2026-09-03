#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

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

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_MEM_MODULE);
  compiler.load(JOGGLE_ONNX_MODULE);
  compiler.load(JOGGLE_REFERENCE_TARGET_MODULE);
  compiler.add(R"(
joggle 1;
module reference_pipeline@1.0.0 {
  import onnx@2.0.0;
  import reference_target@1.0.0;

  fn compile(input: bytes) -> module {
    source = onnx.read(input);
    model = onnx.to_nn(source);
    return reference_target.map(model, 8, 8);
  }
}
)",
               "reference-pipeline.joggle");
  if (!compiler.link() ||
      !compiler.load_behavior("onnx", JOGGLE_ONNX_BEHAVIOR) ||
      !compiler.load_behavior("reference_target",
                              JOGGLE_REFERENCE_TARGET_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto source = read(JOGGLE_REFERENCE_TARGET_ONNX_MODEL);
  const auto first = source
                         ? compiler.run<joggle::Module>(
                               "reference_pipeline.compile", *source)
                         : std::optional<joggle::Module>{};
  const auto second = source
                          ? compiler.run<joggle::Module>(
                                "reference_pipeline.compile", *source)
                          : std::optional<joggle::Module>{};
  const auto target = compiler.module("reference_target");
  const auto memory = compiler.module("mem");
  const auto analyze =
      target ? target->function("local_bytes_upper_bound") : std::nullopt;
  const auto reference = memory ? memory->interface("reference") : std::nullopt;
  if (!first || !second || !analyze || !reference) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto main = first->function("main");
  const joggle::Function* body = main ? main->body() : nullptr;
  const auto bytes =
      compiler.run<std::int64_t>(*analyze, *first);
  bool valid = body != nullptr && bytes && *bytes > 0 &&
               body->ops().size() == 91U && first->data().size() == 42U &&
               first->digest() == second->digest() &&
               first->data() == second->data();
  if (body != nullptr) {
    const auto arguments = body->arguments();
    valid = valid && arguments.size() == 1U &&
            compiler.conforms(arguments.front().type().schema(), *reference);
    for (const auto& op : body->ops()) {
      const auto results = op.results();
      valid = valid &&
              op.callee().symbol().module_name() == "reference_target" &&
              std::all_of(results.begin(), results.end(),
                          [&](const joggle::Value& value) {
                            return compiler.conforms(value.type().schema(),
                                                     *reference);
                          });
    }
  }

  std::size_t payload_bytes = 0;
  for (const auto& name : first->data()) {
    const auto payload = first->data(name);
    payload_bytes += payload ? payload->size() : 0U;
  }
  valid = valid && payload_bytes == 46738848U;
  std::cout << "module " << first->name() << '#' << first->digest() << '\n'
            << "ops " << (body ? body->ops().size() : 0U) << '\n'
            << "resources " << first->data().size() << '\n'
            << "resource-bytes " << payload_bytes << '\n'
            << "local-byte-upper-bound " << (bytes ? *bytes : 0) << '\n';
  return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
