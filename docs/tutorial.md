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

Inspect the modules available on the same explicit search path:

```sh
./build/joggle module list -M modules
./build/joggle module info nn -M modules
./build/joggle module check nn -M modules
```

An external module needs only its directory. Install and remove it from an
explicit local root as follows:

```sh
./build/joggle module install path/to/my.module local-modules -M modules
./build/joggle module uninstall my.module local-modules
```

The install command validates a staged copy before it becomes visible and does
not replace an existing directory.

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
native module and calls its declared native function.

There is no hidden lowering step in this workflow. Loops, calls, mutable source
bindings, and function edits all refer to one `Mod`.

## Reuse network semantics

The installed `tensor` and `nn` modules are ordinary source libraries. A model
can stay concise while the referenced implementation remains inspectable:

```jog
module network
use nn

fn block(x: tensor<f32, [4]>, skip: tensor<f32, [4]>)
    -> tensor<f32, [4]> {
  return nn.relu(x + skip)
}
```

The tensor-specific `+` wins over the generic base overload by structural
specificity. Its body is a linear element loop; `nn.relu` is another loop with
a condition. Loading the functions does not expand them. A project chooses
the level it wants with an ordinary transform:

```jog
module expose
use opt

fn network(m: Mod) -> bool {
  return opt.expand(m, ["operator +", "nn.relu"])
}
```

Running `expose.network` replaces only those two calls by their resolved
bodies. A later invocation can expose `tensor.matmul`, while an experiment
that maps the abstract call directly to a target primitive can leave it
untouched. Body expansion is generic: the core contains no tensor or NN name,
and C++ can perform the same edit with `env.resolve(mod, op)` followed by
`mod.expand(op, fn)`.

Frontend attributes are structural dictionaries. A bridge can use
`has(attrs, key)`, strict `attrs[key]`, `get(attrs, key, fallback)`, and
`keys(attrs)` directly in `.jog`; no schema accessor class or frontend-specific
core hook is required.

For same-signature calls, the whole frontend relation can stay declarative:

```jog
fn onnx_to_nn(m: Mod) -> bool {
  var changed = ir.use(m, "nn")
  changed = opt.rename(m, [["onnx.Relu", "nn.relu"]]) || changed
  return changed
}
```

Neither importing ONNX nor loading `nn` runs this function. The bridge is
selected explicitly, and transaction-final verification rejects a target
whose signature does not match the transported call.

The textual equivalent loads a module and selects one of its normal functions:

```cpp
env.path("modules");
if (!env.load("opt") || !joggle::run(env, "opt.fold_add_zero", mod))
  return env.print_diags(stderr);
```

`modules/opt/module.jog` is the complete transform. It iterates functions,
blocks, and operations through `ir`, replaces the result of `x + 0`, and erases
the dead call. No C++ registration is required for that transform.

## Define a fusion policy

A project module can reuse the generic chain matcher while choosing its own
source and destination functions:

```jog
module my_opt
use opt

[entry, target: "my_accelerator"]
fn fuse_conv_norm_act(m: Mod) -> bool {
  return opt.fuse(
    m,
    ["frontend.conv", "frontend.norm", "frontend.relu"],
    "my_accelerator.conv_norm_act"
  )
}
```

Run it explicitly with `joggle run my_opt.fuse_conv_norm_act model.jog -M ...`.
The names and `target` tag belong to this module; Joggle does not register or
interpret them. `opt.fuse` follows only single-use chains, while `ir.fuse`
checks the selected region's order, live-in/live-out boundary, and dominance
before committing the rewrite.

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
14,156,560 initializer bytes, verifier result, and canonical round trip. It
also uses a test bridge built from `opt.fuse` to combine 36
Conv-BatchNormalization-ReLU chains, then verifies and round-trips the changed
module. A separate in-memory protocol case checks typed multi-result nodes and
multiple graph returns. The codec does not interpret either case's operator
names. The download is never part of a normal configure or build.

## Add a data format and primitive

Build the optional saturating-integer module and run its type-directed selector:

```sh
cmake -S . -B build -DJOGGLE_BUILD_SAT=ON
cmake --build build
./build/joggle run sat.select test/data/sat.jog -M build/modules
```

The module declares both its type constructor and its symbolic algebra as
ordinary functions:

```jog
fn sat<W: int>() -> Ty;
fn +<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
fn add<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
```

After verification, embedding code can inspect the actual overload selected
for any call:

```cpp
joggle::Fn target = env.resolve(mod, addition);
if (!target)
  return mod.print_diags(stderr);
```

The `sat<8>` addition becomes `sat.add(a, b)` while the `i32` addition remains
an ordinary `a + b`. `Env::call` invokes `sat.sim(8, 100, 100)` to obtain the
saturated result `127`, or `sat.emit(8)` to obtain a standalone SystemVerilog
implementation. `test/sat.cpp` exercises selection, idempotence, both saturation
limits, type rejection, and emitter structure.
