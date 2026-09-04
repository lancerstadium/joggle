#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

namespace {

using Integers = std::vector<std::int64_t>;
using Reals = std::vector<double>;
using Shape = std::vector<std::int64_t>;

enum class Storage {
  U8,
  I8,
  I32,
};

bool fail(joggle::Diagnostics& diagnostics, std::string message) {
  diagnostics.report("quant reference: " + std::move(message));
  return false;
}

std::optional<Storage>
storage(const joggle::Type& type, bool quantizing,
        joggle::Diagnostics& diagnostics) {
  const auto symbol = type.schema().symbol();
  if (symbol.module_name() != "prelude") {
    fail(diagnostics, "storage must be a Prelude integer Type");
    return std::nullopt;
  }
  if (symbol.local_name() == "u8") {
    return Storage::U8;
  }
  if (symbol.local_name() == "i8") {
    return Storage::I8;
  }
  if (!quantizing && symbol.local_name() == "i32") {
    return Storage::I32;
  }
  fail(diagnostics,
       quantizing ? "quantize storage must be u8 or i8"
                  : "dequantize storage must be u8, i8, or i32");
  return std::nullopt;
}

std::optional<std::size_t>
element_count(const Shape& shape, joggle::Diagnostics& diagnostics) {
  std::size_t count = 1;
  for (const auto dimension : shape) {
    if (dimension < 0) {
      fail(diagnostics, "shape dimensions must be non-negative");
      return std::nullopt;
    }
    const auto extent = static_cast<std::uint64_t>(dimension);
    if (extent > std::numeric_limits<std::size_t>::max() ||
        (extent != 0U &&
         count > std::numeric_limits<std::size_t>::max() /
                     static_cast<std::size_t>(extent))) {
      fail(diagnostics, "shape element count overflows");
      return std::nullopt;
    }
    count *= static_cast<std::size_t>(extent);
  }
  return count;
}

std::optional<std::size_t>
byte_count(std::size_t elements, std::size_t width,
           joggle::Diagnostics& diagnostics) {
  if (elements > std::numeric_limits<std::size_t>::max() / width) {
    fail(diagnostics, "tensor byte count overflows");
    return std::nullopt;
  }
  return elements * width;
}

struct Parameters {
  std::vector<float> scales;
  Integers zeros;
  std::size_t axis_stride = 0;
  std::size_t axis_extent = 0;

  std::size_t at(std::size_t linear) const {
    return scales.size() == 1U
               ? 0U
               : (linear / axis_stride) % axis_extent;
  }
};

std::optional<Parameters>
parameters(const Reals& scales, const Integers& zeros, const Shape& shape,
           std::int64_t axis, joggle::Diagnostics& diagnostics) {
  if (scales.empty() || scales.size() != zeros.size()) {
    fail(diagnostics, "scale and zero lists must have the same non-zero size");
    return std::nullopt;
  }
  if (std::any_of(shape.begin(), shape.end(),
                  [](std::int64_t extent) { return extent < 0; })) {
    fail(diagnostics, "shape dimensions must be non-negative");
    return std::nullopt;
  }

  Parameters result;
  result.zeros = zeros;
  result.scales.reserve(scales.size());
  for (const double value : scales) {
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(value) || !std::isfinite(narrowed) ||
        narrowed <= 0.0F) {
      fail(diagnostics, "every scale must be positive finite f32");
      return std::nullopt;
    }
    result.scales.push_back(narrowed);
  }

  if (scales.size() == 1U) {
    return result;
  }
  auto normalized = axis;
  if (normalized < 0) {
    normalized += static_cast<std::int64_t>(shape.size());
  }
  if (normalized < 0 ||
      normalized >= static_cast<std::int64_t>(shape.size())) {
    fail(diagnostics, "per-axis quantization axis is out of range");
    return std::nullopt;
  }
  const auto index = static_cast<std::size_t>(normalized);
  if (shape[index] < 0 ||
      static_cast<std::uint64_t>(shape[index]) != scales.size()) {
    fail(diagnostics, "per-axis parameter count must equal the axis extent");
    return std::nullopt;
  }
  result.axis_extent = static_cast<std::size_t>(shape[index]);
  result.axis_stride = 1;
  for (std::size_t dimension = index + 1U; dimension < shape.size();
       ++dimension) {
    const auto extent = static_cast<std::size_t>(shape[dimension]);
    if (extent != 0U &&
        result.axis_stride >
            std::numeric_limits<std::size_t>::max() / extent) {
      fail(diagnostics, "per-axis stride overflows");
      return std::nullopt;
    }
    result.axis_stride *= extent;
  }
  return result;
}

bool zeros_fit(const Integers& zeros, Storage format, bool quantizing,
               joggle::Diagnostics& diagnostics) {
  const auto in_range = [format](std::int64_t value) {
    switch (format) {
      case Storage::U8:
        return value >= 0 && value <= 255;
      case Storage::I8:
        return value >= -128 && value <= 127;
      case Storage::I32:
        return value == 0;
    }
    return false;
  };
  if (!std::all_of(zeros.begin(), zeros.end(), in_range)) {
    fail(diagnostics,
         format == Storage::I32 && !quantizing
             ? "i32 dequantization requires a zero point of zero"
             : "zero point is outside its storage Type");
    return false;
  }
  return true;
}

