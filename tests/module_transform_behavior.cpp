#include <optional>
#include <utility>

#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto add_helper = module.function("add_helper");
  if (!add_helper) {
    diagnostics.report("Module transform behavior does not match its Module");
    return false;
  }
  compiler.bind(
      *add_helper,
      [](joggle::Module input,
         joggle::Diagnostics& reported) -> std::optional<joggle::Module> {
        const auto main_member =
            static_cast<const joggle::Module&>(input).function("main");
        const joggle::ir::Function* main =
            main_member ? main_member->body() : nullptr;
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
