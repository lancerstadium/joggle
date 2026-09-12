# Joggle

Joggle is a small C++20 compiler workbench for neural-network software/hardware
co-design research. It keeps imported networks, reusable semantics, explicit
loops, storage decisions, and target preparation in one readable function IR.
Extensions are ordinary distributable modules rather than new compiler
subsystems.

Joggle is pre-1.0 research software. It can import and execute selected ONNX
and TFLite models through generated C and a deterministic VM, but it is not yet
a production inference runtime.

## Why Joggle

Co-design experiments often cross boundaries that established compilers keep
separate: a new data format changes operator semantics, loop structure, memory
layout, and target code together. Joggle provides a compact workbench in which
those decisions can be inspected and replaced independently:

- one `.jog` language for modules and readable IR;
- one `Mod/Fn/Blk/Op/Val/Ty/Attr` object model;
- structural types, generics, and operator overloading;
- ordinary functions for decoding, conversion, analysis, transformation, and
  emission;
- transactional editing and deterministic output;
- capability-driven exposure of high-level calls;
- optional native code only at binary or host-execution boundaries.

There is no graph class, kernel class, pass hierarchy, generated adaptor,
global registry, or mandatory lowering pipeline.

## Build

The default build needs CMake 3.20 or newer and a C++20 compiler. It downloads
no dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Check a program and run a transformation:

```sh
./build/joggle check test/data/matmul.jog -M build/modules
./build/joggle run opt.fold_add_zero test/data/matmul.jog \
  -M build/modules > transformed.jog
```

The output is normal `.jog` text. It can be inspected, committed, checked, or
passed to another function.

## Language

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

Calls, constants, loops, conditions, returns, and block yields are the only
structural operation kinds. Concrete computation is an ordinary function call.
A graph is the calls and values in a function; exposing a semantic body adds
loops and scalar calls to that same function.

The [language reference](docs/language.md) covers control flow, multiple
results, compile-time functions, structural types, overload resolution, and
open metadata.

## Workflow

Enable the optional ONNX codec and decode a model:

```sh
cmake -S . -B build -DJOGGLE_BUILD_ONNX=ON
cmake --build build

./build/joggle read onnx.read model.onnx \
  -M build/modules > source.jog
```

Conversion, optimization, storage planning, and emission remain explicit:

```sh
./build/joggle run onnx.nn.convert opt.basic source.jog \
  -M build/modules > semantic.jog

./build/joggle run c.prepare mem.plan semantic.jog \
  -M build/modules > prepared.jog

./build/joggle emit c.source prepared.jog \
  -M build/modules > model.c
```

A codec preserves source-format calls and attributes. Its bridge maps supported
calls to shared tensor and neural-network semantics. Unknown calls remain
visible and a target reports its unsupported frontier; neither stage guesses.

Read-only analyses and artifact functions use the same module resolver:

```sh
./build/joggle query opt.untyped semantic.jog -M build/modules
./build/joggle query stat.summary semantic.jog -M build/modules
./build/joggle run vm.prepare semantic.jog -M build/modules > vm.jog
./build/joggle emit vm.image vm.jog -M build/modules
```

Several functions passed to `run` execute as one transaction. If a later
function fails or leaves invalid IR, all earlier mutations are restored.

## Write an extension

A source module is a directory containing `module.jog`. Public functions are
the module API; implementation helpers use `local fn`. No manifest or
generated header duplicates that API.

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

Run it by adding its parent directory to the module search path:

```sh
./build/joggle run choose_lut.apply model.jog \
  -M build/modules -M path/to/my-modules
```

Functions may accept normal `Attr` parameters or another `Fn` as policy.
An extension can therefore reuse safe IR construction, body expansion, loop
rewrites, and target capability queries without a native binding per operator.

The complete out-of-tree
[`ikj` example](examples/ikj/module.jog) replaces matrix multiplication with
an inspectable loop body. The [`edge` example](examples/edge) shows a
bodyless external kernel ABI. [Module documentation](docs/modules.md) covers
packaging, discovery, lifecycle, and every bundled module.

## Artifacts and weights

The C module can emit a source file, public header, and deterministic external
weight payload:

```sh
./build/joggle emit c.source prepared.jog -M build/modules > model.c
./build/joggle emit c.header prepared.jog -M build/modules > model.h
./build/joggle emit c.data prepared.jog -M build/modules > model.bin
```

ABI spelling, widths, alignment, includes, and external scalar types come from
a configuration dictionary. Function prototypes are derived from resolved
signatures; adding an external kernel does not add an emitter case.

Generated C preserves source names when they are valid C identifiers. A public
function such as `model.main` is emitted as `model_main`; named parameters and
locals keep their readable names. Only compiler-owned temporaries, anonymous
results, storage slots, or escaped C keywords use the reserved `joggle_`
prefix. An explicit `[c: {name: "..."}]` binding pins an external ABI name when
source-derived spelling is not the desired contract.

`c.prepare`, `mem.plan`, and the artifact functions are independent.
Emission never performs hidden conversion, scheduling, or storage planning.
The deterministic `vm` module provides a second execution path and reports
instruction steps, not hardware cycles.

## Repository layout

```text
include/joggle/   public C++ API
src/              parser, IR, verifier, resolver, and runtime
modules/          bundled source and optional native modules
examples/         out-of-tree extensions and application workflows
test/             contract and integration tests
docs/             design, language, module, tutorial, and roadmap references
paper/            evidence plan and reproducible measurements
```

Generated files, downloaded models, and experiment outputs belong in build or
managed artifact directories, not in the source tree.

## Boundaries

- The core is C++20 plus the standard library; optional codecs keep their
  dependencies local.
- Adding computation does not add an `Op` subclass or parser case.
- Adding a module does not rebuild a core header.
- Unknown metadata survives unrelated transformations.
- Target preparation is explicit and rejects unsupported IR.
- Joggle does not yet promise dynamic allocation, automatic scheduling,
  production-runtime coverage, competitive generated-code performance, or
  performance portability.

## Documentation

- [Design](docs/design.md): architecture and stable invariants.
- [Language](docs/language.md): complete `.jog` grammar and semantics.
- [Modules](docs/modules.md): packaging, composition, and bundled libraries.
- [Tutorial](docs/tutorial.md): extension-oriented walkthroughs.
- [Roadmap](docs/roadmap.md): current gaps and evidence priorities.
- [Paper plan](paper/README.md): research questions, evidence ledger, and
  evaluation protocol.
- [Examples](examples/README.md): runnable extensions and application gates.

The implementation removed during the clean redesign remains recoverable at
Git tag `archive/pre-relaunch-a2a281e`.

## License

MIT. See [LICENSE](LICENSE).
