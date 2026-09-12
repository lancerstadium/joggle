# Joggle

Joggle is a small C++20 compiler workbench for neural-network and AI
hardware/software co-design research. It provides one readable function IR,
one module format, and one extension boundary. It does not prescribe a graph
dialect, kernel dialect, target hierarchy, scheduler, or paper mechanism.

Joggle is pre-1.0 research software. Its core and extension contracts are
tested on conventional ONNX and TFLite models, but its C emitter is not yet a
production inference runtime.

## The model

The public IR has seven concepts:

```text
Mod  Fn  Blk  Op  Val  Ty  Attr
```

`Mod` owns storage. `Fn`, `Blk`, `Op`, and `Val` are stable handles. `Ty` and
`Attr` are structural values. Calls, constants, loops, conditions, returns,
and block yields are the only structural operation kinds; concrete computation
is an ordinary function call.

A network graph is the calls and values in a function body. Exposing tensor
semantics adds loops and scalar calls to that same function. A transform is
also an ordinary function:

```jog
fn prepare(m: Mod) -> bool
```

There is no pass class, generated adaptor, operator class hierarchy, or hidden
global registry. Frontends, semantic bridges, optimizations, analyses, memory
planners, simulators, and emitters are removable modules.
An exported `fn` is the module API; implementation helpers use `local fn` and
cannot be resolved or invoked through an importing module. No separate export
manifest is generated or maintained.

## Build and try it

The default build needs only a C++20 compiler and CMake 3.20 or newer. It does
not download dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Check a program and run a transform:

```sh
./build/joggle check test/data/matmul.jog -M build/modules
./build/joggle run opt.fold_add_zero test/data/matmul.jog \
  -M build/modules > transformed.jog
```

The output is normal `.jog` text and can be checked, transformed, or committed
again. List several function names before the file to run an ad hoc sequence as
one transaction; a failing later step restores all earlier edits.

## Customize a kernel

[`examples/ikj/module.jog`](examples/ikj/module.jog) is a complete out-of-tree
implementation module. It replaces rank-two `tensor.matmul` with an `i-k-j`
loop body using one generic function:

```jog
[impl: "ikj"]
fn tensor.matmul<E: Ty, M: int, N: int, K: int>(
  a: tensor<E, [M, K]>, b: tensor<E, [K, N]>
) -> tensor<E, [M, N]> {
  var out = tensor<E, [M, N]>(E(0))
  for i in 0..M, k in 0..K, j in 0..N {
    out[i, j] = out[i, j] + a[i, k] * b[k, j]
  }
  return out
}
```

The module contains no C++ binding, target class, schedule object, or second
kernel syntax. `ikj.apply` selects this ordinary body through open metadata;
`c.prepare` then exposes only what the existing C emitter still cannot consume.
The [example instructions](examples/ikj/README.md) generate inspectable IR and
C in `build-dev/`, compile it with warnings as errors, and check its numerical
result. The default test suite runs the same workflow.

[`examples/edge`](examples/edge) covers the complementary case: a bodyless,
monomorphic tensor `Fn` is an external kernel ABI declaration. `c.source`
derives the qualified prototype and call directly from that signature, and a
separate C file provides the implementation. No operator binding table or
emitter edit is involved.

## Language in one example

```jog
module example
use tensor

fn matmul<T: Ty, M: int, N: int, K: int>(
  a: tensor<T, [M, K]>,
  b: tensor<T, [K, N]>
) -> tensor<T, [M, N]> {
  var c = tensor<T, [M, N]>(T(0))
  for i in 0..M, j in 0..N {
    var sum = T(0)
    for k in 0..K {
      sum += a[i, k] * b[k, j]
    }
    c[i, j] = sum
  }
  return c
}
```

Generics are ordinary compile-time values. A module can define another type
constructor with the same mechanism used by `tensor`; widths, shapes, layouts,
and custom number formats do not require a core type registry.

The complete syntax, including multi-result calls, structured control flow,
operator overloading, compile-time functions, and open attributes, is in the
[language reference](docs/language.md).

## Write an extension

