# Tutorial

Build and test Joggle:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Then verify and canonically print the real matrix-multiplication fixture:

```sh
./build/joggle check test/data/matmul.jog -M modules
```

Run the textual add-zero transform and print its result:

```sh
./build/joggle run opt.fold_add_zero test/data/matmul.jog -M modules
```

Keep the transformed IR on standard output and write deterministic execution
evidence separately when an experiment needs it:

```sh
./build/joggle run opt.fold_add_zero test/data/matmul.jog \
  --report run.attr -M modules
```

Run a read-only analysis without rewriting or reprinting the module:

```sh
./build/joggle query opt.untyped test/data/matmul.jog -M modules
```

The result is a canonical `Attr` list of call names whose outputs still have
the open `_` type. `opt.unresolved` is the separate symbol-visibility frontier.
For every file command, the CLI loads the dependency closure declared by the
model's `use` lines from the supplied module paths before verification. A
frontend-produced model can therefore be checked, queried, transformed, or
emitted without manually naming each transitive dependency.

Inspect the modules available on the same explicit search path:

```sh
./build/joggle module list -M modules
./build/joggle module info nn -M modules
./build/joggle module check nn -M modules
```

An external module needs only its directory. Install, upgrade, and remove it
from an explicit local root as follows:

```sh
./build/joggle module install path/to/my.module local-modules -M modules
./build/joggle module upgrade path/to/my.module local-modules -M modules
./build/joggle module uninstall my.module local-modules
```

Install validates a staged copy before it becomes visible and does not replace
an existing directory. Upgrade retains every installed signature modulo
generic-parameter names, validates new dependencies and native code in staging,
and restores the prior directory if commit fails.

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

fn residual(x: tensor<f32, [4]>, skip: tensor<f32, [4]>)
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
and C++ performs the same edit with `env.resolve(mod, op)` followed by
`env.expand(mod, op, fn)`. The environment-aware edit handles both a resolved
body and an alternative implementation, so dependency visibility and body
expansion commit together.

For a larger model, list the calls a consumer can already implement and let
`opt` expose everything else to that boundary:

```jog
module edge
use opt
use ir
use tensor

fn tensor.matmul<M: int, N: int, K: int>(
  a: tensor<i8, [M, K]>, b: tensor<i8, [K, N]>
) -> tensor<i8, [M, N]>;

fn caps() -> list<Fn> {
  return ir.fns("edge")
}

fn prepare(m: Mod) -> bool {
  return opt.legalize(m, caps(), 16)
}

fn missing(m: Mod) -> list<str> {
  return opt.frontier(m, caps())
}
```

`prepare` is an ordinary transform and `missing` is an ordinary read-only
query. A bodyless declaration is both the semantic name and the accepted type
contract. The example retains rank-two `i8` matrix products with compatible
symbolic dimensions; other `tensor.matmul` calls remain visible or expand.
The same form can describe `nn.conv2d` or a custom function, so Joggle does not
prescribe an abstraction level.

To provide an implementation instead of only retaining the call, give that
same declaration a normal body and apply the module's functions:

```jog
fn dot<M: int, N: int, K: int>(
  a: tensor<i8, [M, K]>, b: tensor<i8, [K, N]>
) -> tensor<i8, [M, N]>;

fn tensor.matmul<M: int, N: int, K: int>(
  a: tensor<i8, [M, K]>, b: tensor<i8, [K, N]>
) -> tensor<i8, [M, N]> {
  return dot(a, b)
}

fn apply(m: Mod) -> bool {
  return opt.apply(m, ir.fns("edge"))
}
```

The ordinary overload rules choose among generic and shape- or format-specific
implementations. Newly exposed matching calls are applied to a fixed point.
Use `opt.apply(m, impls, limit)` when a recursive specialization needs an
explicit bound; ambiguity or bound exhaustion restores the complete input
module, and declaration order is never a selection policy.

Frontend attributes are structural dictionaries. A bridge can use
`has(attrs, key)`, strict `attrs[key]`, `get(attrs, key, fallback)`, and
`keys(attrs)` directly in `.jog`; no schema accessor class or frontend-specific
core hook is required.

