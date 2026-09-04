#include <joggle/joggle.h>

#include "import.h"

void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diagnostics&) {
  compiler.bind(mod, "read", joggle_onnx::read);
}
