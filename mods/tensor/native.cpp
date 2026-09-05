#include <utility>

#include <joggle/joggle.h>

#include "pass.h"

void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diag&) {
  compiler.bind(mod, "fuse",
                [mod](joggle::Compiler& active, joggle::Fn input,
                      joggle::Diag& diagnostics) {
                  return joggle_tensor::fuse(active, mod, std::move(input),
                                             diagnostics);
                });
  compiler.bind(mod, "loops",
                [mod](joggle::Compiler& active, joggle::Fn input,
                      joggle::Diag& diagnostics) {
                  return joggle_tensor::loops(active, mod, std::move(input),
                                              diagnostics);
                });
}