Computation and data annotations stay separate without new object families:

```jog
[place: "edge"]
let [quant: {scale: [0.25], zero_point: [0]}] y = frontend.add(a, b)
```

The first dictionary belongs to the call `Op`; the inline dictionary belongs
to its result `Val`. A module queries both with the same `ir.meta` and edits
both with the same `ir.set`/`ir.unset` functions. Names such as `place` and
`quant` are examples, not built-in policies.

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
`Blk`s and operations through `ir`, replaces the result of `x + 0`, and erases
the dead call. No C++ registration is required for that transform.

## Materialize a function template

A module may copy a normal function into the program when a transform needs a
named helper or a local template boundary:

```jog
module localize
use ir
use nn

fn apply(m: Mod) -> bool {
  for fn in ir.fns("nn") {
    if ir.name(fn) == "relu" {
      return ir.live(ir.clone(m, fn, "edge_relu"))
    }
  }
  return false
}
```

The copied `Fn` retains its generic signature, nested loops, metadata, and
ordinary calls. Joggle adds the source module to the program's dependency graph
in the same transaction. A recursive template calls the new local function;
only references that would become ambiguous are qualified. A duplicate overload
or invalid destination name leaves both text and revision unchanged. There is
no generated declaration, stateful builder, or separate kernel representation.

Pass concrete generic terms to create a monomorphic helper through the same
operation:

```jog
let relu4 = ir.clone(
  m, template, "relu4", [ty("f32"), ty("[4]")]
)
```

The result is `fn relu4(x: tensor<f32, [4]>) -> tensor<f32, [4]>` with a fully
substituted body and no generic parameters. Shape or width generics used as
ordinary values become constants or list literals in the entry block. Invalid
bindings and collisions with an existing concrete overload leave the program
unchanged.

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

Download the pinned official model matrix and enable the optional codec:

```sh
cmake -DOUT=/tmp/joggle-onnx-zoo -P test/zoo.cmake
cmake -S . -B build -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_TEST_ONNX_ZOO=/tmp/joggle-onnx-zoo
cmake --build build
ctest --test-dir build --output-on-failure
./build/joggle read onnx.read /tmp/joggle-onnx-zoo/mobilenetv2-7.onnx \
  -M build/modules > /tmp/mobilenet.jog
```

The MobileNetV2 gate checks 267 tensor constants, 155 nodes,
14,156,560 initializer bytes, verifier result, and canonical round trip. It
also uses a test function built from `opt.fuse` to combine 36
Conv-BatchNormalization-ReLU chains, then verifies and round-trips the changed
module. The model-matrix gate independently imports, infers, converts, verifies,
and round-trips SqueezeNet 1.1, QDQ SqueezeNet 1.0, ResNet-18, and
Tiny-YOLOv2. They exercise Concat, whole-network QDQ boundaries, residual Add,
and detection-oriented MaxPool/LeakyReLU structure. Tiny-YOLOv3 is the complex
control-flow gate: 269 tensor constants, 291 calls, and four `Loop` body graphs
must import, verify, round-trip, and retain an exact type frontier. Each body is
an ordinary function, with lexical captures exposed as parameters and call
operands. `onnx.graph` recovers that `Fn`; the semantic module derives
loop-carried and scan types from its signature. This closes all eight Loop
outputs and four dependent Reshapes, reducing the current open frontier from
280 results to 219 without pretending the remaining operators are supported.
A separate in-memory protocol case checks the exact capture mapping, typed
multi-result nodes, scan rank, and multiple graph returns. The codec does not
interpret their operator names. Downloads remain an explicit test setup step
and never occur during configure or build. The pinned 1.2 MB UltraFace RFB-320
adds a shape-heavy edge detector rather than another classifier: its 244 tensor
constants and 242 calls infer from 240 open results to zero, then convert,
verify, and round-trip. It covers both tensor-valued Constant nodes and the
legacy attribute-form Slice schema. All GitHub-hosted models use immutable
repository commits and SHA-256 checks. The 28 MiB SSD-MobileNetV1-12 detector
is a partial semantic stress gate: 1,567 constants, 5,985 nodes, eight nested
graphs, Resize, and NonMaxSuppression must verify and round-trip, while type
propagation reduces 6,790 open results to the pinned frontier of 4,682. This is
deliberately not a full execution claim. ShuffleNet V2 independently requires
complete inference, conversion, verification, and round trip. DenseNet-121
removes every imported intermediate result type before inference and requires
all 910 to be reconstructed from the model signature and constants, preventing
value-info-rich models from producing a false positive.

