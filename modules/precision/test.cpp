#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

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
  const joggle::Bytes source_payload =
      float_bytes({0x00000000U, 0x80000000U, 0x3f800000U, 0xc0000000U,
                   0x7f800000U, 0xff800000U, 0x7fc00000U, 0x477fe000U,
                   0x38800000U, 0x33800000U, 0x3f801000U,
                   0x3f803000U});
  const std::string raw(reinterpret_cast<const char*>(source_payload.data()),
                        source_payload.size());
  const std::string source_name = "sha256:" + joggle::sha256(raw);
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_PRECISION_MODULE);
  compiler.add(std::string(R"(
joggle 1;
module precision_model@1.0.0 {
  import nn@2.0.0;
  import tensor@2.0.0;

  fn main(input: tensor.ranked<f32, [1, 12]>)
    -> tensor.ranked<f32, [1, 12]> {
    weight: tensor.ranked<f32, [1, 12]> = tensor.constant(
      resource: ")") + source_name + R"("
    );
    return nn.add(input, weight);
  }

  fn select(condition: i1, input: tensor.ranked<f32, [1, 12]>)
    -> tensor.ranked<f32, [1, 12]> {
    if condition {
      return nn.relu(input);
    } else {
      return input;
    }
  }
}
)",
               "precision-model.joggle");
  compiler.add(R"(
joggle 1;
module precision_pipeline@1.0.0 {
  import precision@1.0.0;

  fn run(input: module) -> module {
    return precision.f32_to_f16(input);
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
  auto select = compiler.materialize("precision_model.select");
  joggle::Module model("precision_model", {1, 0, 0});
  joggle::Diagnostics diagnostics;
  if (!main || !select || !model.insert("main", std::move(*main), diagnostics) ||
      !model.insert("select", std::move(*select), diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  if (model.store(source_payload) != source_name) {
    return EXIT_FAILURE;
  }
  using Result = joggle::Module;
  const auto first =
      compiler.run<Result>("precision_pipeline.run", model);
  const auto second =
      compiler.run<Result>("precision_pipeline.run", model);
  if (!first || !second) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto& converted = *first;
  const auto declaration = converted.function("main");
  const auto select_declaration = converted.function("select");
  const auto* select_body =
      select_declaration ? select_declaration->body() : nullptr;
  const auto* body = declaration ? declaration->body() : nullptr;
  const auto ops = body ? body->ops()
                                 : std::vector<joggle::Op>{};
  const auto result_type = body ? body->result_types().front()
                                : std::optional<joggle::Type>{};
  const auto element = result_type
                           ? result_type->get<joggle::Type>("element")
                           : std::optional<joggle::Type>{};
  const auto resource = !ops.empty()
                            ? ops.front().property<std::string>("resource")
                            : std::optional<std::string>{};
  const auto payload = resource ? converted.data(*resource) : std::nullopt;

  bool ok = true;
  ok &= expect(body && body->arguments().front().type().get<joggle::Type>(
                           "element") == element &&
                   element &&
                   element->schema().symbol().qualified_name() ==
                       "prelude.f16" &&
                   ops.size() == 2U && resource &&
                   resource->starts_with("sha256:") &&
                   *resource != source_name,
               "f32 tensor signatures, constants, and calls become f16");
  const joggle::Bytes expected{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80},
      std::byte{0x00}, std::byte{0x3c}, std::byte{0x00}, std::byte{0xc0},
      std::byte{0x00}, std::byte{0x7c}, std::byte{0x00}, std::byte{0xfc},
      std::byte{0x00}, std::byte{0x7e}, std::byte{0xff}, std::byte{0x7b},
      std::byte{0x00}, std::byte{0x04}, std::byte{0x01}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x3c}, std::byte{0x02}, std::byte{0x3c}};
  ok &= expect(converted.data().size() == 1U &&
                   !converted.data(source_name) && payload &&
                   joggle::Bytes(payload->begin(), payload->end()) == expected,
               "f32 payloads use deterministic IEEE binary16 encoding");
  ok &= expect(converted.digest() == second->digest() &&
                   converted.data() == second->data(),
               "precision conversion is deterministic");
  ok &= expect(
      select_body && select_body->blocks().size() == 3U &&
          select_body->arguments()[1].type().get<joggle::Type>("element") ==
              element &&
          select_body->result_types().front().get<joggle::Type>("element") ==
              element,
      "precision conversion preserves residual CFG while mapping tensor types");
  auto missing_main = compiler.materialize("precision_model.main");
  joggle::Module missing("missing", {1, 0, 0});
  joggle::Diagnostics missing_diagnostics;
  if (!missing_main ||
      !missing.insert("main", std::move(*missing_main), missing_diagnostics)) {
    return EXIT_FAILURE;
  }
  const auto rejected = compiler.run<Result>("precision_pipeline.run", missing);
  const bool reports_missing = std::any_of(
      compiler.diagnostics().entries().begin(),
      compiler.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("missing Module data") !=
               std::string::npos;
      });
  ok &= expect(!rejected && reports_missing,
               "a missing Module payload rejects the whole conversion");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
