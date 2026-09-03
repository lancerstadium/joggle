#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

void append_f32(joggle::Bytes& output, float value) {
  static_assert(sizeof(float) == sizeof(std::uint32_t));
  const std::uint32_t word = std::bit_cast<std::uint32_t>(value);
  output.push_back(static_cast<std::byte>(word & 0xffU));
  output.push_back(static_cast<std::byte>((word >> 8U) & 0xffU));
  output.push_back(static_cast<std::byte>((word >> 16U) & 0xffU));
  output.push_back(static_cast<std::byte>((word >> 24U) & 0xffU));
}

std::optional<std::vector<float>> decode_f32(const joggle::Bytes& input) {
  if (input.size() % sizeof(float) != 0U) {
    return std::nullopt;
  }
  std::vector<float> result;
  result.reserve(input.size() / sizeof(float));
  for (std::size_t offset = 0; offset < input.size(); offset += sizeof(float)) {
    const std::uint32_t word =
        std::to_integer<std::uint32_t>(input[offset]) |
        (std::to_integer<std::uint32_t>(input[offset + 1U]) << 8U) |
        (std::to_integer<std::uint32_t>(input[offset + 2U]) << 16U) |
        (std::to_integer<std::uint32_t>(input[offset + 3U]) << 24U);
    result.push_back(std::bit_cast<float>(word));
  }
  return result;
}

joggle::Bytes deterministic_input(std::size_t count) {
  joggle::Bytes result;
  result.reserve(count * sizeof(float));
  for (std::size_t index = 0; index < count; ++index) {
    const std::uint64_t pattern =
        (static_cast<std::uint64_t>(index) * 17U + 13U) % 256U;
    const std::int64_t centered = static_cast<std::int64_t>(pattern) - 128;
    append_f32(result, static_cast<float>(centered) / 128.0F);
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
module anchor_numerical@1.0.0 {
  import onnx@2.0.0;
  import anchor@1.0.0;

  fn compile(input: bytes) -> module {
    source = onnx.read(input);
    model = onnx.to_nn(source);
    mapped = anchor.map(model, 8, 8);
    fused = anchor.fuse_relu(mapped);
    return anchor.plan_storage(fused);
  }
}
)",
               "anchor-numerical.joggle");
  if (!compiler.link() ||
      !compiler.load_behavior("onnx", JOGGLE_ONNX_BEHAVIOR) ||
      !compiler.load_behavior("anchor", JOGGLE_ANCHOR_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto source = read(JOGGLE_ANCHOR_ONNX_MODEL);
  const auto expected_bytes = read(JOGGLE_ANCHOR_ONNX_ORACLE);
  const auto program =
      source ? compiler.run<joggle::Module>("anchor_numerical.compile", *source)
             : std::optional<joggle::Module>{};
  const auto members = program ? program->functions()
                               : std::vector<joggle::Module::FunctionDecl>{};
  const joggle::Function* body =
      members.size() == 1U ? members.front().body() : nullptr;
  const auto arguments =
      body ? body->arguments() : std::vector<joggle::Value>{};
  const auto shape =
      arguments.size() == 1U
          ? arguments.front().type().get<std::vector<std::int64_t>>("shape")
          : std::optional<std::vector<std::int64_t>>{};
  std::size_t input_count = 1U;
  if (!shape) {
    input_count = 0U;
  } else {
    for (const std::int64_t dimension : *shape) {
      if (dimension <= 0 ||
          static_cast<std::uint64_t>(dimension) >
              static_cast<std::uint64_t>(
                  std::numeric_limits<std::size_t>::max() / input_count)) {
        input_count = 0U;
        break;
      }
      input_count *= static_cast<std::size_t>(dimension);
    }
  }
  const auto input = deterministic_input(input_count);
  const auto actual_bytes =
      program && input_count != 0U
          ? compiler.run<joggle::Bytes>("anchor.execute_f32", *program, input)
          : std::optional<joggle::Bytes>{};
  joggle::Bytes truncated_input = input;
  if (truncated_input.size() >= sizeof(float)) {
    truncated_input.resize(truncated_input.size() - sizeof(float));
  }
  const auto invalid =
      program ? compiler.run<joggle::Bytes>("anchor.execute_f32", *program,
                                            truncated_input)
              : std::optional<joggle::Bytes>{};
  const auto expected = expected_bytes ? decode_f32(*expected_bytes)
                                       : std::optional<std::vector<float>>{};
  const auto actual = actual_bytes ? decode_f32(*actual_bytes)
                                   : std::optional<std::vector<float>>{};
  if (!program || !body || invalid || !expected || !actual ||
      expected->empty() || expected->size() != actual->size()) {
    compiler.diagnostics().print(std::cerr);
    std::cerr << "invalid numerical result or ONNX Runtime oracle\n";
    return EXIT_FAILURE;
  }

  float maximum_absolute = 0.0F;
  float maximum_scaled = 0.0F;
  std::size_t worst = 0U;
  for (std::size_t index = 0; index < expected->size(); ++index) {
    if (!std::isfinite((*expected)[index]) ||
        !std::isfinite((*actual)[index])) {
      std::cerr << "non-finite numerical result at " << index << '\n';
      return EXIT_FAILURE;
    }
    const float absolute = std::abs((*actual)[index] - (*expected)[index]);
    const float scaled = absolute / (1.0F + std::abs((*expected)[index]));
    maximum_absolute = std::max(maximum_absolute, absolute);
    if (scaled > maximum_scaled) {
      maximum_scaled = scaled;
      worst = index;
    }
  }
  const auto expected_top = static_cast<std::size_t>(std::distance(
      expected->begin(), std::max_element(expected->begin(), expected->end())));
  const auto actual_top = static_cast<std::size_t>(std::distance(
      actual->begin(), std::max_element(actual->begin(), actual->end())));
  std::cout << "outputs " << actual->size() << '\n'
            << "max-absolute-error " << maximum_absolute << '\n'
            << "max-scaled-error " << maximum_scaled << '\n'
            << "worst-index " << worst << '\n'
            << "top-1 " << actual_top << '\n';
  return maximum_scaled <= 0.0001F && expected_top == actual_top ? EXIT_SUCCESS
                                                                 : EXIT_FAILURE;
}
