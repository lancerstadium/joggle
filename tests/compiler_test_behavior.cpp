#include <cstdint>

#include <joggle/joggle.h>

namespace {

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto integer = module.type("integer");
  if (!integer) {
    diagnostics.report("test behavior does not match its linked schema");
    return;
  }
  compiler.verify(*integer, [](const joggle::Type& type,
                               joggle::Diagnostics& type_diagnostics) {
    const auto width = type.get<std::int64_t>("width");
    if (!width || *width <= 0 || *width > 4096) {
      type_diagnostics.report(
          "test_ir.integer width must be between 1 and 4096");
      return false;
    }
    return true;
  });
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
