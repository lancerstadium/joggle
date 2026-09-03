#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include <joggle/joggle.h>

namespace {

std::string decode(const joggle::Bytes& input) {
  std::string result;
  result.reserve(input.size());
  for (const std::byte value : input) {
    result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
  }
  return result;
}

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_MEM_MODULE);
  compiler.load(JOGGLE_ANCHOR_MODULE);
  compiler.add(R"(
joggle 1;
module user_kernel@1.0.0 {
  import arith@2.0.0;
  import anchor@1.0.0;
  import mem@1.0.0;

  fn square(
    input: anchor.ref<f32, [1, 4], anchor.linear, anchor.io>
  ) -> anchor.ref<f32, [1, 4], anchor.linear, anchor.local> {
    output: anchor.ref<f32, [1, 4], anchor.linear, anchor.local> = mem.alloc();
    for offset: index in range(4) {
      value = anchor.load(input, offset);
      anchor.store(output, offset, value * value);
    }
    return output;
  }
}
)",
               "user-kernel.joggle");
  compiler.add(R"(
joggle 1;
module user_model@1.0.0 {
  import anchor@1.0.0;
  import user_kernel@1.0.0;

  fn main(
    input: anchor.ref<f32, [1, 4], anchor.linear, anchor.io>
  ) -> anchor.ref<f32, [1, 4], anchor.linear, anchor.local> {
    return user_kernel.square(input);
  }
}
)",
               "user-model.joggle");
  compiler.add(R"(
joggle 1;
module opaque_kernel@1.0.0 {
  import anchor@1.0.0;

  fn opaque(
    input: anchor.ref<f32, [1, 4], anchor.linear, anchor.io>
  ) -> anchor.ref<f32, [1, 4], anchor.linear, anchor.local>;

  fn main(
    input: anchor.ref<f32, [1, 4], anchor.linear, anchor.io>
  ) -> anchor.ref<f32, [1, 4], anchor.linear, anchor.local> {
    return opaque(input);
  }
}
)",
               "opaque-kernel.joggle");
  if (!compiler.link() ||
      !compiler.load_behavior("anchor", JOGGLE_ANCHOR_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto source = compiler.materialize("user_model.main");
  const auto opaque = compiler.materialize("opaque_kernel.main");
  const auto anchor = compiler.module("anchor");
  const auto report = anchor ? anchor->function("kernel_report")
                             : std::optional<joggle::Module::FunctionDecl>{};
  joggle::Diagnostics diagnostics;
  joggle::Module program("user_program", {1, 0, 0});
  joggle::Module bad_program("opaque_program", {1, 0, 0});
  if (!source || !opaque || !report ||
      !program.insert("main", *source, diagnostics) ||
      !bad_program.insert("main", *opaque, diagnostics)) {
    diagnostics.print(std::cerr);
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto summary = compiler.run<joggle::Bytes>(*report, program);
  const auto rejected = compiler.run<joggle::Bytes>(*report, bad_program);
  const std::string text = summary ? decode(*summary) : std::string{};
  bool ok = true;
  ok &= expect(summary.has_value(),
               "a source-defined user kernel closes over shared primitives");
  ok &= expect(
      text.starts_with("anchor kernel closure 1\nmodule user_program#") &&
          text.find("\nroot-calls 1\n") != std::string::npos &&
          text.find("\nsource-specializations 1\n") != std::string::npos &&
          text.find("\nprimitive-sites ") != std::string::npos &&
          text.find("\nmax-source-depth 1\n") != std::string::npos,
      "the closure report is deterministic and specialization-aware");
  ok &= expect(!rejected,
               "an opaque user operation fails the kernel-closure gate");
  if (!ok) {
    compiler.diagnostics().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
