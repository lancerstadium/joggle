#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto keep = module.declaration("keep");
  const auto converted = module.declaration("converted");
  const auto convert = module.declaration("convert");
  if (!keep || !converted || !convert) {
    diagnostics.report("external behavior does not match its Module");
    return false;
  }

  compiler.bind(*convert, [keep = *keep, converted = *converted](
                              joggle::ir::Function& function,
                              joggle::Diagnostics& diagnostics) {
    return joggle::ir::replace_calls(function, keep, converted, diagnostics)
        .has_value();
  });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
