#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto keep = module.function("keep");
  const auto converted = module.function("converted");
  const auto convert = module.function("convert");
  if (!keep || !converted || !convert) {
    diagnostics.report("external behavior does not match its Module");
    return false;
  }

  compiler.bind(*convert, [keep = *keep, converted = *converted](
                            joggle::ir::Function& function,
                            joggle::Diagnostics& pass_diagnostics) {
    const auto operations = function.instructions();
    auto edit = function.edit();
    for (const joggle::ir::Instruction& instruction : operations) {
      if (instruction.callee() == keep) {
        edit.replace(instruction, converted);
      }
    }
    return edit.commit(pass_diagnostics);
  });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