A module is a directory containing `module.jog`. It may also contain sorted
`.jog` fragments under `lib/` and one optional native library under `native/`.
No second manifest or generated header is required.

This transform attaches user-owned policy without adding a core concept:

```jog
module choose_lut
use ir

fn apply(m: Mod) -> bool {
  var changed = false
  for op in ir.ops(m) {
    if ir.callee(op) == "nn.relu" {
      changed = ir.set(m, op, "implementation", "lut") || changed
    }
  }
  return changed
}
```

Run it like any bundled function:

```sh
./build/joggle run choose_lut.apply model.jog \
  -M build/modules -M path/to/my-modules
```

Embedding code may also pass ordinary `Attr` arguments to a transform through
`joggle::run`. This exposes parameterized functions such as
`opt.expand(m, callees)` directly, with the supplied arguments recorded in the
deterministic run report; no wrapper pass or generated option class is needed.
The CLI accepts the same canonical literals with repeatable `--arg` options:

```sh
joggle query opt.count model.jog --arg '"operator +"' -M modules
joggle run c.place model.jog --arg '"static"' -M modules
```

Booleans, integers, reals, strings, `hex"..."` bytes, lists, dictionaries, and
`nil` use exactly the `.jog` attribute-literal syntax. `query`, `emit`, and each
function in an ad hoc `run` sequence receive the same ordered arguments.

Compile-time execution is transactional. If the function fails or produces an
invalid module, Joggle restores the input. C++ and `.jog` functions edit the
same `Fn`/`Blk`/`Op`/`Val` representation through the same checks.
`opt.fold(m)` evaluates calls to ordinary `base` functions when every operand
is static; `opt.fold(m, fns)` applies the same mechanism to an explicitly
selected set of user functions. It is a normal transform, not parser magic or
a target hook. Batch `ir.replace` and `ir.erase` keep large rewrites linear in
the size of the IR instead of requiring one whole-module scan per value. The
list form of `ir.set` similarly propagates distinct metadata values across all
selected value families in one structural traversal.
An extension may clone a normal module `fn` into the program with
`ir.clone(m, fn, name)`, providing generated helpers and local template
materialization without a separate kernel builder.
Supplying a final `list<Ty>` binds the template generics and produces a
monomorphic function through the same operation.
`ir.match(call, fn)` supplies those terms directly from a resolved network
call, so an extension does not reimplement dtype or shape inference.
Function overloads of `ir.rename` and `ir.erase` let later transforms maintain
or remove those helpers while preserving resolved calls and live handles.
`ir.generics(op)` reflects explicit call arguments as structural `Ty` values,
and `ir.generics(m, op, types)` changes them through normal overload checking.
Format and layout modules can therefore recursively rewrite types carried both
by values and by constructor calls without parsing callee strings.

Local module lifecycle commands are deterministic and registry-free:

```sh
./build/joggle module list -M build/modules
./build/joggle module info tensor -M build/modules
./build/joggle module check tensor -M build/modules
./build/joggle module install path/to/module local-modules -M build/modules
./build/joggle module upgrade path/to/module local-modules -M build/modules
./build/joggle module uninstall module_name local-modules
```

`module info` includes the callable `fn` declarations, so an installed
extension remains discoverable without a generated header or separate
manifest.

Install and upgrade validate the complete dependency and native-library
closure before changing the destination. Upgrade preserves existing function
signatures. Uninstall refuses to remove a module still used by another module
in the same installation root.

See the [module guide](docs/modules.md) and [tutorial](docs/tutorial.md) for a
native function, semantic relation, custom format, fusion policy, and emitter.

## Import a real network

ONNX is optional, so Protobuf never becomes a core dependency:

```sh
cmake -S . -B build -DJOGGLE_BUILD_ONNX=ON
cmake --build build
./build/joggle read onnx.read model.onnx \
  -M build/modules > source.jog
./build/joggle run onnx.nn.convert source.jog \
  -M build/modules > network.jog
```

