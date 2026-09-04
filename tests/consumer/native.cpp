#include <joggle/joggle.h>

namespace {

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto keep = module.function("keep");
  const auto converted = module.function("converted");
  if (!keep || !converted) {
    diagnostics.report("external native does not match its Module");
    return;
  }

  compiler.bind(
      module, "convert",
      [keep = *keep, converted = *converted](
          joggle::Function function,
          joggle::Diagnostics& diagnostics) -> std::optional<joggle::Function> {
        if (!joggle::replace_calls(function, keep, converted, diagnostics)) {
          return std::nullopt;
        }
        return function;
      });
}

}  // namespace

void joggle_module(joggle::Compiler& compiler, const joggle::Module& module,
                   joggle::Diagnostics& diagnostics) {
  bind(compiler, module, diagnostics);
}
