# Joggle

Joggle is a small C++20 compiler infrastructure for building and distributing
extensions that cross conventional subsystem boundaries. Program vocabulary,
analysis, transformation, implementation choice, and artifact generation can
live in ordinary typed modules over one readable function IR instead of each
requiring a separate host-side registry or plugin hierarchy.

The bundled modules currently concentrate on neural-network inference because
that workload stresses semantics, tensor and loop structure, storage policy,
specialized computation, and deployment together. Joggle is pre-1.0 research
software: it can import selected ONNX and TFLite models and execute selected
models through generated C and a deterministic VM, but it is not yet a
production inference runtime.

## Why Joggle

Compiler experiments often cross boundaries that established systems make
independently extensible. A datatype can affect syntax, type rules, analyses,
representation, and artifacts; a generated policy needs typed inspection,
checked edits, validation, and rollback; a hardware experiment adds semantics,
layout, selection, and deployment together. Joggle keeps such decisions in one
distributable lifecycle while allowing them to be inspected and replaced
independently:

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

One configured directory is enough. `build` is the working tree for everything
below, and every option is additive, so enabling a codec later reconfigures the
same directory rather than starting a second one. **Always pass
`CMAKE_BUILD_TYPE`**: CMake's default is an empty build type, which compiles
without optimization, and a directory configured that way will silently
misreport any timing taken in it. Use a separate `build-debug` only when you
want a debug or sanitizer build alongside the optimized one.

### Running a subset of the tests

Every test carries a kind label, so the everyday loop does not have to pay for
the pinned-model gates:

```sh
ctest --test-dir build -LE model      # everything except the pinned models
ctest --test-dir build -L unit        # native C++ tests; about two seconds
ctest --test-dir build -L c           # generated-C emission and execution
ctest --test-dir build -L cli         # command-line behavior
ctest --test-dir build -L extension   # out-of-tree packages under extensions/
ctest --test-dir build -L example     # example applications
ctest --test-dir build -L model       # pinned ONNX and TFLite gates; minutes
```

The labels are `unit`, `cli`, `c`, `analysis`, `extension`, `example`, `model`,
`install`, `lint`, and `research`. They are assigned in one place at the end of
the test section in `CMakeLists.txt`, so the whole taxonomy can be read at once,
and the existing `onnx-zoo` labels are preserved.

The full configuration, with every optional module enabled and the research
checks registered, is:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DJOGGLE_BUILD_ONNX=ON -DJOGGLE_BUILD_TFLITE=ON -DJOGGLE_BUILD_SAT=ON \
  -DJOGGLE_TEST_ONNX_ZOO=.cache/onnx-zoo -DJOGGLE_RESEARCH=ON
cmake --build build
```

`JOGGLE_RESEARCH` is off by default and is the only switch that makes the build
read `paper/`. It registers two research targets and the checks that verify
recorded evidence and baseline records. Keeping it off is what makes a clone
self-contained: the default configuration builds and tests the project without
the research workspace, so nothing under `src/`, `test/`, or `tool/` depends on
`paper/`. Turn it on when working on the paper rather than on the project.

`JOGGLE_BUILD_ONNX` needs Protobuf and `JOGGLE_BUILD_TFLITE` needs FlatBuffers.
If either is installed outside the default search path, point CMake at it with
`-DCMAKE_PREFIX_PATH=<prefix>`; nothing else in the build depends on where those
libraries live.

Check a program and run a transformation:

```sh
./build/joggle check test/data/matmul.jog -M build/modules
./build/joggle run opt.fold_add_zero test/data/matmul.jog \
  -M build/modules > transformed.jog
```

The output is normal `.jog` text. It can be inspected, committed, checked, or
passed to another function.

All commands accept the global prefix `--diagnostics text|jog`. `text` is the
default for people. `jog` writes failures as one canonical attribute list to
standard error, using the same grammar as module metadata and `--arg` values:

```sh
./build/joggle --diagnostics jog check test/data/matmul.jog -M build/modules
```

Successful standard output is unchanged, so a tool can request structured
failures without changing an existing model or artifact pipeline.

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

The [language reference](docs/reference/language.md) covers control flow, multiple
results, compile-time functions, structural types, overload resolution, and
open metadata.

## Workflow

Enable the optional ONNX codec and decode a model:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJOGGLE_BUILD_ONNX=ON
cmake --build build

./build/joggle read onnx.read model.onnx \
  -M build/modules > source.jog
```

Conversion, optimization, storage planning, and emission remain explicit:

