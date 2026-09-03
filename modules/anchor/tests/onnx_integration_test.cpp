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

  fn compile(input: bytes) -> module {
    source = onnx.read(input);
    model = onnx.to_nn(source);
    mapped = anchor.map(model, 8, 8);
    return anchor.plan_storage(mapped);
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
  const auto target = compiler.module("anchor");
  const auto memory = compiler.module("mem");
  const auto analyze =
      target ? target->function("scratch_bytes") : std::nullopt;
  const auto cycle_model = target ? target->function("cycles") : std::nullopt;
  const auto emit = target ? target->function("emit") : std::nullopt;
  const auto config = target ? target->type("config") : std::nullopt;
  const auto reference = memory ? memory->interface("reference") : std::nullopt;
  if (!first || !second || !analyze || !cycle_model || !emit || !config ||
      !reference) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto main = first->function("main");
  const joggle::Function* body = main ? main->body() : nullptr;
  const auto scratch = compiler.run<std::int64_t>(*analyze, *first);
  const auto machine = compiler.make(
      *config, std::int64_t{16}, std::int64_t{4}, std::int64_t{32},
      std::int64_t{16}, std::int64_t{16777216}, std::int64_t{4});
  const auto cycles = machine
                          ? compiler.run<std::int64_t>(*cycle_model, *first,
                                                       *machine)
                          : std::optional<std::int64_t>{};
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
  bool valid = body != nullptr && scratch && *scratch > 0 && cycles &&
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
               timeline.find("\nevents ") != std::string::npos &&
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
            << "manifest-bytes " << (manifest ? manifest->size() : 0U)
            << '\n';
  return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
