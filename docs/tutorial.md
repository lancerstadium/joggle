# Tutorial

Build and test Joggle:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Then verify and canonically print the real matrix-multiplication fixture:

```sh
./build/joggle check test/data/matmul.jog
```

Run the textual add-zero transform and print its result:

```sh
./build/joggle run opt.fold_add_zero test/data/matmul.jog -M modules
```

The equivalent embedded use is:

```cpp
#include <joggle/joggle.h>

int main() {
  joggle::Env env;
  joggle::Mod mod;
  constexpr std::string_view source =
      "module demo\nfn id(x: i32) -> i32 { return x }\n";
  if (!joggle::parse(env, source, mod, "model.jog"))
    return mod.print_diags(stderr);
  if (!mod.verify(env))
    return mod.print_diags(stderr);
  joggle::print(stdout, mod);
}
```

The complete tested workflow is in `test/workflow.cpp`. It loads `base` and
`tensor`, parses and verifies the generic nested-loop matmul, round-trips its
canonical form, and walks the same `Fn/Blk/Op/Val` representation to fold
`x + 0` by replacing uses and erasing the call. The test also loads an actual
native module and calls its declared host function.

There is no hidden lowering step in this workflow. Loops, calls, mutable source
bindings, and pass edits all refer to one `Mod`.

The textual equivalent loads a module and selects one of its normal functions:

```cpp
env.path("modules");
if (!env.load("opt") || !joggle::run(env, "opt.fold_add_zero", mod))
  return env.print_diags(stderr);
```

`modules/opt/module.jog` is the complete transform. It iterates functions,
blocks, and operations through `ir`, replaces the result of `x + 0`, and erases
the dead call. No C++ registration is required for that transform.

## Import an official ONNX model

Download the pinned official MobileNetV2 model and enable the optional codec:

```sh
cmake -DOUT=/tmp/mobilenetv2-7.onnx -P test/model.cmake
cmake -S . -B build -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_TEST_ONNX_MODEL=/tmp/mobilenetv2-7.onnx
cmake --build build
ctest --test-dir build --output-on-failure
./build/joggle read onnx.read /tmp/mobilenetv2-7.onnx \
  -M build/modules > /tmp/mobilenet.jog
```

The `onnx` test checks the official model's 267 tensor constants, 155 nodes,
14,156,560 initializer bytes, verifier result, and canonical round trip. The
download is never part of a normal configure or build.

## Add a data format and primitive

Build the optional saturating-integer module and run its type-directed selector:

```sh
cmake -S . -B build -DJOGGLE_BUILD_SAT=ON
cmake --build build
./build/joggle run sat.select test/data/sat.jog -M build/modules
```

The `sat<8>` addition becomes `sat.add(a, b)` while the `i32` addition remains
an ordinary `a + b`. `Env::call` invokes `sat.sim(8, 100, 100)` to obtain the
saturated result `127`, or `sat.emit(8)` to obtain a standalone SystemVerilog
implementation. `test/sat.cpp` exercises selection, idempotence, both saturation
limits, type rejection, and emitter structure.
