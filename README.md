# Joggle

Joggle is a C++20 compiler infrastructure for extending model semantics,
transformations, conversions, and artifact generation in one typed language.
Its current implementation focuses on neural-network inference and emits
inspectable C or deterministic VM artifacts.

Joggle is pre-1.0 software. The documented surface is the implemented and
tested project API.

## What is implemented

- one `.jog` language for readable IR and compiler metaprograms;
- one `Mod/Fn/Blk/Op/Val/Ty/Attr` object model;
- structural types, generics, and operator overloading;
- ordinary typed functions for analysis, rewriting, conversion, and emission;
- graph-scoped `mod` discovery and composition through explicit search paths;
- transactional editing, verification, and deterministic output;
- revision-indexed query and stage dependencies;
- invalidation by observed functions, collections, operations, values,
  packages, and intrinsics;
- reusable evaluator plans and artifact regions;
- selected ONNX and TFLite import paths;
- generated C and deterministic VM execution paths.

Joggle does not currently claim a general JIT compiler, incremental native-code
compiler, production inference runtime, or automatic optimization of arbitrary
compiler functions.

## Build and test

The default build requires CMake 3.20 or newer and a C++20 compiler. It
downloads no dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Optional components are additive:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_BUILD_TFLITE=ON \
  -DJOGGLE_BUILD_SAT=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`JOGGLE_BUILD_ONNX` requires Protobuf. `JOGGLE_BUILD_TFLITE` requires
FlatBuffers. Set `CMAKE_PREFIX_PATH` when either dependency is installed outside
the default search path.

Useful test groups are:

```sh
ctest --test-dir build -L unit
ctest --test-dir build -L cli
ctest --test-dir build -L c
ctest --test-dir build -L extension
ctest --test-dir build -L example
ctest --test-dir build -L model
```

## Five-minute workflow

Check a program and apply a transformation:

```sh
./build/joggle check test/data/matmul.jog -M build/modules
./build/joggle run opt.fold_add_zero test/data/matmul.jog \
  -M build/modules > transformed.jog
```

Enable the ONNX codec and emit C:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJOGGLE_BUILD_ONNX=ON
cmake --build build

./build/joggle read onnx.read model.onnx \
  -M build/modules > source.jog
./build/joggle run onnx.nn.convert opt.basic source.jog \
  -M build/modules > semantic.jog
./build/joggle run c.prepare bounds.fold opt.fold opt.basic \
  tile.canon mem.plan c.noalias semantic.jog \
  -M build/modules > prepared.jog
./build/joggle emit c.source prepared.jog \
  -M build/modules > model.c
```

Every command accepts `-` as its input, so these stages can also be composed as
a shell pipeline. Named intermediate files remain useful when a workflow needs
to inspect or preserve each state.

## Language sketch

```jog
mod example
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

Calls, constants, loops, conditions, returns, and block yields are the
structural operation kinds. Concrete computation is expressed as ordinary
function calls. A graph is the calls and values in a function; exposing a
semantic body adds loops and scalar calls to that same representation.

See the [language reference](docs/reference/language.md) for the complete
grammar and semantics.

## Write a `mod`

A source package is a directory containing `module.jog`. The file begins with
the `mod` keyword. Public functions form the package API; helpers use
`local fn`.

```jog
mod choose_lut
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

Add its parent directory to the search path:

```sh
./build/joggle run choose_lut.apply model.jog \
  -M build/modules -M path/to/mods
```

The [`ikj`](extensions/ikj/module.jog), [`edge`](extensions/edge/module.jog),
[`locality`](extensions/locality/module.jog), and
[`compact`](extensions/compact/module.jog) extensions are executable examples.
The [bundled `mod` catalogue](docs/reference/module-catalogue.md) describes the
installed packages and their responsibilities.

## Design

Joggle uses one typed object model for programs and compiler functions. Import,
transformation, planning, and emission are explicit operations. `mod` packages
load from explicit search roots, and a `run` sequence commits as one verified
transaction.

The [system design](docs/design/index.md) documents the representation and
component boundaries. [Module organization](docs/design/modules.md) covers
package discovery and lifecycle; [execution and updates](docs/design/execution.md)
covers transactions, dependency tracking, and fallback behavior.

## Repository layout

```text
include/joggle/  public C++ API
src/             parser, IR, verifier, resolver, and evaluator
tool/            the `joggle` command-line tool
modules/         bundled source `mod` packages
extensions/      out-of-tree examples loaded with `-M extensions`
test/            unit, integration, CLI, backend, and documentation tests
docs/            design, tutorials, and reference material
```

Generated content belongs in a configured `build*` directory and is not
tracked.

## Documentation

- [Documentation site](https://lancerstadium.github.io/joggle/)
- [Documentation source](docs/index.md)
- [Getting started](docs/getting-started/index.md)
- [Task-oriented tutorials](docs/tutorials/index.md)
- [System design](docs/design/index.md)
- [Language reference](docs/reference/language.md)
- [Bundled `mod` catalogue](docs/reference/module-catalogue.md)

The documentation tree is validated by `test/docs.py` and deployed from
`docs/` by the GitHub Pages workflow. Code examples in the tutorials should map to
named tests rather than forming a second, unverified implementation.

## Project boundaries

- Unknown source-format operations remain visible; importers do not guess.
- Emission performs no hidden conversion, scheduling, or storage planning.
- External kernels are explicit ABI boundaries.
- Dynamic shapes require proved finite capacities for bounded static storage.
- Reactive reuse is conditional on observed dependencies and verification.
- Experimental ideas do not become documented capabilities before their tests
  and public interfaces exist.

## License

See [LICENSE](LICENSE).
