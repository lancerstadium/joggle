#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include <joggle/joggle.h>

namespace {

std::optional<std::int64_t> encode(const joggle::Type& type, double value,
                                   joggle::Diagnostics& diagnostics) {
  const auto width = type.get<std::int64_t>("width");
  const auto fraction = type.get<std::int64_t>("fraction");
  const auto signed_value = type.get<bool>("signed");
  if (!width || !fraction || !signed_value || !std::isfinite(value)) {
    diagnostics.report("fixed.encode requires a finite value");
    return std::nullopt;
  }
  const long double scale = std::ldexp(1.0L, static_cast<int>(*fraction));
  const long double modulus = std::ldexp(1.0L, static_cast<int>(*width));
  const long double minimum = *signed_value ? -modulus / 2.0L : 0.0L;
  const long double maximum =
      *signed_value ? modulus / 2.0L - 1.0L : modulus - 1.0L;
  long double scaled = std::round(static_cast<long double>(value) * scale);
  scaled = std::clamp(scaled, minimum, maximum);
  if (scaled < 0.0L) {
    scaled += modulus;
  }
  return static_cast<std::int64_t>(scaled);
}

std::optional<double> decode(const joggle::Type& type, std::int64_t bits,
                             joggle::Diagnostics& diagnostics) {
  const auto width = type.get<std::int64_t>("width");
  const auto fraction = type.get<std::int64_t>("fraction");
  const auto signed_value = type.get<bool>("signed");
  if (!width || !fraction || !signed_value) {
    return std::nullopt;
  }
  const std::int64_t modulus = std::int64_t{1} << *width;
  if (bits < 0 || bits >= modulus) {
    diagnostics.report("fixed.decode bits do not fit the declared width");
    return std::nullopt;
  }
  std::int64_t value = bits;
  const std::int64_t sign = std::int64_t{1} << (*width - 1);
  if (*signed_value && value >= sign) {
    value -= modulus;
  }
  return std::ldexp(static_cast<double>(value), -static_cast<int>(*fraction));
}

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto q = module.type("q");
  if (!q) {
    diagnostics.report("fixed behavior does not match its linked schema");
    return false;
  }

  compiler.bind(
      *q,
      [](const joggle::Type& type, joggle::Diagnostics& type_diagnostics) {
        const auto width = type.get<std::int64_t>("width");
        const auto fraction = type.get<std::int64_t>("fraction");
        if (!width || !fraction || *width < 2 || *width > 62 || *fraction < 0 ||
            *fraction >= *width) {
          type_diagnostics.report(
              "fixed.q needs 2 <= width <= 62 and 0 <= fraction < width");
          return false;
        }
        return true;
      });
  compiler.bind(*q, "storage_bits", [](const joggle::Type& type) {
    return type.get<std::int64_t>("width");
  });
  compiler.bind(*q, "is_signed", [](const joggle::Type& type) {
    return type.get<bool>("signed");
  });
  compiler.bind(*q, "encode",
                [](const joggle::Type& type, double value,
                   joggle::Diagnostics& method_diagnostics) {
                  return encode(type, value, method_diagnostics);
                });
  compiler.bind(*q, "decode",
                [](const joggle::Type& type, std::int64_t bits,
                   joggle::Diagnostics& method_diagnostics) {
                  return decode(type, bits, method_diagnostics);
                });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