Import transports source operations and attributes. Conversion is a separate,
explicit bridge module. Unknown operations remain visible source calls rather
than receiving guessed semantics. A second optional TFLite codec and bridge
exercise the same core boundary with different source metadata and layouts.
Both codecs mark the source format's top-level graph as `[entry]`; nested
subgraphs remain ordinary internal functions.

The test matrix downloads pinned files only when its explicit ONNX Zoo gate is
configured. Normal configure and build remain offline. Covered models include
MNIST, MobileNetV2, SqueezeNet, ResNet-18, Tiny YOLO, UltraFace, SSD-MobileNet,
ShuffleNet, DenseNet, GoogLeNet, EfficientNet QDQ/INT8, and BiDAF. Coverage is
reported conservatively: an imported or typed source call is not described as
executable semantic support. MNIST is the normal end-to-end application gate;
it executes official data through both VM and generated C without reducing the
example to one synthetic operator. MobileNetV2 additionally exercises atomic
network-wide body expansion and canonical round trip on the resulting
loop-level IR. An independent, opt-in application gate executes its official
ONNX Zoo input through both the deterministic VM and compiled C, then compares
all 1,000 outputs with the official result. Source construction carries only
mutable bindings actually
changed by nested control flow, so readable `var` syntax does not replicate
every in-scope binding across every exposed tensor loop.
The shared driver, generated-C harness, reproducible pipelines, and artifact
guide live together in [`examples/onnx`](examples/onnx) rather than being
duplicated between model-specific examples or hidden as test-only code.

## Analyze and emit

Analyses are read-only module functions:

```sh
./build/joggle query opt.untyped network.jog -M build/modules
./build/joggle query stat.summary network.jog -M build/modules
./build/joggle query bounds.report network.jog -M build/modules
```

An analysis policy can itself be a module. The runnable
[`examples/cost`](examples/cost) example supplies an ordinary
`fn(Mod, Op) -> int` weight function and aggregates it with `stat.sum`; the
compiler core knows neither its unit nor its device assumptions.
`bounds` is the corresponding reusable integer fact analysis. It proves only
intervals justified by the current IR and leaves overflow, unsupported
operations, and modified loop-carried recurrences unknown. It never changes an
`index` type or chooses a target ABI; a format, address-generation, or emitter
module may consume the facts explicitly.

Emitters return `str` or `bytes` through the same read-only boundary:

```sh
./build/joggle run c.prepare test/data/c_open.jog \
  -M build/modules > prepared.jog
./build/joggle run mem.plan prepared.jog \
  -M build/modules > planned.jog
./build/joggle emit c.source planned.jog \
  -M build/modules > model.c
./build/joggle emit c.header planned.jog \
  -M build/modules > model.h
```

`c.prepare`, `mem.plan`, `c.source`, and `c.header` are independent module
functions.
If any concrete function carries ordinary `[entry]` metadata, `c.header`
exports only marked entries while `c.source` retains their internal helpers.
A module with no entry marks keeps the pre-1.0 all-functions behavior. No
function name is treated specially. A function may also own its public C
spelling without changing the Joggle symbol:

```jog
[entry, c: {name: "model_run"}]
fn main(x: tensor<f32, [784]>) -> tensor<f32, [10]> { ... }
```

