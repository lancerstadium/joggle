#include <optional>
#include <utility>

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

  compiler.bind(mod, "convert",
                [keep = *keep, converted = *converted](
                    joggle::Fn fn,
                    joggle::Diag& diagnostics) -> std::optional<joggle::Fn> {
                  auto edit = fn.edit();
                  for (const auto& op : fn.ops()) {
                    if (op.callee().referenced_fn() == keep) {
                      edit.replace(op, converted);
                    }
                  }
                  return edit.commit(diagnostics)
                             ? std::optional<joggle::Fn>{std::move(fn)}
                             : std::nullopt;
                });
}

}  // namespace

void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diag& diagnostics) {
  bind(compiler, mod, diagnostics);
}