float read_f32(const joggle::Bytes& input, std::size_t offset) {
  std::uint32_t bits = 0;
  for (std::size_t byte = 0; byte < 4U; ++byte) {
    bits |= static_cast<std::uint32_t>(
                std::to_integer<unsigned char>(input[offset + byte]))
            << (byte * 8U);
  }
  return std::bit_cast<float>(bits);
}

void append_f32(joggle::Bytes& output, float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  for (std::size_t byte = 0; byte < 4U; ++byte) {
    output.push_back(static_cast<std::byte>((bits >> (byte * 8U)) & 0xffU));
  }
}

std::int64_t round_even(float value) {
  const float lower_value = std::floor(value);
  const auto lower = static_cast<std::int64_t>(lower_value);
  const float fraction = value - lower_value;
  if (fraction < 0.5F) {
    return lower;
  }
  if (fraction > 0.5F || lower % 2 != 0) {
    return lower + 1;
  }
  return lower;
}

std::optional<joggle::Bytes>
quantize(const joggle::Bytes& input, const Reals& scales,
         const Integers& zeros, const Shape& shape, std::int64_t axis,
         const joggle::Type& storage_type,
         joggle::Diagnostics& diagnostics) {
  const auto format = storage(storage_type, true, diagnostics);
  const auto elements = element_count(shape, diagnostics);
  const auto expected =
      elements ? byte_count(*elements, 4U, diagnostics) : std::nullopt;
  const auto affine = parameters(scales, zeros, shape, axis, diagnostics);
  if (!format || !expected || !affine || input.size() != *expected ||
      !zeros_fit(zeros, *format, true, diagnostics)) {
    if (format && expected && affine && input.size() != *expected &&
        diagnostics.ok()) {
      fail(diagnostics, "quantize input is not one little-endian f32 per element");
    }
    return std::nullopt;
  }

  const std::int64_t minimum = *format == Storage::U8 ? 0 : -128;
  const std::int64_t maximum = *format == Storage::U8 ? 255 : 127;
  joggle::Bytes output;
  output.reserve(*elements);
  for (std::size_t index = 0; index < *elements; ++index) {
    const auto parameter = affine->at(index);
    const float value = read_f32(input, index * 4U);
    if (std::isnan(value)) {
      fail(diagnostics, "quantize does not assign a value to NaN input");
      return std::nullopt;
    }
    const float quotient = value / affine->scales[parameter];
    const auto zero = affine->zeros[parameter];
    std::int64_t result = 0;
    if (quotient <= static_cast<float>(minimum - zero)) {
      result = minimum;
    } else if (quotient >= static_cast<float>(maximum - zero)) {
      result = maximum;
    } else {
      result = std::clamp(round_even(quotient) + zero, minimum, maximum);
    }
    const auto bits = *format == Storage::U8
                          ? static_cast<std::uint8_t>(result)
                          : std::bit_cast<std::uint8_t>(
                                static_cast<std::int8_t>(result));
    output.push_back(static_cast<std::byte>(bits));
  }
  return output;
}

std::int64_t read_integer(const joggle::Bytes& input, std::size_t index,
                          Storage format) {
  if (format == Storage::U8) {
    return std::to_integer<std::uint8_t>(input[index]);
  }
  if (format == Storage::I8) {
    const auto bits = std::to_integer<std::uint8_t>(input[index]);
    return std::bit_cast<std::int8_t>(bits);
  }
  std::uint32_t bits = 0;
  for (std::size_t byte = 0; byte < 4U; ++byte) {
    bits |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(
                input[index * 4U + byte]))
            << (byte * 8U);
  }
  return std::bit_cast<std::int32_t>(bits);
}

std::optional<joggle::Bytes>
dequantize(const joggle::Bytes& input, const Reals& scales,
           const Integers& zeros, const Shape& shape, std::int64_t axis,
           const joggle::Type& storage_type,
           joggle::Diagnostics& diagnostics) {
  const auto format = storage(storage_type, false, diagnostics);
  const auto elements = element_count(shape, diagnostics);
  const auto width = format && *format == Storage::I32 ? 4U : 1U;
  const auto expected =
      elements ? byte_count(*elements, width, diagnostics) : std::nullopt;
  const auto affine = parameters(scales, zeros, shape, axis, diagnostics);
  if (!format || !expected || !affine || input.size() != *expected ||
      !zeros_fit(zeros, *format, false, diagnostics)) {
    if (format && expected && affine && input.size() != *expected &&
        diagnostics.ok()) {
      fail(diagnostics, "dequantize input byte count does not match storage");
    }
    return std::nullopt;
  }

  const auto output_size = byte_count(*elements, 4U, diagnostics);
  if (!output_size) {
    return std::nullopt;
  }
  joggle::Bytes output;
  output.reserve(*output_size);
  for (std::size_t index = 0; index < *elements; ++index) {
    const auto parameter = affine->at(index);
    const auto difference =
        read_integer(input, index, *format) - affine->zeros[parameter];
    append_f32(output,
               static_cast<float>(difference) * affine->scales[parameter]);
  }
  return output;
}

}  // namespace

void joggle_module(joggle::Compiler& compiler, const joggle::Module& module,
                   joggle::Diagnostics&) {
  compiler.bind(module, "quantize", quantize);
  compiler.bind(module, "dequantize", dequantize);
}