## Add a data format and primitive

Build the optional saturating-integer module and run its type-directed selector:

```sh
cmake -S . -B build -DJOGGLE_BUILD_SAT=ON
cmake --build build
./build/joggle run sat.select test/data/sat.jog -M build/modules
```

The module declares its type constructor, symbolic algebra, and structural
format predicate as ordinary functions:

```jog
fn sat<W: int>() -> Ty;
fn +<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
fn add<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
fn supports(type: Ty) -> bool;
```

After verification, embedding code can inspect the actual overload selected
for any call:

```cpp
joggle::Fn target = env.resolve(mod, addition);
if (!target)
  return mod.print_diags(stderr);
```

The predicate uses ordinary `Ty` reflection rather than a native string parser.
The `sat<8>` addition becomes `sat.add(a, b)` while the `i32` addition remains
an ordinary `a + b`. `Env::call` invokes `sat.sim(8, 100, 100)` to obtain the
saturated result `127`, or `sat.emit(8)` to obtain a standalone SystemVerilog
implementation. `test/sat.cpp` exercises selection, idempotence, both
saturation limits, type rejection, and emitter structure.

## Emit an exposed kernel as C

The standard `c` module consumes computation only after its dependency calls
have been exposed into local scalar, tensor-access, loop, and branch structure:

```sh
joggle emit c.source test/data/c.jog -M build/modules > /tmp/model.c
cc -std=c99 /tmp/model.c test/data/c_main.c -o /tmp/model
/tmp/model
```

`c.source` is an ordinary read-only `fn(Mod) -> str`. The command does not
choose a target pipeline or mutate the input. Static tensors become flat C
arrays and tensor results use an output-pointer parameter. If a `tensor` or
`nn` call has not been exposed, emission fails and names that call rather than
performing an implicit lowering.

When the shared function bodies are the desired implementation, preparation is
another explicit function call:

```sh
joggle run c.prepare test/data/c_open.jog \
  -M build/modules > /tmp/prepared.jog
joggle emit c.source /tmp/prepared.jog \
  -M build/modules > /tmp/add.c
```

`c.prepare` expands only unsupported, metadata-free calls with a visible body
and stops at the C module's scalar/tensor-access/control-flow boundary. It is
transactional and bounded; it is not run by `emit` or module loading.

Plan reusable storage only when the experiment needs it:

```sh
joggle run mem.plan /tmp/prepared.jog \
  -M build/modules > /tmp/planned.jog
joggle query mem.buffers /tmp/planned.jog -M build/modules
joggle emit c.source /tmp/planned.jog \
  -M build/modules > /tmp/planned.c
```

`mem.plan` is an ordinary idempotent transform. It handles fixed-shape local
tensors, excludes parameters and constants, and reuses a slot only after the
prior binding's last real use. `c.source` reads `mem.slot` metadata if present;
it does not run the planner. A device-specific module may instead interpret or
replace the same open metadata with its own allocation policy.

Inspect deterministic structural measurements before or after any step:

```sh
joggle query stat.summary /tmp/prepared.jog -M build/modules
joggle query stat.summary /tmp/planned.jog -M build/modules
```

The returned dictionary is stable and directly diffable. `tensor_vals` and
`static_tensor_elems` describe represented IR values; `mem_slots` and
`mem_elems` describe an explicit storage plan. Device bytes, cycles, and
energy belong in a separate policy module rather than being guessed by `stat`.
