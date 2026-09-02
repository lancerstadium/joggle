#include <cstdint>

#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto integer = module.type("integer");
  if (!integer) {
    diagnostics.report("arith behavior does not match its linked schema");
    return false;
  }
  compiler.bind(
      *integer,
      [](const joggle::Type& type, joggle::Diagnostics& type_diagnostics) {
        const auto width = type.get<std::int64_t>("width");
        if (!width || *width <= 0 || *width > 4096) {
          type_diagnostics.report(
              "arith.integer width must be between 1 and 4096");
          return false;
        }
        return true;
      });
  compiler.bind(*integer, "storage_bits", [](const joggle::Type& type) {
    return type.get<std::int64_t>("width");
  });
  compiler.bind(*integer, "is_signed", [](const joggle::Type& type) {
    return type.get<bool>("signed");
  });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
