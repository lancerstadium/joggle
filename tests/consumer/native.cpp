#include <joggle/joggle.h>

namespace {

void bind(joggle::Compiler& compiler, const joggle::Mod& mod,
          joggle::Diag& diagnostics) {
  const auto keep = mod.fn("keep");
  const auto converted = mod.fn("converted");
  if (!keep || !converted) {
    diagnostics.report("external native does not match its Mod");
    return;
  }

  compiler.bind(
      mod, "convert",
      [keep = *keep, converted = *converted](
          joggle::Fn fn,
          joggle::Diag& diagnostics) -> std::optional<joggle::Fn> {
        if (!joggle::replace_calls(fn, keep, converted, diagnostics)) {
          return std::nullopt;
        }
        return fn;
      });
}

}  // namespace

void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diag& diagnostics) {
  bind(compiler, mod, diagnostics);
}
