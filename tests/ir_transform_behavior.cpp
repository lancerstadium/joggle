#include <optional>
#include <utility>

#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto add_helper = module.function("add_helper");
  if (!add_helper) {
    diagnostics.report("IR transform behavior does not match its schema");
    return false;
  }
  compiler.bind(
      *add_helper,
      [](joggle::ir::Module input,
         joggle::Diagnostics& reported)
          -> std::optional<joggle::ir::Module> {
        const joggle::Function* main =
            static_cast<const joggle::ir::Module&>(input).function("main");
        if (main == nullptr ||
            !input.insert("helper", main->clone(), reported)) {
          return std::nullopt;
        }
        return input;
      });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
