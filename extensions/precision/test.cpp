#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

joggle::Bytes float_bytes(std::initializer_list<std::uint32_t> values) {
  joggle::Bytes result;
  for (const std::uint32_t value : values) {
    for (std::uint32_t shift = 0; shift < 32U; shift += 8U) {
      result.push_back(static_cast<std::byte>(value >> shift));
    }
  }
  return result;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_RESOURCE_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_PRECISION_MODULE);
  compiler.add(R"(
joggle 1;
module precision_model@1.0.0 {
  import nn@2.0.0;
  import tensor@2.0.0;

  fn main(input: tensor.ranked<f32, [1, 12]>)
    -> tensor.ranked<f32, [1, 12]> {
    weight: tensor.ranked<f32, [1, 12]> = tensor.constant(
      resource: "sha256:source"
    );
    return nn.add(input, weight);
  }
}
)",
               "precision-model.joggle");
  compiler.add(R"(
joggle 1;
module precision_pipeline@1.0.0 {
  import precision@1.0.0;
  import resource@1.0.0;

  fn run(input: module, resources: resource.set)
    -> (module, resource.set) {
    output, converted = precision.f32_to_f16(input, resources);
    return output, converted;
  }
}
)",
               "precision-pipeline.joggle");
  if (!compiler.link() ||
      !compiler.load_behavior("precision", JOGGLE_PRECISION_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  auto main = compiler.materialize("precision_model.main");
  joggle::Module model("precision_model", {1, 0, 0});
  joggle::Diagnostics diagnostics;
  if (!main || !model.insert("main", std::move(*main), diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  joggle::ResourceSet resources{
      {"sha256:source",
       float_bytes({0x00000000U, 0x80000000U, 0x3f800000U, 0xc0000000U,
                    0x7f800000U, 0xff800000U, 0x7fc00000U, 0x477fe000U,
                    0x38800000U, 0x33800000U, 0x3f801000U,
                    0x3f803000U})}};
  using Result = std::tuple<joggle::Module, joggle::ResourceSet>;
  const auto first =
      compiler.run<Result>("precision_pipeline.run", model, resources);
  const auto second =
      compiler.run<Result>("precision_pipeline.run", model, resources);
  if (!first || !second) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto& [converted, converted_resources] = *first;
  const auto declaration = converted.function("main");
  const auto* body = declaration ? declaration->body() : nullptr;
  const auto instructions = body ? body->instructions()
                                 : std::vector<joggle::Instruction>{};
  const auto result_type = body ? body->result_types().front()
                                : std::optional<joggle::Type>{};
  const auto element = result_type
                           ? result_type->get<joggle::Type>("element")
                           : std::optional<joggle::Type>{};
  const auto resource = !instructions.empty()
                            ? instructions.front().get<std::string>("resource")
                            : std::optional<std::string>{};
  const auto payload = resource ? converted_resources.find(*resource)
                                : converted_resources.end();

  bool ok = true;
  ok &= expect(body && body->arguments().front().type().get<joggle::Type>(
                           "element") == element &&
                   element &&
                   element->schema().symbol().qualified_name() ==
                       "prelude.f16" &&
                   instructions.size() == 2U && resource &&
                   resource->starts_with("sha256:") &&
                   *resource != "sha256:source",
               "f32 tensor signatures, constants, and calls become f16");
  ok &= expect(converted_resources.size() == 1U &&
                   !converted_resources.contains("sha256:source") &&
                   payload != converted_resources.end() &&
                   payload->second ==
                       joggle::Bytes{
                           std::byte{0x00}, std::byte{0x00},
                           std::byte{0x00}, std::byte{0x80},
                           std::byte{0x00}, std::byte{0x3c},
                           std::byte{0x00}, std::byte{0xc0},
                           std::byte{0x00}, std::byte{0x7c},
                           std::byte{0x00}, std::byte{0xfc},
                           std::byte{0x00}, std::byte{0x7e},
                           std::byte{0xff}, std::byte{0x7b},
                           std::byte{0x00}, std::byte{0x04},
                           std::byte{0x01}, std::byte{0x00},
                           std::byte{0x00}, std::byte{0x3c},
                           std::byte{0x02}, std::byte{0x3c}},
               "f32 payloads use deterministic IEEE binary16 encoding");
  ok &= expect(converted.digest() == std::get<0>(*second).digest() &&
                   converted_resources == std::get<1>(*second),
               "precision conversion is deterministic");
  joggle::ResourceSet wrong_size{
      {"sha256:source", float_bytes({0x3f800000U})}};
  const auto rejected =
      compiler.run<Result>("precision_pipeline.run", model, wrong_size);
  const bool reports_size = std::any_of(
      compiler.diagnostics().entries().begin(),
      compiler.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("resource size disagrees") !=
               std::string::npos;
      });
  ok &= expect(!rejected && reports_size,
               "payload/type disagreement rejects the whole conversion");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
