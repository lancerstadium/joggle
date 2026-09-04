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

module onnx_fusion_pipeline@1.0.0 {
  import fusion@1;
  import onnx@1;

  fn compile(input: bytes, name: string) -> module {
    model = @onnx.read(input, name);
    return @fusion.run(model);
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
  if (argc != 7) {
    return EXIT_FAILURE;
  }
  joggle::Compiler compiler;
  compiler.load(argv[1]);
  compiler.load(argv[2]);
  compiler.load(argv[4]);
  compiler.add(pipeline_source, "onnx-fusion-pipeline.joggle");
  if (!compiler.link() || !compiler.load_native("fusion", argv[3]) ||
      !compiler.load_native("onnx", argv[5])) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto bytes = read_bytes(argv[6]);
  const auto source = compiler.run<joggle::Module>(
      "onnx.read", bytes, std::string{"squeezenet_fusion"});
  const auto optimized = compiler.run<joggle::Module>(
      "onnx_fusion_pipeline.compile", bytes,
      std::string{"squeezenet_fusion"});
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
  std::size_t convs = 0;
  std::size_t relus = 0;
  for (const joggle::Op& op : after->ops()) {
    const auto owner = op.callee().symbol().module_name();
    const auto name = op.callee().symbol().local_name();
    constants += static_cast<std::size_t>(owner == "tensor" &&
                                          name == "constant");
    convs += static_cast<std::size_t>(owner == "tensor" && name == "conv");
    relus += static_cast<std::size_t>(owner == "tensor" && name == "relu");
    if (owner == "fusion" && name == "conv_relu") {
      ++fused;
      located_fused += static_cast<std::size_t>(op.location().has_value());
    }
  }

  bool ok = true;
  ok &= expect(before->ops().size() == 117U && after->ops().size() == 91U &&
                   constants == 52U && fused == 26U && convs == 0U &&
                   relus == 0U,
               "all 26 official SqueezeNet Conv/ReLU pairs fuse");
  ok &= expect(located_fused == fused,
               "fused calls retain deterministic ONNX provenance");
  joggle::Diagnostics equivalence;
  ok &= expect(joggle::equivalent(compiler, *before, *after, equivalence) &&
                   equivalence.ok(),
               "the whole transformed model is definitionally equivalent");
  ok &= expect(compiler.verify(*optimized),
               "the transformed official model remains valid Module IR");
  const std::string canonical = joggle::format(*optimized);
  ok &= expect(canonical.find("import fusion@1.0.0;") != std::string::npos,
               "canonical output derives the new Module dependency from IR");
  if (!ok) {
    equivalence.print(std::cerr);
    compiler.diagnostics().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
