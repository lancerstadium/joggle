#include <optional>
#include <utility>

#include <joggle/joggle.h>

namespace {

template <typename Subject>
std::optional<Subject>
replace(joggle::Compiler& compiler, Subject input, const joggle::Fn& before,
        const joggle::Fn& after, joggle::Diag& diagnostics) {
  const auto changed =
      joggle::replace(compiler, input, before, after, diagnostics);
  return changed ? std::optional<Subject>{std::move(input)} : std::nullopt;
}

}  // namespace

void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diag&) {
  compiler.bind(
      mod, "replace",
      [](joggle::Compiler& active, joggle::Fn input, const joggle::Fn& before,
         const joggle::Fn& after, joggle::Diag& diagnostics) {
        return replace(active, std::move(input), before, after, diagnostics);
      });
  compiler.bind(
      mod, "replace",
      [](joggle::Compiler& active, joggle::Mod input, const joggle::Fn& before,
         const joggle::Fn& after, joggle::Diag& diagnostics) {
        return replace(active, std::move(input), before, after, diagnostics);
      });
}
