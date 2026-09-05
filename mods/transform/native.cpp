#include <optional>
#include <utility>

#include <joggle/joggle.h>

void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diag&) {
  compiler.bind(mod, "inline",
                [](joggle::Compiler& active, joggle::Fn input,
                   joggle::Diag& diagnostics) -> std::optional<joggle::Fn> {
                  const auto changed =
                      joggle::inline_calls(active, input, diagnostics);
                  return changed ? std::optional<joggle::Fn>{std::move(input)}
                                 : std::nullopt;
                });
  compiler.bind(mod, "inline",
                [](joggle::Compiler& active, joggle::Mod input,
                   joggle::Diag& diagnostics) -> std::optional<joggle::Mod> {
                  const auto changed =
                      joggle::inline_calls(active, input, diagnostics);
                  return changed ? std::optional<joggle::Mod>{std::move(input)}
                                 : std::nullopt;
                });
  compiler.bind(mod, "resolve",
                [](joggle::Compiler& active, const joggle::Mod& input,
                   joggle::Diag& diagnostics) {
                  return active.resolve(input, diagnostics);
                });
}
