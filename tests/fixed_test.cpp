#include <cmath>
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

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_BITMATH_MODULE);
  compiler.load(JOGGLE_FIXED_MODULE);
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto bitmath = compiler.module("bitmath");
  const auto fixed = compiler.module("fixed");
  if (!bitmath || !fixed ||
      !compiler.load_behavior("bitmath", JOGGLE_BITMATH_BEHAVIOR) ||
      !compiler.load_behavior("fixed", JOGGLE_FIXED_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto q = fixed->type("q");
  const auto codec = fixed->interface("codec");
  const auto numeric_format = bitmath->interface("numeric_format");
  const auto encode = codec ? codec->method("encode") : std::nullopt;
  const auto decode = codec ? codec->method("decode") : std::nullopt;
  const auto storage_bits =
      numeric_format ? numeric_format->method("storage_bits") : std::nullopt;
  if (!q || !codec || !numeric_format || !encode || !decode || !storage_bits) {
    return EXIT_FAILURE;
  }

  const auto q8_4 = compiler.make(*q, 8, 4, true);
  const auto u8_4 = compiler.make(*q, 8, 4, false);
  bool ok = true;
  ok &=
      expect(q8_4 && u8_4 && compiler.conforms(*q, *numeric_format) &&
                 compiler.conforms(*q, *codec),
             "a third-party format conforms to imported and local interfaces");
  const auto negative =
      q8_4 ? compiler.call<std::int64_t>(*q8_4, *encode, -1.5) : std::nullopt;
  const auto restored = negative && q8_4
                            ? compiler.call<double>(*q8_4, *decode, *negative)
                            : std::nullopt;
  const auto saturated =
      q8_4 ? compiler.call<std::int64_t>(*q8_4, *encode, 100.0) : std::nullopt;
  const auto unsigned_floor =
      u8_4 ? compiler.call<std::int64_t>(*u8_4, *encode, -1.0) : std::nullopt;
  ok &= expect(negative && *negative == 232 && restored &&
                   std::abs(*restored + 1.5) < 1e-12 && saturated &&
                   *saturated == 127 && unsigned_floor && *unsigned_floor == 0,
               "the codec has executable rounding, saturation, and "
               "two's-complement semantics");
  const auto bits =
      q8_4 ? compiler.call<std::int64_t>(*q8_4, *storage_bits) : std::nullopt;
  ok &= expect(bits && *bits == 8,
               "the imported numeric-format method dispatches to fixed");

  const auto invalid = compiler.make(*q, 1, 0, true);
  const auto invalid_diagnostics = compiler.diagnostics().entries();
  ok &= expect(!invalid && !compiler.ok() && !invalid_diagnostics.empty() &&
                   invalid_diagnostics.back().message.find(
                       "2 <= width <= 62") != std::string::npos,
               "invalid format parameters fail at construction");

  joggle::Compiler invalid_source;
  invalid_source.load(JOGGLE_BITMATH_MODULE);
  invalid_source.load(JOGGLE_FIXED_MODULE);
  invalid_source.add(R"(
joggle 1;

module invalid_fixed@1.0.0 {
  import fixed@1 as fx;

  graph main(%x: fx.q<1, 0>) -> fx.q<1, 0> {
    return %x;
  }
}
)",
                     "invalid-fixed.joggle");
  const bool source_linked = invalid_source.link();
  const bool source_behaviors =
      source_linked &&
      invalid_source.load_behavior("bitmath", JOGGLE_BITMATH_BEHAVIOR) &&
      invalid_source.load_behavior("fixed", JOGGLE_FIXED_BEHAVIOR);
  const auto invalid_module = invalid_source.module("invalid_fixed");
  const auto invalid_graph =
      source_behaviors && invalid_module
          ? invalid_source.graph("invalid_fixed.main")
          : std::optional<joggle::Graph>{};
  const auto source_diagnostics = invalid_source.diagnostics().entries();
  ok &= expect(
      !invalid_graph && !source_diagnostics.empty() &&
          source_diagnostics.back().message.find("2 <= width <= 62") !=
              std::string::npos &&
          source_diagnostics.back().source &&
          source_diagnostics.back().source->source == "invalid-fixed.joggle" &&
          source_diagnostics.back().source->begin.line == 7U,
      "type behavior diagnostics retain the graph source range");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