```sh
./build/joggle run onnx.nn.convert opt.basic source.jog \
  -M build/modules > semantic.jog

./build/joggle run c.prepare bounds.fold opt.fold opt.basic \
  tile.canon mem.plan c.noalias semantic.jog \
  -M build/modules > prepared.jog

./build/joggle emit c.source prepared.jog \
  -M build/modules > model.c
```

Every command that reads a model accepts `-` for standard input, so the same
explicit composition can remain in a shell pipeline when intermediate files
are not needed:

```sh
./build/joggle read onnx.read model.onnx -M build/modules |
  ./build/joggle run onnx.nn.convert opt.basic - -M build/modules |
  ./build/joggle run c.prepare bounds.fold opt.fold opt.basic \
    tile.canon mem.plan c.noalias - -M build/modules |
  ./build/joggle emit c.source - -M build/modules > model.c
```

Named files remain preferable when an experiment must preserve and inspect
each progressive state. The example includes `c.noalias` because its generated
application owns disjoint input, output, and weight buffers; omit that explicit
contract when a caller may alias them.

`c.prepare` also specializes the tensor library's explicitly marked shape
bookkeeping. This removes compile-time rank traversal without teaching the C
emitter about Conv, ONNX, or a fixed tensor rank. User and imported value names
remain visible in generated C. Pointer results derived from named return values
use a collision-checked `_out` suffix; only anonymous compiler-created
temporaries, result buffers, and planned storage slots use the role-based
`tmp_`, `out`, and `slot_` stems. C keywords receive a trailing underscore.
The full `joggle_` spelling is reserved for the installed native-module ABI,
not generated model code. Generated artifacts never introduce abbreviated
`jog_` or ordinal `v_` prefixes; regression tests enforce this for public
interfaces, private helpers, and generated application harnesses.

Reusable generic implementations may be specialized without registering every
shape or operator configuration:

```sh
./build/joggle run opt.instantiate semantic.jog \
  --arg '"nn"' -M build/modules > instanced.jog
```

The compiler binds inferred types and literal scalar/list configuration,
deduplicates equal instances, and preserves tensor weights as parameters. The
first private IR helper keeps its source function stem; suffixes appear only
for actual collisions. C function symbols remain module-qualified to avoid
translation-unit collisions, while source and imported value names remain
readable inside those functions.

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
`opt.apply` also accepts either a read-only per-candidate predicate or a
`fn(Mod, Op, list<Fn>) -> list<Fn>` selector. The latter returns zero or one
compatible member, so an implementation module can resolve equal signatures
by shape, layout, representation, cost, or device features without a central
registry. Either policy may take one ordinary `dict` argument, allowing each
invocation to supply a resource budget or feature set without creating a
target class or changing the implementation functions. `opt.candidates`
exposes the unmodified compatible set for reports and policy development.

The complete out-of-tree
[`ikj` extension](extensions/ikj/module.jog) replaces matrix multiplication with
an inspectable loop body. The [`edge` extension](extensions/edge) selects a generic
external kernel while the unchanged model continues to call its semantic
functions; that ABI escape hatch is distinct from optimizing an inspectable
body. [`locality`](extensions/locality) reorders a proved affine reduction, and the
generic `tile.canon` pass compacts proved affine index trees without another
Conv overload. The separate `tile.scalarize` mechanism lets an explicit policy
promote carried output elements when replication is profitable, while
`tile.merge` can linearize adjacent static axes without changing their
lexicographic order or losing loop-carried state.
[`compact`](extensions/compact)
demonstrates the distinct case of selecting a fused implementation with
different workspace behavior.
Neither changes the frontend or C emitter. [Module documentation](docs/reference/module-catalogue.md) covers
packaging, discovery, lifecycle, and every bundled module.

## Artifacts and weights

The C module can emit a source file, public header, and deterministic weight
payload. Without a data-name argument, constants are embedded in the source:

```sh
./build/joggle emit c.source prepared.jog -M build/modules > model.c
./build/joggle emit c.header prepared.jog -M build/modules > model.h
```

Pass the same data name to source and header emission to keep weights in a
separate file:

```sh
./build/joggle emit c.source prepared.jog --arg '"weights"' \
  -M build/modules > model.c
./build/joggle emit c.header prepared.jog --arg '"weights"' \
  -M build/modules > model.h
./build/joggle emit c.data prepared.jog -M build/modules > model.bin
```

Applications and experiment harnesses can obtain the same interface as
structured JSON instead of parsing the generated header:

```sh
./build/joggle query c.api prepared.jog --arg '"weights"' \
  -M build/modules
```

