#include <cstdint>

#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto word = module.type("word");
  if (!word) {
    diagnostics.report("bitmath behavior does not match its linked schema");
    return false;
  }
  compiler.bind(
      *word,
      [](const joggle::Type& type, joggle::Diagnostics& type_diagnostics) {
        const auto width = type.get<std::int64_t>("width");
        if (!width || *width <= 0 || *width > 4096) {
          type_diagnostics.report(
              "bitmath.word width must be between 1 and 4096");
          return false;
        }
        return true;
      });
  compiler.bind(*word, "storage_bits", [](const joggle::Type& type) {
    return type.get<std::int64_t>("width");
  });
  compiler.bind(*word, "is_signed", [](const joggle::Type& type) {
    return type.get<bool>("signed");
  });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
