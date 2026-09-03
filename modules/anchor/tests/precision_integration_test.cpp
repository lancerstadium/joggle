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
    if (value == std::byte{0}) {
      break;
    }
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

bool is_f16_reference(
    joggle::Compiler& compiler, const joggle::Type& type,
    const joggle::Module::InterfaceDecl& reference) {
  if (!compiler.conforms(type.schema(), reference)) {
    return false;
  }
  const auto element = type.get<joggle::Type>("element_type");
  return element &&
         element->schema().symbol().qualified_name() == "prelude.f16";
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_MEM_MODULE);
  compiler.load(JOGGLE_ONNX_MODULE);
  compiler.load(JOGGLE_PRECISION_MODULE);
  compiler.load(JOGGLE_ANCHOR_MODULE);
  compiler.add(R"(
joggle 1;
module anchor_precision_pipeline@1.0.0 {
  import onnx@2.0.0;
  import precision@1.0.0;
  import anchor@1.0.0;

  fn compile_early(input: bytes) -> module {
    source = onnx.read(input);
    reduced = precision.f32_to_f16(source);
    model = onnx.to_nn(reduced);
    mapped = anchor.map(model, 8, 8);
    optimized = anchor.fuse_relu(mapped);
    return anchor.plan_storage(optimized);
  }

  fn compile_late(input: bytes) -> module {
    source = onnx.read(input);
    model = onnx.to_nn(source);
    reduced = precision.f32_to_f16(model);
    mapped = anchor.map(reduced, 8, 8);
    optimized = anchor.fuse_relu(mapped);
    return anchor.plan_storage(optimized);
  }
}
)",
               "anchor-precision-pipeline.joggle");
  if (!compiler.link() ||
      !compiler.load_behavior("onnx", JOGGLE_ONNX_BEHAVIOR) ||
      !compiler.load_behavior("precision", JOGGLE_PRECISION_BEHAVIOR) ||
      !compiler.load_behavior("anchor", JOGGLE_ANCHOR_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto source = read(JOGGLE_ANCHOR_PRECISION_ONNX_MODEL);
  const auto early = source
                         ? compiler.run<joggle::Module>(
                               "anchor_precision_pipeline.compile_early",
                               *source)
                         : std::optional<joggle::Module>{};
  const auto late = source
                        ? compiler.run<joggle::Module>(
                              "anchor_precision_pipeline.compile_late",
                              *source)
                        : std::optional<joggle::Module>{};
  const auto target = compiler.module("anchor");
  const auto memory = compiler.module("mem");
  const auto scratch_function =
      target ? target->function("scratch_bytes") : std::nullopt;
  const auto cycle_function =
      target ? target->function("cycles") : std::nullopt;
  const auto bundle_function =
      target ? target->function("bundle") : std::nullopt;
  const auto unpack_function =
      target ? target->function("unpack") : std::nullopt;
  const auto emit_function =
      target ? target->function("emit") : std::nullopt;
  const auto config = target ? target->type("config") : std::nullopt;
  const auto reference =
      memory ? memory->interface("reference") : std::nullopt;
  if (!early || !late || !scratch_function || !cycle_function ||
      !bundle_function || !unpack_function || !emit_function || !config ||
      !reference) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto main = early->function("main");
  const joggle::Function* body = main ? main->body() : nullptr;
  const auto scratch =
      compiler.run<std::int64_t>(*scratch_function, *early);
  const auto machine = compiler.make(
      *config, std::int64_t{16}, std::int64_t{4}, std::int64_t{32},
      std::int64_t{16}, std::int64_t{4194304}, std::int64_t{4});
  const auto cycles = machine
                          ? compiler.run<std::int64_t>(*cycle_function, *early,
                                                       *machine)
                          : std::optional<std::int64_t>{};
  const auto trace = machine
                         ? compiler.run<joggle::Bytes>("anchor.trace", *early,
                                                       *machine)
                         : std::optional<joggle::Bytes>{};
  const auto trace_again = machine
                               ? compiler.run<joggle::Bytes>("anchor.trace",
                                                             *late, *machine)
                               : std::optional<joggle::Bytes>{};
  const auto bundled =
      compiler.run<joggle::Module>(*bundle_function, *early);
  const auto manifest = machine
                            ? compiler.run<joggle::Bytes>(*emit_function,
                                                         *early, *machine)
                            : std::optional<joggle::Bytes>{};
  const auto unpacked = manifest
                            ? compiler.run<joggle::Module>(*unpack_function,
                                                           *manifest)
                            : std::optional<joggle::Module>{};
  if (body == nullptr || !scratch || !machine || !cycles || !trace ||
      !trace_again || !bundled || !manifest || !unpacked) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto arguments = body->arguments();
  bool references_are_f16 =
      std::all_of(arguments.begin(), arguments.end(),
                  [&](const joggle::Value& value) {
                    return is_f16_reference(compiler, value.type(), *reference);
                  });
  for (const auto& op : body->ops()) {
    for (const auto& result : op.results()) {
      references_are_f16 =
          references_are_f16 &&
          is_f16_reference(compiler, result.type(), *reference);
    }
  }

  std::size_t payload_bytes = 0;
  for (const auto& name : early->data()) {
    const auto payload = early->data(name);
    payload_bytes += payload ? payload->size() : 0U;
  }
  const std::string timeline = decode(*trace);
  const std::string emitted = decode(*manifest);
  const std::size_t fusions =
      calls_named(*body, "anchor.conv_relu_nchw");
  const std::size_t events = event_count(timeline);
  const bool valid =
      early->digest() == late->digest() && early->data() == late->data() &&
      joggle::format(*early) == joggle::format(*late) &&
      early->data().size() == 42U && payload_bytes == 23369424U &&
      body->ops().size() == 122U && fusions == 9U && events == 40U &&
      references_are_f16 && *scratch == 3867600 && *cycles == 28848157 &&
      *trace == *trace_again &&
      timeline.starts_with("anchor timeline 1\nmodule main_graph#") &&
      timeline.find("\nscratch-bytes " + std::to_string(*scratch) + "\n") !=
          std::string::npos &&
      timeline.find("\ncycles " + std::to_string(*cycles) + "\n") !=
          std::string::npos &&
      emitted.find("\nscratch-bytes " + std::to_string(*scratch) + "\n") !=
          std::string::npos &&
      emitted.find("\ncycles " + std::to_string(*cycles) + "\n") !=
          std::string::npos &&
      bundled->functions().size() == 43U &&
      bundled->data() == early->data() &&
      unpacked->digest() == bundled->digest() &&
      unpacked->data() == bundled->data() &&
      joggle::format(*unpacked) == joggle::format(*bundled) &&
      emitted.starts_with("anchor 3\nsource main_graph#") &&
      emitted.ends_with(joggle::format(*bundled));

  std::cout << "module " << early->name() << '#' << early->digest() << '\n'
            << "ops " << body->ops().size() << '\n'
            << "fusions " << fusions << '\n'
            << "events " << events << '\n'
            << "resources " << early->data().size() << '\n'
            << "resource-bytes " << payload_bytes << '\n'
            << "scratch-bytes " << *scratch << '\n'
            << "cycles " << *cycles << '\n'
            << "manifest-bytes " << manifest->size() << '\n';
  return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