Each exported function reports its exact C declaration, symbol, parameters,
results, C representation class, tensor shapes, element and byte counts,
pointer passing, and whether it receives the external payload. `c.api`,
`c.header`, and `c.source` share the same internal ABI and naming functions.

Private definitions express every non-empty fixed-shape tensor parameter and
result as a C11 array parameter with its static minimum element count. Public
definitions, declarations, and the portable C/C++ header use one consistent
pointer form; `c.api` reports that form as well. This avoids cross-declaration
diagnostics while still carrying known bounds across internal call boundaries.

An entry can state an explicit non-aliasing contract with
`[c: {noalias: true}]`, or a workflow can annotate every exported entry with
`joggle run c.noalias`. The C definition then adds `restrict` to its bounded
tensor array parameters and payload pointer; declarations in the portable
public header remain unqualified. `c.api` reports the contract as `noalias`.
This is never inferred: the caller remains responsible for passing disjoint
pointer objects.

After `mem.plan`, `joggle run c.restrict` can instead prove a conservative
contract for private functions. It considers every represented call to a
candidate and accepts only pairwise-distinct planned slots, distinct immutable
tensor payloads, or a slot/payload pair. Repeated arguments, entry parameters,
returned storage, unplanned temporaries, and functions that implicitly read the
payload are rejected. One unsafe call keeps the private function unqualified;
exported entries are never inferred. The proof is an ordinary module function,
and its result remains visible as `[c: {restrict: true}]` before emission.

ABI spelling, widths, alignment, includes, and external scalar types come from
a configuration dictionary. Function prototypes are derived from resolved
signatures; adding an external kernel does not add an emitter case.

Generated C preserves source names when they are valid C identifiers. A public
function such as `model.main` is emitted as `model_main`; named parameters and
locals keep their readable names. Named pointer results derive from the return
value with an `_out` suffix. Only compiler-owned temporaries, anonymous
results, payload arrays, and storage slots use `tmp_`, `out`, `data_`, and
`slot_`; C keywords receive a trailing underscore. Generated code therefore
does not add a project-name prefix. Immutable scalar bindings remain `const`,
and compound source updates remain `+=`, `*=`, and their corresponding C
forms; integer identity updates are omitted. An explicit
`[c: {name: "..."}]` binding pins an external ABI name when source-derived
spelling is not the desired contract.

When a function has one return path and an unplanned local tensor is returned,
the C emitter binds that tensor directly to its output pointer. It does not
materialize a second local array or append a whole-tensor copy. Planned
workspaces and multiple return paths retain the conservative copy boundary. A
return-only tensor call also receives the output pointer directly, so anonymous
results do not introduce a numbered pointer alias solely for the return.

`mem.plan` also proves a narrow full-overwrite case from ordinary IR structure.
If a tensor constructor feeds one loop, the loop covers either every linear
element or the complete rectangular shape, and its carried tensor is written
unconditionally at the corresponding indices before it is yielded, the plan
marks the constructor fill as dead. Targets may honor that fact; the C module
omits the redundant fill loop. A constructor with any unproven coverage keeps
its original fill, so users do not need an unsafe allocation primitive.

Dynamic logical shapes do not require a second tensor type. `mem.bound` traces
ordinary runtime shape construction and conservative integer intervals to
attach a finite `mem.capacity` when one is provable. `mem.plan` consumes that
same fact for local storage; unbounded values remain explicit instead of being
silently heap allocated or assigned a target-specific limit.

The C artifact keeps these tensors as flat pointers. Unknown input axes become
adjacent `index` arguments and unknown output axes become adjacent `index*`
results; `c.api` reports logical shape and proved capacity separately. This
supports deterministic bounded dynamic results without a runtime tensor
descriptor or hidden allocation.

`c.prepare`, `mem.plan`, and the artifact functions are independent.
Emission never performs hidden conversion, scheduling, or storage planning.
The deterministic `vm` module provides a second execution path and reports
instruction steps, not hardware cycles.

## Repository layout

```text
include/joggle/   public C++ API
src/              parser, IR, verifier, resolver, and runtime
tool/             the `joggle` command-line tool
modules/          bundled source modules; <module>/lib/ holds auto-loaded
                  fragments of the same module scope
extensions/       out-of-tree module packages loaded with -M extensions
test/             contract and integration tests; test/onnx-app/ is the
                  opt-in application gate, test/tools/ its generators
docs/             guide, reference, and internals, indexed by docs/README.md
paper/            research workspace: plan, evidence records, and manuscript
```

`paper/` is a research workspace rather than part of the shipped project, and
the project does not read it. That boundary is enforced, not merely intended:
the default configuration builds and tests without it, and `JOGGLE_RESEARCH=ON`
is the only switch that changes that.

