#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

std::string decode(const joggle::Bytes& bytes) {
  std::string result;
  result.reserve(bytes.size());
  for (const std::byte value : bytes) {
    result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
  }
  return result;
}

std::size_t calls(const joggle::Module& module, std::string_view symbol) {
  std::size_t count = 0;
  for (const joggle::Module::FunctionDecl& member : module.functions()) {
    const joggle::ir::Function* function = module.body(member.name());
    if (function == nullptr) {
      continue;
    }
    const auto instructions = function->instructions();
    count += static_cast<std::size_t>(std::count_if(
        instructions.begin(), instructions.end(),
        [symbol](const joggle::ir::Instruction& instruction) {
          return instruction.callee().symbol().qualified_name() == symbol;
        }));
  }
  return count;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_RESNET_BLOCK);
  compiler.load(JOGGLE_EXAMPLE_ACCEL_MODULE);
  compiler.load(JOGGLE_NN_PIPELINE_MODULE);
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto prepare = compiler.module("nn_pipeline");
  const auto prepare_function =
      prepare ? prepare->function("prepare") : std::nullopt;
  const auto compile_function =
      prepare ? prepare->function("compile") : std::nullopt;
  auto block = compiler.function("resnet18_basic_block.main");
  if (!prepare_function || !compile_function || !block ||
      !compiler.load_behavior("nn_pipeline", JOGGLE_NN_PIPELINE_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  joggle::Module model("resnet18_pipeline", {1, 0, 0});
  joggle::Diagnostics diagnostics;
  joggle::ir::Function second = block->clone();
  if (!model.insert("stage1_block0", std::move(*block), diagnostics) ||
      !model.insert("stage1_block1", std::move(second), diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto unchanged =
      compiler.run<joggle::Module>(*prepare_function, model, false);
  const auto mapped =
      compiler.run<joggle::Module>(*prepare_function, model, true);
  const auto emitted =
      compiler.run<joggle::Bytes>(*compile_function, model, true);
  if (!unchanged || !mapped || !emitted) {
    compiler.diagnostics().print(std::cerr);
  }

  const std::string source = emitted ? decode(*emitted) : std::string{};
  joggle::Diagnostics parse_diagnostics;
  const auto parsed =
      joggle::parse_module(source, parse_diagnostics, "compiled-model.joggle");

  bool ok = true;
  ok &= expect(model.functions().size() == 2U && calls(model, "nn.relu") == 4U,
               "the original multi-Function NN Module remains unchanged");
  ok &= expect(unchanged && calls(*unchanged, "nn.relu") == 4U &&
                   calls(*unchanged, "example_accel.relu") == 0U,
               "a Known false compiler branch returns the original module");
  ok &= expect(mapped && calls(*mapped, "nn.relu") == 0U &&
                   calls(*mapped, "example_accel.relu") == 4U,
               "a Known true compiler branch invokes an ordinary Module "
               "transform");
  ok &= expect(parsed &&
                   source.find("import example_accel@1.0.0;") !=
                       std::string::npos &&
                   source.find("example_accel.relu") != std::string::npos,
               "an ordinary emitter produces canonical, parseable source");
  if (!parse_diagnostics.ok()) {
    parse_diagnostics.print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
