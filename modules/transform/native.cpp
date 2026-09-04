#include <optional>
#include <utility>

#include <joggle/joggle.h>

namespace {

template <typename Subject>
std::optional<Subject> replace(joggle::Compiler& compiler, Subject input,
                               const joggle::Function& before,
                               const joggle::Function& after,
                               joggle::Diagnostics& diagnostics) {
  const auto changed =
      joggle::replace(compiler, input, before, after, diagnostics);
  return changed ? std::optional<Subject>{std::move(input)} : std::nullopt;
}

}  // namespace

void joggle_module(joggle::Compiler& compiler, const joggle::Module& module,
                   joggle::Diagnostics&) {
  compiler.bind(
      module, "replace",
      [](joggle::Compiler& active, joggle::Function input,
         const joggle::Function& before, const joggle::Function& after,
         joggle::Diagnostics& diagnostics) {
        return replace(active, std::move(input), before, after, diagnostics);
      });
  compiler.bind(
      module, "replace",
      [](joggle::Compiler& active, joggle::Module input,
         const joggle::Function& before, const joggle::Function& after,
         joggle::Diagnostics& diagnostics) {
        return replace(active, std::move(input), before, after, diagnostics);
      });
}
