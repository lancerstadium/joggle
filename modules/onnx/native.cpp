#include <joggle/joggle.h>

#include "import.h"

void joggle_module(joggle::Compiler& compiler, const joggle::Module& module,
                   joggle::Diagnostics&) {
  compiler.bind(module, "read", joggle_onnx::read);
}
