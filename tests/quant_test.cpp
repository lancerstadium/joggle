#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

using Integers = std::vector<std::int64_t>;
using Reals = std::vector<double>;
using Shape = std::vector<std::int64_t>;

constexpr std::string_view source = R"(
joggle 1;

mod quant_fixture@1.0.0 {
  import quant@1 as q;

  fn roundtrip(input: bytes) -> bytes {
    encoded = @q.quantize(input, [0.5], [128], [3], 0, u8);
    return @q.dequantize(encoded, [0.5], [128], [3], 0, u8);
  }
}
)";

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

void append_u32(joggle::Bytes& output, std::uint32_t value) {
  for (std::size_t byte = 0; byte < 4U; ++byte) {
    output.push_back(static_cast<std::byte>((value >> (byte * 8U)) & 0xffU));
  }
}

joggle::Bytes f32_bytes(std::initializer_list<float> values) {
  joggle::Bytes output;
  output.reserve(values.size() * 4U);
  for (const float value : values) {
    append_u32(output, std::bit_cast<std::uint32_t>(value));
  }
  return output;
}

joggle::Bytes i32_bytes(std::initializer_list<std::int32_t> values) {
  joggle::Bytes output;
  output.reserve(values.size() * 4U);
  for (const auto value : values) {
    append_u32(output, std::bit_cast<std::uint32_t>(value));
  }
  return output;
}

bool bytes_equal(const std::optional<joggle::Bytes>& actual,
                 std::initializer_list<unsigned int> expected) {
  return actual && actual->size() == expected.size() &&
         std::equal(actual->begin(), actual->end(), expected.begin(),
                    [](std::byte lhs, unsigned int rhs) {
                      return std::to_integer<unsigned int>(lhs) == rhs;
                    });
}

bool load(joggle::Compiler& compiler, bool fixture = false) {
  compiler.load(JOGGLE_QUANT_MOD);
  if (fixture) {
    compiler.add(source, "quant-fixture.joggle");
  }
  return compiler.link() && compiler.load_native("quant", JOGGLE_QUANT_NATIVE);
}

template <typename Invoke> bool rejects(Invoke&& invoke) {
  joggle::Compiler compiler;
  if (!load(compiler)) {
    return false;
  }
  return !invoke(compiler) && !compiler.ok();
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  if (!load(compiler, true)) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto u8 = compiler.make("u8");
  const auto i8 = compiler.make("i8");
  const auto i32 = compiler.make("i32");
  if (!u8 || !i8 || !i32) {
    return EXIT_FAILURE;
  }

  const auto ties = compiler.run<joggle::Bytes>(
      "quant.quantize", f32_bytes({-2.5F, -1.5F, -0.5F, 0.5F, 1.5F, 2.5F}),
      Reals{1.0}, Integers{0}, Shape{6}, std::int64_t{0}, *i8);
  const auto saturation = compiler.run<joggle::Bytes>(
      "quant.quantize",
      f32_bytes({-std::numeric_limits<float>::infinity(), -1000.0F, -1.0F, 0.0F,
                 1.0F, 1000.0F, std::numeric_limits<float>::infinity()}),
      Reals{0.5}, Integers{128}, Shape{7}, std::int64_t{0}, *u8);

  const auto per_axis_input = f32_bytes({0.0F, 0.0F, 0.0F, 1.0F, 2.0F, 4.0F});
  const auto per_axis = compiler.run<joggle::Bytes>(
      "quant.quantize", per_axis_input, Reals{0.5, 1.0, 2.0},
      Integers{0, 1, -1}, Shape{2, 3}, std::int64_t{-1}, *i8);
  const auto per_axis_decoded =
      per_axis ? compiler.run<joggle::Bytes>(
                     "quant.dequantize", *per_axis, Reals{0.5, 1.0, 2.0},
                     Integers{0, 1, -1}, Shape{2, 3}, std::int64_t{-1}, *i8)
               : std::nullopt;
  const auto i32_decoded = compiler.run<joggle::Bytes>(
      "quant.dequantize", i32_bytes({-2, 0, 3}), Reals{0.25}, Integers{0},
      Shape{3}, std::int64_t{0}, *i32);
  const auto staged = compiler.run<joggle::Bytes>(
      "quant_fixture.roundtrip", f32_bytes({-1.0F, 0.0F, 1.0F}));
  const auto repeated = compiler.run<joggle::Bytes>(
      "quant.quantize", per_axis_input, Reals{0.5, 1.0, 2.0},
      Integers{0, 1, -1}, Shape{2, 3}, std::int64_t{-1}, *i8);

  bool ok = true;
  ok &= expect(
      bytes_equal(ties, {0xfeU, 0xfeU, 0x00U, 0x00U, 0x02U, 0x02U}),
      "quantize uses round-to-nearest-even for positive and negative ties");
  ok &= expect(bytes_equal(saturation,
                           {0x00U, 0x00U, 0x7eU, 0x80U, 0x82U, 0xffU, 0xffU}),
               "u8 quantization saturates finite and infinite f32 values");
  ok &= expect(
      bytes_equal(per_axis, {0x00U, 0x01U, 0xffU, 0x02U, 0x03U, 0x01U}) &&
          per_axis_decoded == per_axis_input && repeated == per_axis,
      "negative-axis parameters broadcast in row-major order "
      "deterministically");
  ok &= expect(i32_decoded == f32_bytes({-0.5F, 0.0F, 0.75F}),
               "i32 dequantization uses a zero point of zero and f32 output");
  ok &= expect(staged == f32_bytes({-1.0F, 0.0F, 1.0F}),
               "the bytes overloads compose through explicit source staging");

  const bool rejects_zero_scale = rejects([&](joggle::Compiler& invalid) {
    const auto storage = invalid.make("u8");
    return storage && invalid.run<joggle::Bytes>(
                          "quant.quantize", f32_bytes({1.0F}), Reals{0.0},
                          Integers{0}, Shape{1}, std::int64_t{0}, *storage);
  });
  const bool rejects_bad_axis = rejects([&](joggle::Compiler& invalid) {
    const auto storage = invalid.make("i8");
    return storage &&
           invalid.run<joggle::Bytes>("quant.quantize", f32_bytes({0.0F, 0.0F}),
                                      Reals{1.0, 1.0}, Integers{0, 0}, Shape{2},
                                      std::int64_t{1}, *storage);
  });
  const bool rejects_i32_zero = rejects([&](joggle::Compiler& invalid) {
    const auto storage = invalid.make("i32");
    return storage && invalid.run<joggle::Bytes>(
                          "quant.dequantize", i32_bytes({1}), Reals{1.0},
                          Integers{1}, Shape{1}, std::int64_t{0}, *storage);
  });
  const bool rejects_nan = rejects([&](joggle::Compiler& invalid) {
    const auto storage = invalid.make("u8");
    return storage &&
           invalid.run<joggle::Bytes>(
               "quant.quantize",
               f32_bytes({std::numeric_limits<float>::quiet_NaN()}), Reals{1.0},
               Integers{0}, Shape{1}, std::int64_t{0}, *storage);
  });
  ok &= expect(rejects_zero_scale && rejects_bad_axis && rejects_i32_zero &&
                   rejects_nan,
               "invalid scales, axes, zero points, and NaNs fail closed");

  if (!ok) {
    compiler.diag().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