### Where generated files go

The tree separates three kinds of file on purpose, and mixing them is the main
source of confusion in this repository:

| Kind | Location | Tracked |
| --- | --- | --- |
| Hand-written source, fixtures, recorded results | `src/`, `modules/`, `test/`, `paper/data/`, `paper/fixtures/` | yes |
| Compiler and test output | `build/`, or another `build-*` tree you configure | no |
| Study output and raw working records | `build-study/` | no |

Three rules follow from this:

- `build/` is a build tree. Configure it, build it, run `ctest` in it, and take
  compiler timings from it. Study output does not belong there; a script whose
  default output root is inside `build/` is a bug in that script.
- A measurement becomes evidence only once it is promoted into `paper/data/`
  or the relevant `paper/baselines/<system>/<task>/` and described in that
  directory's `README.md`. Numbers that exist only under a `build*` tree are
  working data, and no claim may cite them.
- Nothing under a `build*` tree is deleted because it looks unreferenced. A
  tree can hold the only copy of a record, and a recorded result that was never
  promoted is invisible to `git grep`. Promote first, reclaim second.

### Current build trees

Three, each with one role. Nothing else belongs at the top level.

| Tree | Role |
| --- | --- |
| `build/` | development, test, and compiler-timing tree |
| `build-deps/` | installed FlatBuffers prefix that `build/` is configured against |
| `build-study/` | every study's output: working artifacts, study inputs, and raw records |

Earlier revisions kept one `build-*` tree per study, which is how fifteen of them
accumulated while the README claimed one configured directory was enough. They
are gone: their records were preserved under `build-study/legacy/`, the study
input they held is under `build-study/inputs/`, and their generated artifacts are
reproducible from the pinned models and modules. Do not add a per-study build
tree; point the study's output root at `build-study/` instead.

One name looks like a fourth tree and is not: `build-app` is created inside the
GitHub runners by `.github/workflows/linux-{performance,policy,fusion}.yml`,
which build there, measure, and upload the result. Records under `paper/data/`
cite paths under `build-app` because that is where the artifact lived during the
CI run, so those paths are CI provenance and must not be rewritten to a local
path. No local `build-app` is needed.

`docs/guide/` writes its demonstration output into `build/`, which is ordinary
working scratch and is cleared by a rebuild. Study output is different: it is
kept, so it goes in `build-study/`, and a script whose default output root is
inside `build/` is a bug in that script.

### Checking the layout

The rules above are checked, not merely stated. `test/layout.cmake` runs as
part of the suite and needs only CMake:

```sh
ctest --test-dir build -R layout --output-on-failure
cmake -DSOURCE="$PWD" -P test/layout.cmake   # standalone, same output
```

Each rule is classed `enforce` or `report`. An `enforce` rule already holds, so
a violation is a regression and the test fails. A `report` rule is stated above
but the tree does not satisfy it yet: the finding is listed, the suite stays
green, and the list is the migration queue. Promote a rule to `enforce` in
`test/layout.cmake` once its findings reach zero; every rule ends up there.

Six rules are enforced: no tracked file under a generated tree; every `build*`
directory on disk has a role in the table above; no paper script defaults its
output into `build/`; every repository path quoted in the documentation exists,
with generated trees judged by their declared root rather than by their
contents; every markdown link resolves relative to the file that links it; and
every study directory carries a README.

One metric is reported rather than enforced: how much of `paper/` is still flat.
It is judged acceptable because `paper/README.md` indexes every script with its
output and relocating them would break the recorded reproduction commands that
name them, so the count stays visible without being a defect.

A seventh check was removed as redundant: counting records under generated trees
duplicated the `script-output-root` rule, and it could not separate a violation
from a CTest working directory, which lives in `build/` by design and writes the
same kinds of file.

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

`docs/` is organized by who is reading. Its [index](docs/README.md) carries the
full list and a table mapping each walkthrough to the tests that prove it.

- [Guide](docs/guide/README.md): task-oriented walkthroughs for writing
  extensions.
- [Reference](docs/reference/language.md): the `.jog` language, and the
  catalogue of bundled modules.
- [Internals](docs/internals/design.md): architecture, module lifecycle, and
  the engineering roadmap.
- [Paper plan](paper/README.md): research questions, evidence ledger, and
  evaluation protocol.
- [Extensions](extensions/README.md): out-of-tree module packages the suite
  loads, each a worked example of one compiler boundary.

The implementation removed during the clean redesign remains recoverable at
Git tag `archive/pre-relaunch-a2a281e`.

## License

MIT. See [LICENSE](LICENSE).
