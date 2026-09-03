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

std::string decode(const joggle::Bytes& bytes) {
  std::string result;
  result.reserve(bytes.size());
  for (const std::byte value : bytes) {
    result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
  }
  return result;
}

std::size_t calls_named(const joggle::Function& function,
                        std::string_view qualified_name) {
  const auto ops = function.ops();
  return static_cast<std::size_t>(std::count_if(
      ops.begin(), ops.end(),
      [qualified_name](const joggle::Op& op) {
        return op.callee().symbol().qualified_name() == qualified_name;
      }));
}

std::size_t event_count(std::string_view trace) {
  constexpr std::string_view prefix = "\nevent ";
  std::size_t result = 0;
  std::size_t offset = 0;
  while ((offset = trace.find(prefix, offset)) != std::string_view::npos) {
    ++result;
    offset += prefix.size();
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
  compiler.load(JOGGLE_ANCHOR_MODULE);
  compiler.add(R"(
joggle 1;
module anchor_pipeline@1.0.0 {
  import onnx@2.0.0;
  import anchor@1.0.0;

  fn map(input: bytes) -> module {
    source = onnx.read(input);
    model = onnx.to_nn(source);
    return anchor.map(model, 8, 8);
  }

  fn compile(input: bytes) -> module {
    return anchor.plan_storage(map(input));
  }

  fn compile_fused(input: bytes) -> module {
    mapped = map(input);
    optimized = anchor.fuse_relu(mapped);
    return anchor.plan_storage(optimized);
  }

  fn deploy(input: bytes, target: type) -> bytes {
    planned = compile(input);
    return anchor.emit(planned, target);
  }

  fn trace(input: bytes, target: type) -> bytes {
    planned = compile(input);
    return anchor.trace(planned, target);
  }
}
)",
               "anchor-pipeline.joggle");
  if (!compiler.link() ||
      !compiler.load_behavior("onnx", JOGGLE_ONNX_BEHAVIOR) ||
      !compiler.load_behavior("anchor", JOGGLE_ANCHOR_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto source = read(JOGGLE_ANCHOR_ONNX_MODEL);
  const auto first = source
                         ? compiler.run<joggle::Module>(
                               "anchor_pipeline.compile", *source)
                         : std::optional<joggle::Module>{};
  const auto second = source
                          ? compiler.run<joggle::Module>(
                                "anchor_pipeline.compile", *source)
                          : std::optional<joggle::Module>{};
  const auto fused = source
                         ? compiler.run<joggle::Module>(
                               "anchor_pipeline.compile_fused", *source)
                         : std::optional<joggle::Module>{};
  const auto target = compiler.module("anchor");
  const auto memory = compiler.module("mem");
  const auto analyze =
      target ? target->function("scratch_bytes") : std::nullopt;
  const auto cycle_model = target ? target->function("cycles") : std::nullopt;
  const auto kernel_report =
      target ? target->function("kernel_report") : std::nullopt;
  const auto emit = target ? target->function("emit") : std::nullopt;
  const auto config = target ? target->type("config") : std::nullopt;
  const auto reference = memory ? memory->interface("reference") : std::nullopt;
  if (!first || !second || !analyze || !cycle_model || !kernel_report ||
      !emit || !config || !reference) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto main = first->function("main");
  const joggle::Function* body = main ? main->body() : nullptr;
  const auto fused_main = fused ? fused->function("main") : std::nullopt;
  const joggle::Function* fused_body =
      fused_main ? fused_main->body() : nullptr;
  const auto scratch = compiler.run<std::int64_t>(*analyze, *first);
  const auto machine = compiler.make(
      *config, std::int64_t{16}, std::int64_t{4}, std::int64_t{32},
      std::int64_t{16}, std::int64_t{16777216}, std::int64_t{4});
  const auto cycles = machine
                          ? compiler.run<std::int64_t>(*cycle_model, *first,
                                                       *machine)
                          : std::optional<std::int64_t>{};
  const auto fused_scratch =
      fused ? compiler.run<std::int64_t>(*analyze, *fused)
            : std::optional<std::int64_t>{};
  const auto fused_cycles = fused && machine
                                ? compiler.run<std::int64_t>(*cycle_model,
                                                             *fused, *machine)
                                : std::optional<std::int64_t>{};
  const auto fused_trace =
      fused && machine
          ? compiler.run<joggle::Bytes>("anchor.trace", *fused, *machine)
          : std::optional<joggle::Bytes>{};
  const auto kernel_summary =
      compiler.run<joggle::Bytes>(*kernel_report, *first);
  const auto fused_kernel_summary =
      fused ? compiler.run<joggle::Bytes>(*kernel_report, *fused)
            : std::optional<joggle::Bytes>{};
  if (!kernel_summary || !fused_kernel_summary) {
    compiler.diagnostics().print(std::cerr);
  }
  const auto fused_trace_again =
      fused && machine
          ? compiler.run<joggle::Bytes>("anchor.trace", *fused, *machine)
          : std::optional<joggle::Bytes>{};
  const auto manifest = machine
                            ? compiler.run<joggle::Bytes>(*emit, *first,
                                                         *machine)
                            : std::optional<joggle::Bytes>{};
  const auto manifest_again = machine
                                  ? compiler.run<joggle::Bytes>(*emit, *first,
                                                               *machine)
                                  : std::optional<joggle::Bytes>{};
  const auto deployed = source && machine
                            ? compiler.run<joggle::Bytes>(
                                  "anchor_pipeline.deploy", *source, *machine)
                            : std::optional<joggle::Bytes>{};
  const auto trace = source && machine
                         ? compiler.run<joggle::Bytes>(
                               "anchor_pipeline.trace", *source, *machine)
                         : std::optional<joggle::Bytes>{};
  const auto trace_again = first && machine
                               ? compiler.run<joggle::Bytes>("anchor.trace",
                                                             *first, *machine)
                               : std::optional<joggle::Bytes>{};
  const std::string emitted = manifest ? decode(*manifest) : std::string{};
  const std::string timeline = trace ? decode(*trace) : std::string{};
  const std::string fused_timeline =
      fused_trace ? decode(*fused_trace) : std::string{};
  const std::string kernels =
      kernel_summary ? decode(*kernel_summary) : std::string{};
  const std::string fused_kernels =
      fused_kernel_summary ? decode(*fused_kernel_summary) : std::string{};
  const std::size_t events = event_count(timeline);
  const std::size_t fused_events = event_count(fused_timeline);
  const std::size_t fusions =
      fused_body ? calls_named(*fused_body, "anchor.conv_relu_nchw") : 0U;
  bool valid = body != nullptr && fused_body != nullptr && scratch &&
               *scratch > 0 && cycles && fused_scratch && fused_cycles &&
               *cycles == 29453374 && manifest && manifest_again &&
               deployed && *manifest == *manifest_again &&
               *manifest == *deployed &&
               emitted.starts_with("anchor 1\nmodule main_graph#") &&
               emitted.find("\nscratch-bytes 10946464\n") !=
                   std::string::npos &&
               emitted.find("\ncycles 29453374\n") != std::string::npos &&
               emitted.ends_with(joggle::format(*first)) &&
               trace && trace_again && *trace == *trace_again &&
               timeline.starts_with(
                   "anchor timeline 1\nmodule main_graph#") &&
               timeline.find("\nscratch-bytes 10946464\n") !=
                   std::string::npos &&
               timeline.find("\ncycles 29453374\n") != std::string::npos &&
               events == 49U &&
               fused_trace && fused_trace_again &&
               *fused_trace == *fused_trace_again &&
               fused_timeline.starts_with(
                   "anchor timeline 1\nmodule main_graph#") &&
               fused_timeline.find("\ncycles " +
                                   std::to_string(*fused_cycles) + "\n") !=
                   std::string::npos &&
               fused_events == 40U &&
               *fused_cycles == 29161690 &&
               *fused_scratch == 7735200 &&
               fused_body->ops().size() == 122U &&
               fusions == 9U && kernel_summary && fused_kernel_summary &&
               kernels.starts_with(
                   "anchor kernel closure 1\nmodule main_graph#") &&
               kernels.find("\nroot-calls 49\n") != std::string::npos &&
               kernels.find("\nsource-specializations 35\n") !=
                   std::string::npos &&
               kernels.find("\nprimitive-sites 1547\n") !=
                   std::string::npos &&
               kernels.find("\nmax-source-depth 2\n") !=
                   std::string::npos &&
               fused_kernels.starts_with(
                   "anchor kernel closure 1\nmodule main_graph#") &&
               fused_kernels.find("\nroot-calls 40\n") !=
                   std::string::npos &&
               fused_kernels.find("\nsource-specializations 42\n") !=
                   std::string::npos &&
               fused_kernels.find("\nprimitive-sites 1609\n") !=
                   std::string::npos &&
               fused_kernels.find("\nmax-source-depth 3\n") !=
                   std::string::npos &&
               body->ops().size() == 140U && first->data().size() == 42U &&
               first->digest() == second->digest() &&
               first->data() == second->data();
  if (body != nullptr) {
    const auto arguments = body->arguments();
    valid = valid && arguments.size() == 1U &&
            compiler.conforms(arguments.front().type().schema(), *reference);
    for (const auto& op : body->ops()) {
      const auto results = op.results();
      valid = valid &&
              op.callee().symbol().module_name() == "anchor" &&
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
            << "scratch-bytes " << (scratch ? *scratch : 0) << '\n'
            << "cycles " << (cycles ? *cycles : 0) << '\n'
            << "events " << events << '\n'
            << "fused-ops " << (fused_body ? fused_body->ops().size() : 0U)
            << '\n'
            << "fusions " << fusions << '\n'
            << "fused-events " << fused_events << '\n'
            << "fused-scratch-bytes "
            << (fused_scratch ? *fused_scratch : 0) << '\n'
            << "fused-cycles " << (fused_cycles ? *fused_cycles : 0) << '\n'
            << (kernel_summary ? kernels : std::string{})
            << (fused_kernel_summary ? fused_kernels : std::string{})
            << "manifest-bytes " << (manifest ? manifest->size() : 0U)
            << '\n';
  return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