Both the generated declaration and definition then use `model_run`; the final
symbol table is still checked for collisions.
Emission never triggers hidden lowering or planning. The current C module
supports fixed-shape tensor kernels, scalar expressions, local calls,
structured loops and conditions, short-circuit logical expressions,
literal-list indexing, zero- and multi-result functions, and the standard
floating-point functions declared by `math`; unsupported IR fails with a
diagnostic. Preparation composes the
reusable static evaluator and copy propagation before exposing remaining
calls. Each bodyless math declaration owns its optional
`[c: {name: "...", include: "..."}]` and `[vm: "..."]` bindings; emitters do
not contain a second table of `math` names. `round_even` is an ordinary
function body and is exposed by the same preparation path as user code. C and
VM cover the same current `math` surface through exact `f32` and `f64`
overloads; host-libm
transcendentals are not presented as cross-platform bit-exact
implementations. This is a defined portable subset, not yet a complete
numerical library. Generic NN bodies defer only calls
that depend on their element type, allowing specialization to select a precise
built-in or user-supplied overload instead of accepting every `Ty` eagerly.
The preparation policy is not a second target interface. `c.accepts(Mod, Op)`
is an ordinary read-only function, and `opt.expose` repeatedly folds static
work, removes copies, and exposes only calls that predicate rejects. A new
target can supply the same one-function capability boundary without adding a
target class, declaration catalogue, or core case.
After `mem.plan`, the ordinary parameterized transform
`c.place(m, "static")` can request static C workspace storage through open
function metadata. Local storage remains the default. Placement is explicit:
neither planning nor emission chooses it implicitly, and another target is free
to interpret the target-neutral slots differently.
With `JOGGLE_BUILD_SAT=ON`, the separate `sat.c.prepare` bridge recursively
maps concrete `sat<W>` types to C storage, materializes width-specialized
saturating helpers, and then calls `c.prepare`. The C module contains no
`sat` name or format case. The executable regression covers scalar values and
a fixed-shape tensor whose element format is defined entirely by the module.
The same format-owned `sat.materialize` function accepts an ordered storage
map; `sat.vm.prepare` maps every supported width to `i64`, invokes the unchanged
VM preparation path, and executes the tensor case. Neither target knows the
format name.

The bundled `vm` module is a genuinely different execution target. Its pure
`.jog` function `vm.image(Mod) -> str` reflects ordinary IR into a deterministic
image, while its native `vm.run` implementation executes that image and returns
both result bytes and an exact instruction-step count. It accepts scalar
`bool`, `i64`, `index`, `int`, `f32`, and `f64` values plus static tensors of
those elements, structured loops and branches, checked multidimensional
indexing, runtime selection from scalar literal lists, floating square root,
and explicit numeric conversion. Integer and floating-point
nested-loop matrix multiplication execute through both C and VM gates. The
explicit `vm.prepare` function uses `vm.accepts(Mod, Op)` through the same
generic `opt.expose` policy, expands the shared high-level tensor `+` body, and
executes it in the VM. Preparation is never hidden inside image emission. The
step count is not presented as hardware cycles. Frontend bridges normalize
ONNX and TFLite weight payloads to the shared `tensor.literal` primitive; C and
VM both execute that primitive without knowing either frontend. The pinned official
ONNX `test_matmul_2d` case now imports, converts, expands, and matches its
official output through both VM and compiled C. The same gate now completes on
the official MobileNetV2 application: its 28 MB image executes 98,167,456,513
deterministic VM steps and all 1,000 outputs agree with both the official
TensorProto result and generated C. The large step count is deliberately
reported: complete scalar exposure is correct, but is not an efficient
application execution strategy.

## Guarantees and boundaries

- Core is C++20 plus the standard library; codecs keep dependencies local.
- Adding computation does not add an `Op` subclass or parser case.
- Adding a module does not generate or rebuild a core header.
- Module and compile-time-function failures are transactional.
- Verification rejects missing, duplicate, and cyclic dependency structure;
  failed inference restores prior types.
- Cross-module body expansion has one environment-aware API and updates
  dependency visibility atomically.
- Canonical text is deterministic and structurally round-trippable.
- Optional per-step timings are returned separately from canonical reports.
- Attribute names have no built-in target, schedule, placement, or device
  meaning.

Joggle does not yet provide dynamic allocation, a production runtime, a
hardware scheduler, or end-to-end performance portability. Those are module
research opportunities, not implicit core promises.

## Documentation

- [Design](docs/design.md): architecture, invariants, and implemented slices.
- [Language](docs/language.md): complete `.jog` grammar and semantics.
- [Modules](docs/modules.md): packaging, native ABI, and bundled libraries.
- [Tutorial](docs/tutorial.md): extension-oriented walkthroughs.
- [Roadmap](docs/roadmap.md): completion gates and remaining work.

The implementation removed during the clean redesign remains recoverable at
Git tag `archive/pre-relaunch-a2a281e`.

## License

MIT. See [LICENSE](LICENSE).
