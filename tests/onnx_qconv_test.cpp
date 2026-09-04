#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

constexpr std::string_view pipeline_source = R"(
joggle 1;

module onnx_qconv_pipeline@1.0.0 {
  import onnx@1;
  import qconv@1;

  fn compile(input: bytes, name: string) -> module {
    model = @onnx.read(input, name);
    return @qconv.run(model);
  }
}
)";

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

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 8) {
    return EXIT_FAILURE;
  }
  joggle::Compiler compiler;
  compiler.load(argv[1]);
  compiler.load(argv[2]);
  compiler.load(argv[3]);
  compiler.load(argv[5]);
  compiler.add(pipeline_source, "onnx-qconv-pipeline.joggle");
  if (!compiler.link() || !compiler.load_native("qconv", argv[4]) ||
      !compiler.load_native("onnx", argv[6])) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto bytes = read_bytes(argv[7]);
  const auto source = compiler.run<joggle::Module>(
      "onnx.read", bytes, std::string{"squeezenet_qconv"});
  const auto optimized = compiler.run<joggle::Module>(
      "onnx_qconv_pipeline.compile", bytes,
      std::string{"squeezenet_qconv"});
  const auto source_main = source ? source->function("main") : std::nullopt;
  const auto optimized_main =
      optimized ? optimized->function("main") : std::nullopt;
  const joggle::Function* before = source_main ? source_main->body() : nullptr;
  const joggle::Function* after =
      optimized_main ? optimized_main->body() : nullptr;
  if (before == nullptr || after == nullptr) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  std::size_t constants = 0;
  std::size_t fused = 0;
  std::size_t located_fused = 0;
  std::size_t quantize = 0;
  std::size_t dequantize = 0;
  std::size_t convolutions = 0;
  for (const joggle::Op& op : after->ops()) {
    const auto symbol = op.callee().symbol();
    const auto owner = symbol.module_name();
    const auto name = symbol.local_name();
    constants += static_cast<std::size_t>(owner == "tensor" &&
                                          name == "constant");
    quantize += static_cast<std::size_t>(owner == "quant" &&
                                         name == "quantize");
    dequantize += static_cast<std::size_t>(owner == "quant" &&
                                           name == "dequantize");
    convolutions += static_cast<std::size_t>(owner == "tensor" &&
                                             name == "conv");
    if (owner == "qconv" && name == "conv") {
      ++fused;
      located_fused += static_cast<std::size_t>(op.location().has_value());
    }
  }

  bool ok = true;
  ok &= expect(before->ops().size() == 399U && after->ops().size() == 303U &&
                   constants == 228U && fused == 26U &&
                   located_fused == fused && quantize == 13U &&
                   dequantize == 21U && convolutions == 0U,
               "all 26 QDQ Conv expressions fuse without duplicating the "
               "eight activation dequantizers shared by 16 branches");
  joggle::Diagnostics equivalence;
  ok &= expect(joggle::equivalent(compiler, *before, *after, equivalence) &&
                   equivalence.ok(),
               "the whole QDQ model remains definitionally equivalent");
  ok &= expect(compiler.verify(*optimized),
               "the transformed QDQ model remains valid Module IR");
  const std::string canonical = joggle::format(*optimized);
  ok &= expect(canonical.find("import qconv@1.0.0;") != std::string::npos,
               "canonical output derives the qconv dependency from calls");
  const auto dependencies = optimized->dependencies();
  const auto has_dependency = [&](std::string_view name) {
    return std::any_of(dependencies.begin(), dependencies.end(),
                       [&](const auto& dependency) {
                         return dependency.name == name;
                       });
  };
  ok &= expect(has_dependency("qconv") && has_dependency("quant") &&
                   has_dependency("tensor"),
               "the transformed Module retains the complete semantic "
               "dependency closure");
  if (!ok) {
    std::cerr << "before=" << before->ops().size()
              << " after=" << after->ops().size()
              << " constants=" << constants << " qconv=" << fused
              << " located=" << located_fused
              << " quantize=" << quantize
              << " dequantize=" << dequantize
              << " conv=" << convolutions << '\n';
    equivalence.print(std::cerr);
    compiler.diagnostics().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
