# Real-model compiler-procedure derivation pilot

This is a local mechanism check, not a competitive performance experiment.
The procedure is the existing source-defined `c.prepare(Mod)`. It is cloned
with its private lexical call closure into a separate `Mod`; the second
`opt.expose` call in the copied private `prepare_with` is replaced by `false`.
The original definition and model inputs remain separate. The runner
compares the prepared IR and `c.source` byte-for-byte and verifies both models.
It does not establish that this edit is legal for arbitrary programs.

Build and run from the repository root:

```sh
cmake -S . -B build
cmake --build build --target joggle-derive-prepare -j2
./build/joggle-derive-prepare build-study/c-current/semantic.jog modules build/modules
./build/joggle-derive-prepare build-study/tflite-app/semantic.jog modules build/modules
./build/joggle-derive-prepare build-study/ultraface-block/canonical.jog modules build/modules
./build/joggle-derive-prepare build-study/squeezenet-block/canonical.jog modules build/modules
```

The inputs are existing, ignored complete-model IR artifacts, not checked-in
synthetic fixtures. The UltraFace and SqueezeNet inputs are already canonical
intermediates from prior studies, whereas the two MobileNet inputs are semantic
IR. They must be regenerated or transferred
with their provenance before an external reproduction. The input and compiler
source hashes for this run are:

| Artifact | SHA-256 |
| --- | --- |
| ONNX MobileNetV2 semantic IR | `7a9a1d442068d5d346109808fca0b15ac77a453ad649289ea3b488d5821f93e3` |
| TFLite MobileNetV2 semantic IR | `b39116180618efd8a81889824e8ccd081a77d5a53efbb2fa844409235a8ee4c8` |
| UltraFace canonical IR | `7340007dba3d075ca56d4cac9cd89bd60583fcad195999c8118243de18caf891` |
| SqueezeNet canonical IR | `d1e77c052437dc41f19f730a0a672d61e63be20abbdd8bffc8ae1c798ace768d` |
| `modules/c/module.jog` | `c82e0ddd3386acf37691776a43915ab889ab7947624c08d94b90f4f4ed9bfbb3` |
| Pilot runner | `7d5d8ac1c12193f63bfaad1b1e69bbfcbc9cfefaeaf4d7342f2196e444119abe` |

Environment: macOS arm64, Apple M4, unisolated interactive host; repository
base `b1d8bba` plus the runner in this change. Wall times below are diagnostic
only. The host had other work running, the run order was original then derived,
and there is neither randomized ordering nor dispersion. Do not convert these
numbers into an acceleration claim.

| Input | Original / derived prepare | Model revision, both | IR bytes each | Checked result |
| --- | ---: | ---: | ---: | --- |
| ONNX MobileNetV2, 28,413,332 bytes | 24.640 / 24.092 s | 0 → 7,593 | 28,504,666 | identical IR/C source, both verified |
| TFLite MobileNetV2, 27,955,752 bytes | 32.263 / 31.275 s | 0 → 9,866 | 28,056,370 | identical IR/C source, both verified |
| UltraFace, 2,675,643 bytes | 4.595 / 4.043 s | 0 → 7 | 2,674,735 | identical IR/C source, both verified |
| SqueezeNet, 9,978,320 bytes | 1.702 / 1.548 s | 0 → 7 | 9,977,940 | identical IR/C source, both verified |

The compiler copy took 0.004 s in each reported run. Its code module contains
20 functions and 1,115 operations; the edit moved its revision from 20 to 21.
These counts measure the copied source closure, not the whole installed
compiler or runtime memory. All four model procedures have nonzero structural
revision growth. A revision increment is not necessarily one independent
optimization action. The observed equality on three network architectures
may mean the second exposure step was redundant for these inputs; it does not
prove a general rewrite law or useful speedup. The phase probe below resolves
whether this call changed these inputs. A proposed useful derivation follows
the historical record; it is not an additional result of this pilot.

## Phase boundary probe

The source-only [exposure module](exposure/module.jog) executes the first
`opt.expose`, `ir.type`, and second `opt.expose` from the existing
private `prepare_with` body in `c`. It returns the second call's `bool` result.
The module
has SHA-256
`8b94d812472a37e83b75bd13defb95e199264e9c490be378e04782255673b58a`.
To preserve a report for any input above:

```sh
./build/joggle module check exposure -M paper/experiments -M modules -M build/modules
./build/joggle run exposure.phases build-study/c-current/semantic.jog \
  --report build-study/derivation/onnx-phases.jog \
  -M paper/experiments -M modules -M build/modules > /dev/null
jq '{before,after,expose_calls:.calls["opt.expose"],
     expose_steps:[.steps[]?|select(.fn=="opt.expose")|
                   {before,after,edits}]}' \
  build-study/derivation/onnx-phases.jog
```

The same command with the other three input and report paths was executed
on the macOS host above. Each report records two `opt.expose` calls but only
one changed `opt.expose` step. The first changed step moves the model revision
from 1 to 2,011 (ONNX MobileNetV2), 1 to 3,233 (TFLite MobileNetV2), or 1 to 7
(UltraFace and SqueezeNet). The final report revision is respectively 2,011,
3,234, 7, and 7. The TFLite revision increase between its first exposure and
the final report is attributable to `ir.type`; its second exposure still
reports no structural edit. Unchanged calls are omitted from the `steps`
array, so the separate `calls` count is necessary to establish that the second
exposure actually ran.

This probes one concrete edit, not the general safety of omitting a
post-inference exposure. `ir.type` can change inferred value types and
normalization; a capability predicate may then make a new call eligible for
exposure. Until a rewrite condition or counterexample search closes that
boundary, the original `c.prepare` retains both phases.

## Memory-planner derivation

**Status: source-defined derivation, regression checks, and a complete-model
storage result with a matching local source-patch control, September 16.
An executable external customization comparison and isolated mechanism-cost
measurements remain outstanding.** The complete-model result is recorded
[below](#complete-model-storage-check); it is not an inference-speed result.
Source inspection and local checks were performed by the assistant;
author verification is pending.
The initial, pre-fix audit used base
`62ed2a83efd47021a3272620aa60b2a041bd3964`, with these file hashes:

| Source | SHA-256 |
| --- | --- |
| `modules/mem/module.jog` | `c16c17b0460707252d5d749a72da771ca62ae5c11d571913d57902a9630f6d34` |
| `modules/c/module.jog` | `c82e0ddd3386acf37691776a43915ab889ab7947624c08d94b90f4f4ed9bfbb3` |
| `src/ir.cpp` | `e0feb4fd347a8ae510ffac4185ed8617d5cd0cd77947a1d4054fa8856d8a25f7` |

### Task and implementation boundary

Customize buffer reuse inside the existing `mem.plan(Mod, Fn)`, retaining its
candidate collection, lifetime computation, fill handling, and metadata output.
The source currently selects the first slot with the same element type whose
last use precedes the new buffer's first use. It then grows that slot to the
larger capacity. The public overloads do not take a slot-selection callback.
These observations come from `mem.plan`, not from a claim about every planner.

Derive the **two-argument function**, not merely `mem.plan(Mod)`. The latter
calls the former as a public dependency. `Mod::clone` captures private lexical
helpers but leaves other public dependencies linked; cloning the wrapper would
leave the allocation decision in the original module. The [driver](reuse/compiler.jog)
retains `mem.bound` and invokes the derived function on each subject `Fn`.
Do not add an implicit whole-module clone or a memory-specific execution API.

Within the derived body, replace only the slot-ranking decision. For an eligible
slot with capacity `c` and a request of `n` elements, minimize the tuple
`(max(0, n - c), max(0, c - n), slot_id)` lexicographically. Eligibility remains
same element type and `last_use < first_use`. If no slot is eligible, retain the
existing new-slot path. Retain the existing capacity update and metadata writes.
This is a deterministic greedy rule, not a new allocation algorithm
or a guarantee of globally smaller memory.

The body edit must identify the decision by its operands, control-flow position,
and update of `selected`; it must reject an absent or ambiguous match before
publishing the derived function. Do not match a textual line number or silently
edit the first integer comparison. No new callback may be inserted into the
original planner solely to make this case work. An ordinary source copy with
the same edit remains a valid control.

| Reused part | Edited part | Observable consequence |
| --- | --- | --- |
| Candidate and lifetime computation in `mem.plan` | Selection among eligible slots | Slot assignments and capacities may change |
| Fill handling and `mem.slot` / `mem.types` / `mem.counts` output | No schema change | Existing `c.source` consumes either plan |
| Original function and linked public dependencies | Separate derived function body | Both variants remain independently callable |

This makes the first obstacle concrete: invoking the planner or wrapping it
does not, by itself, change its internal selection rule. The research question
is whether body derivation is a useful, maintainable way to make that change,
not whether a greedy allocator can be implemented in another framework.

### Correctness and resource accounting

Preserving the source's eligibility test is necessary but not sufficient for
memory safety. Its lifetimes are built from named bindings and flattened
operation positions. Before using this case as evidence, audit views, escaping
values, branch joins, and loop-carried aliases against the emitted C. Unknown
capacity and uncertain alias/lifetime cases must be reported and excluded from
reuse, not assumed safe. If a substantial new lifetime analysis is required,
defer this case rather than silently broadening the submission scope.

Check each assigned slot for compatible element type, sufficient capacity, and
non-overlapping live storage, including aliases. Verify the original definition
is unchanged; repeat the derived planner to check deterministic output and
idempotence. Original and derived IR/C need **not** be byte-identical: changed
storage is intentional. Execute both artifacts against the existing numerical
oracles; add lifetime/alias counterexamples separately from model measurements.

Report planned capacity and emitted storage separately. The C emitter's
`definition` uses `uses_slot` to omit slots without runtime uses. Count bytes
using the target C element sizes and account separately for outputs, unplanned
temporaries, weights, and alignment. A sum of `mem.counts` is neither bytes for
mixed types nor measured process RSS. Report runtime peak memory only with a
separately defined measurement; do not infer it from this metadata.

### Controls and decision rule

Use two distinct questions, without pooling their results:

1. **Mechanism:** compare body derivation with a conventional source copy or
   framework-native extension that performs the same rule change. Measure
   retained/copied implementation, edit and integration work, rebuild scope,
   derivation cost, execution cost, and behavior under a small source revision.
   A fragile structural matcher is a cost, not free reuse. Do not infer developer
   productivity from LOC or an assistant's elapsed implementation time.
2. **Utility:** on the already recorded four model inputs, compare artifact
   correctness and storage before/after the rule change. Retain unchanged,
   worse, and ineligible cases. This within-Joggle comparison is an ablation,
   not an external performance comparison. It establishes neither inference
   acceleration nor superiority over another compiler's default allocator.

The external route must use existing customization facilities where available.
The inspected [TVM source](https://apache.googlesource.com/tvm/+/0a3fe22208329edc596db0116752b3259f5d90a2/src/relax/transform/static_plan_block_memory.cc)
already separates token initialization, allocation, and rewriting;
`TokenAllocatorMixed::RequestReuse` searches a size-indexed pool. This historical
revision is source evidence, not the chosen current benchmark revision.
[MLIR's static-memory-planner documentation](https://mlir.llvm.org/docs/Passes/#static-memory-planner-analysis)
describes configurable planning algorithms and a conservative eligibility
envelope. Inspect and pin the available algorithm interface before selecting
a baseline; do not equate this task with ownership-based deallocation or claim
that MLIR cannot change its planner. A matching built-in option is a legitimate,
potentially cheaper solution. Full-model comparisons additionally require the
same workload, target, memory-accounting scope, and output obligations.

Advance this candidate only if the internal change is safely implementable,
affects real eligible inputs, and supports a nontrivial external comparison.
Otherwise keep it as a capability check. Do not manufacture a favorable model
or call the ranking rule itself a paper contribution. The frozen S0/S1/S2
[evolution study](../studies/evolution-study.md) remains a separate, unchanged protocol.

The alternatives inspected were `stat.sum`, which already accepts a measurement
function, and `opt.expose`, whose fixed-point traversal would require a broader
dependency/invalidation design to make a worklist optimization sound. The former
does not require body derivation for ordinary customization; the latter remains
a larger research task. Neither should be substituted merely to produce a win.

### Executed local checks

The prerequisite audit found two incorrect storage reuses in the original
planner. A view read after a subsequent allocation returned `14` instead of `9`
for scalar inputs `2` and `7`. Reusing an allocation inside a loop overwrote a
captured tensor before the next iteration: two iterations returned `23` instead
of `18`. Both failures were reproduced by compiling and executing planned C;
the same prepared programs without planning passed the oracle.
A subsequent dynamic-view check also exposed premature reuse of its shape
tensor: an extent query produced an output of `8` instead of `7` when the
data fill was `2`, the intended extent `2`, and a later index tensor held `3`.

The shared planner now excludes view/backing/shape bindings from reuse, retains
dynamic constructor shapes for later extent queries, and extends uses in a loop
through its end. This is a **common correctness repair**, not
part of the derived ranking policy and not an advantage credited to it. It is
conservative, may increase storage, and does not prove safety for arbitrary
opaque calls or all possible alias/control-flow patterns.

The [derivation module](reuse/module.jog) copies `mem.plan(Mod, Fn)` and its
private helpers into an inactive compiler module. It finds the slot-loop guard,
checks the assignment and type/lifetime comparisons, resolves operands at the
edit site, inserts the ranking call, and retargets the driver's existing call.
Both model and compiler edits use ordinary source-defined functions and existing
IR APIs. The result is a loadable `.jog` module; no C++ implementation, generated
header, or memory-specific core hook was added for derivation. The initial
matcher depended on source binding names. The structural revision below removes
that dependency but still matches a specific control/dataflow structure; neither
version is a general algorithm recognizer or promises arbitrary compatibility.

The [regression fixture](../../test/data/mem_safety.jog) and
[C oracle](../../test/data/mem_safety_main.c) cover direct/chained/returned views,
dynamic view/constructor extents, both branch outcomes, zero to three loop
iterations, nested loops, and a slot-selection case. The latter intentionally distinguishes the rules: two
available `f32` slots of capacities 4 and 16 precede a request of 16 elements.
The original rule produces capacities `[16,16]`; the derived rule produces
`[4,16]`. Both C artifacts satisfy the same numerical oracle. **This synthetic
fixture checks that the internal edit has an effect; it is not a model-memory
result, an allocator benchmark, or an inference speedup.**

`mem-safety` checks prepared and original-planned C plus repeated-plan equality.
`derive-memory` serializes and loads the derived compiler, runs that same oracle,
checks repeated-plan equality and the changed selection, rejects an ambiguous
driver and a changed source guard without publishing output, and checks that
the installed source file is unchanged. These checks do not establish general
numerical equivalence or compatibility across upstream revisions.

Reproduce from the repository root:

```sh
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build -R '^(mem-safety|derive-memory|mem-execution)$' --output-on-failure
```

To inspect the complete derived module, the test writes
`build/derive-memory-test/modules/planned/module.jog`; its model IR and C are in
`build/derive-memory-test/execution/`. The source-only derivation command is:

```sh
./build/joggle run reuse.derive paper/experiments/reuse/compiler.jog \
  -M paper/experiments -M modules -M build/modules
```

The local suite passed 43/43 tests with the configured ONNX, TFLite, and SAT
build options disabled. This is not a fresh check of those optional builds.
At this regression-check stage, no latency, RSS, or complete-model storage
measurements had been run. The subsequent bounded model check is below.
Source hashes at the initial prototype check distinguish it from the pre-fix
audit above; the current structural recipe has a separate hash below:

| Source | SHA-256 |
| --- | --- |
| Repaired `modules/mem/module.jog` | `f6188a5ea043e97a4e462db222cc352e73fcba1859d1541349d209e936e99055` |
| `reuse/module.jog` | `e3fd02b0a3ed265c6e93fec3f236841e90688837ff1fb743e5d3983e71cf7881` |
| `test/data/mem_safety.jog` | `a75dda189a9e32ba749c31f857e38bdef93e18d73fce7ff2546298f938398931` |

### Complete-model storage check

The first utility check uses the existing UltraFace RFB-320 canonical IR and
stored ONNX Runtime reference outputs from the earlier block pilot, with the
same input and weights. This is an exploratory mechanism check on one input,
not a task-accuracy evaluation, latency experiment, or cross-compiler benchmark.
The current `c.prepare` is applied once; original and derived planners receive
that same prepared IR. Both then use `c.place("static")` and the same C emitter.
No blocking, vectorization, operator-specific implementation, or weight change
is introduced by this comparison.

The [checker](reuse/check.py) builds both emitted programs with Apple Clang 17,
strict C99 warnings, and `-O2`. It invokes each artifact twice, starting each
output array with NaNs, rejects non-finite results, and checks all 8,840 scores
and 17,680 box coordinates against the stored references. The absolute-error
threshold is `1e-5` with no relative allowance, chosen before this check; it is
a diagnostic gate, not an assertion of the original benchmark's tolerance.
Two repeated calls do not represent two independent model inputs.

| Quantity | Original rule | Derived rule |
| --- | ---: | ---: |
| Declared static `float` slots | 6,837,920 B | 6,530,720 B |
| Declared static `int64_t` slots | 64 B | 88 B |
| Total declared static slots | 6,837,984 B | 6,530,808 B |
| Object-file `__DATA,__bss` | 6,837,952 B | 6,530,752 B |
| Maximum score absolute error | 2.98023224e-7 | 2.98023224e-7 |
| Maximum box absolute error | 3.57627869e-7 | 3.57627869e-7 |

Both variants have 27 float slots and four integer slots; their outputs are
bit-identical across variants and repeat calls. Generated headers and weight
blobs are identical. Declared slot storage decreases by 307,176 B (4.4922%);
integer storage increases by 24 B and is included rather than discarded.
Object BSS decreases by 307,200 B. The different totals reflect the compiler's
emitted object representation; declared source capacity and object sections
are separate metrics. Neither measures peak RSS, total workspace, nor live
memory. Inputs, outputs, weights, unplanned automatic temporaries, alignment,
and runtime overhead are not included in declared-slot accounting.

The [machine-readable record](reuse/result.json) contains compilation commands,
platform, per-call error counts, target scalar-size accounting, object section
reports, and input/reference/IR/C/header/weight/checker hashes. The canonical
IR hash is unchanged from the earlier record above. The Joggle executable hash
was `55b0c874099162ccf5007d4317aedb11743e5c43031dc9ee09f84607a318b9a6`,
at repository base `62ed2a83efd47021a3272620aa60b2a041bd3964` with the source
changes and hashes recorded in this study. The generated model artifacts remain
under the ignored `build-study/derivation/ultraface/` directory. The JSON is an
assistant-produced execution record; author verification remains pending.

Reproduce using the previously recorded local model/reference fixtures:

```sh
mkdir -p build-study/derivation/ultraface/{original,derived,modules/planned}
./build/joggle run reuse.derive paper/experiments/reuse/compiler.jog \
  -M paper/experiments -M modules -M build/modules \
  > build-study/derivation/ultraface/modules/planned/module.jog
./build/joggle run c.prepare build-study/ultraface-block/canonical.jog \
  -M modules -M build/modules > build-study/derivation/ultraface/prepared.jog
./build/joggle run mem.plan build-study/derivation/ultraface/prepared.jog \
  -M modules -M build/modules > build-study/derivation/ultraface/original/planned.jog
./build/joggle run planned.apply build-study/derivation/ultraface/prepared.jog \
  -M build-study/derivation/ultraface/modules -M paper/experiments \
  -M modules -M build/modules > build-study/derivation/ultraface/derived/planned.jog
for variant in original derived; do
  artifact_dir="build-study/derivation/ultraface/$variant"
  ./build/joggle run c.place "$artifact_dir/planned.jog" --arg '"static"' \
    -M modules -M build/modules > "$artifact_dir/placed.jog"
  ./build/joggle emit c.source "$artifact_dir/placed.jog" --arg '"weights"' \
    -M modules -M build/modules > "$artifact_dir/model.c"
  ./build/joggle emit c.header "$artifact_dir/placed.jog" --arg '"weights"' \
    -M modules -M build/modules > "$artifact_dir/model.h"
  ./build/joggle emit c.data "$artifact_dir/placed.jog" \
    -M modules -M build/modules > "$artifact_dir/weights.bin"
done
python3 paper/experiments/reuse/check.py build-study/derivation/ultraface \
  build-study/inputs/ultraface-rfb-320 > build-study/derivation/ultraface/check.json
```

The remaining fixed model inputs have not been checked with this storage
change. This result establishes a useful local effect on one complete model;
it does not establish a generally better allocator or faster inference.

### Pinned external interface audit

**Selected external control.** Use the pinned TVM/Relax planner's natural
source-level route, rather than inventing a Transform/xDSL wrapper around the
Joggle recipe. The controlled change will replace only the allocation ranking
inside `TokenAllocatorMixed`, retain TVM's existing token eligibility and rewrite
pipeline, and run on one frozen fixture with an independently checked artifact.
Record initial build/rebuild scope, repeated compilation feedback time,
correctness, and planned storage separately. Because TVM already implements a
capacity-indexed policy, the result will compare customization and rebuild
boundaries, not claim a new ranking algorithm or default-plan advantage. The
route was executed on September 16, 2026; see
[External natural-route control](#external-natural-route-control).

TVM's existing project pin is `c7b458e946bc4266915da582457476bdcd9705ae`.
Its [Python factory](https://github.com/apache/tvm/blob/c7b458e946bc4266915da582457476bdcd9705ae/python/tvm/relax/transform/transform.py#L480-L500)
has no ranking-policy parameter. Its
[C++ planner](https://github.com/apache/tvm/blob/c7b458e946bc4266915da582457476bdcd9705ae/src/relax/transform/static_plan_block_memory.cc#L199-L259)
already searches a capacity-indexed pool, preferring an adequate block before
growing a smaller one. Thus capacity-aware selection is not a new algorithm
contributed by this study. The same file exposes device size/scope hooks
(lines 132–178) and separates initialization, allocation, and rewriting
(lines 955–980); claiming that TVM has no customization facilities would be
incorrect.

The bounded source-level distinction is an internal rule versus a public
parameter: changing this ranking is not an argument to that factory. A native
replacement or source modification is a valid comparison route. The executed
control below measures its build and rebuild boundary; source reading alone is
not a productivity experiment, and integration or maintenance effort remains
unmeasured. Author verification of this comparison remains pending.

**ONNX-MLIR source audit (not executed).** At the project pin `4a13c34`,
ONNX-MLIR's storage decisions are upstream MLIR passes wired in
`src/Compiler/CompilerPasses.cpp`: `OneShotBufferizePass` with function-boundary
bufferization (lines 355–360), then `BufferLoopHoisting`,
`buildBufferDeallocationPipeline`, `OptimizeAllocationLiveness`, and
`ConvertBufferizationToMemRef` on both the Krnl path (lines 480–492) and the
Linalg path (lines 376–385). The only public storage option is
`--enable-krnl-buffer-reuse` (`src/Compiler/CompilerOptions.cpp` lines
261–266), which reuses a buffer within one elementwise lowering
(`src/Conversion/ONNXToKrnl/Math/Elementwise.cpp` lines 84–85); there is no
cross-operation slot-ranking decision to edit. The natural route for the
studied change is therefore a new C++ pass, or an edit to upstream
bufferization at the pinned LLVM `1053047a`, followed by an ONNX-MLIR (and
possibly LLVM) rebuild. This is a source observation with the same shape as
the TVM route, not a measured result; a runnable ONNX-MLIR control remains
undone.

### External natural-route control

Executed September 16, 2026 (18:35 UTC) on the same macOS 15.7 arm64 laptop
used for the Joggle checks. Records and scripts are preserved under
[reuse/external](reuse/external): `summary.json` aggregates `default.json`,
`ranked.json`, `default-after-revert.json`, `joggle.json`, and
`rebuild_cycle.log`; `tvm-ranking.patch` is generated by `make_patch.py`;
`plan_ultraface.py`, `joggle_route.py`, `rebuild_cycle.sh`, and
`build-initial.sh` (with `build-initial.log`) reproduce the runs. Generated libraries, converted graphs,
and the TVM build stay under the ignored `build-study/tvm-control/`. These are
assistant-produced execution records; author verification remains pending.

**Fixture.** The same UltraFace RFB-320 ONNX graph
(`34cd7e60aeff28744c657de7a3dc64e872d506741de66987f3426f2b79f88017`), the same
`input.bin`, and the same stored ONNX Runtime references as the Joggle study
(hashes repeated in each JSON record). TVM's Relax ONNX frontend rejected the
opset-9 graph at `Slice`, reading its attributes as inputs, so the graph was
converted with `onnx.version_converter` to opset 13
(`8e4a0ccb60fd2a0ec415be3c5502d701aa732a8a01c9a5b8a6e656b3986e019f`) before
import. Joggle consumed the original graph through its existing canonical
path. The conversion is a frontend-compatibility step; the reference outputs
are unchanged.

**TVM route.** TVM `c7b458e`, built from source on September 13 with
`USE_LLVM=OFF` (`build-make`, the recipe in
[`baselines/tvm/README.md`](README.md)). The pip wheel route
is excluded by construction: the factory has no ranking parameter and the
planner is C++. `tvm-ranking.patch` replaces Steps 2–4 of
`TokenAllocatorMixed::RequestReuse` (windowed best-fit, then enlarge-smaller)
with the growth, capacity, storage-id ranking that mirrors `reuse.better`;
token eligibility, the dynamic-size branch, and the rewrite pipeline are
unchanged (29 lines replaced by 37). Applying the patch recompiled one
translation unit and relinked `libtvm_compiler.dylib` in 8.03 s; reverting it
took 7.65 s (`cmake --build build-make --parallel 10`). Planning runs
`from_onnx` followed by DispatchSampling, DispatchSortScan, LegalizeOps,
RewriteDataflowReshape, ToNonDataflow, RemovePurityChecking, CallTIRRewrite,
and StaticPlanBlockMemory; the median of five fresh imports is 0.151 s
(default) and 0.153 s (ranked), and the five plans per variant are identical.
The executed artifact uses `relax.build(target="c")` and `export_library`
(1.35 s default, 1.39 s ranked), then two VM calls. Both variants pass the
same oracle: maximum absolute error 2.61e-7 (scores) and 3.58e-7 (boxes),
zero failed elements at 1e-5, repeated outputs identical.

**Planned storage.** Default: 14 `memory.alloc_storage` tokens, 5,634,560
bytes, all float32. Ranked: 9 tokens, 5,694,400 bytes (+59,840 bytes,
+1.06%). Two `builtin.alloc_tensor` calls remain unplanned in both variants.
After the revert the default plan is identical to the first run. These byte
totals are not comparable to Joggle's declared static slots: the lowering,
fusion, and accounting differ. The comparison is between customization routes,
not between planners.

**Joggle route.** Five fresh rounds of the recorded reproduction block on the
same host with the recorded tool
(`55b0c874099162ccf5007d4317aedb11743e5c43031dc9ee09f84607a318b9a6`). Every
round reproduces the recorded `planned.jog` and derived-module hashes and
passes the oracle (maximum absolute error 3.58e-7; declared slots 6,837,984
to 6,530,808 bytes). Medians: `reuse.derive` 0.041 s; `c.prepare` 4.64 s;
`mem.plan` 2.40 s; derived `planned.apply` 2.48 s; `c.place` 0.29 s; the three
emitters 2.84 s (original) and 2.90 s (derived); compiling and checking both
artifacts 2.42 s. No compiler rebuild occurs.

**Reading.** Both routes carry the same internal rule to an independently
checked artifact. TVM's natural route requires a from-source compiler build
and a rebuild per edit (8 s incremental here; a clean `USE_LLVM=OFF` source
build of 558 translation units took 514 s at `--parallel 10`,
`build-initial.log`), and its native
planning step is about sixteen times faster than Joggle's interpreted planner
(0.15 s against 2.5 s). Joggle's route needs no rebuild but pays interpretation
cost at every stage. The same rule lowered Joggle's first-fit planner's
declared slots by 4.49% and raised TVM's already capacity-aware plan by 1.06%,
confirming that the rule is not an algorithmic contribution. Not measured:
developer effort, maintenance across upstream changes, an LLVM-enabled build,
other models, or a second host.

```sh
# TVM (pinned checkout under $TVM_ROOT with the build-make source build)
export TVM_LIBRARY_PATH="$TVM_ROOT/build-make/lib" PYTHONPATH="$TVM_ROOT/python"
"$TVM_ROOT/.venv/bin/python" paper/experiments/reuse/external/plan_ultraface.py \
  --label default --opset 13 --out build-study/tvm-control/default.json
paper/experiments/reuse/external/rebuild_cycle.sh   # patch, rebuild, measure, revert
# Joggle
python3 paper/experiments/reuse/external/joggle_route.py \
  --out build-study/tvm-control/joggle.json --repeat 5
```

### Source-patch control and revision behavior

The [control runner](reuse/compare.py) compares the existing derivation recipe
with a [one-hunk source patch](reuse/source.patch). Both call the same
`reuse.better` ranking function and retain the original driver and public
`mem.bound` dependency. The source route extracts only `plan(Mod, Fn)` and
its private helper closure into a separate module, renames the entry to
`ranked`, then applies the patch with `git apply --ignore-whitespace` and checks
the module. The extraction uses explicit source boundaries for this study,
not a general parser or the IR-cloning mechanism being evaluated. Unrelated
public memory APIs are not copied. Generated snapshots stay in `build-study`;
the maintained delta is a small patch, not a duplicate source module checked
into the project.

The comparison is local to Joggle. It tests source-level versus IR-level reuse,
not TVM/MLIR developer effort, and does not count lines as productivity. The
source-patch path adds no native build requirement; neither route rebuilds
Joggle. Both routes still require parsing, checking, and executing source
modules, followed by compilation of emitted C for the numerical check.

| Source event | Source extraction and patch | IR derivation |
| --- | --- | --- |
| Current repaired planner | Oracle passes; UltraFace plan matches recorded artifact | Same |
| Private helper `use_ends` renamed | Oracle passes; same UltraFace plan | Same |
| Capacity list `counts` renamed | Patch applies; module check rejects unknown binding | Oracle passes; same UltraFace plan |
| Nine local bindings renamed together | Patch does not apply | Oracle passes; same UltraFace plan |
| Selection guard `< 0` changed to `<= 0` | Patch does not apply | Guard matcher rejects; no serialized output |
| Planner from commit `62ed2a8`, before lifetime repairs | Constructs and type-checks; numerical oracle fails | Same |
| Those old generated bodies loaded with current public dependencies | Numerical oracle still fails | Same |

The renaming/guard cases are deliberately constructed perturbations, not
observed upstream releases. Binding renames are confined to `plan(Mod, Fn)`;
comments, string-valued metadata keys, and public APIs outside that function
are unchanged. The first control used a raw text replacement for `counts`,
which also changed the `mem.counts` key; that was not a pure renaming test.
The current runner corrects the fixture and reruns the checks, rather than
using the earlier failed construction as evidence of alpha-renaming behavior.
The historical source comes from the actual pinned
commit and has SHA-256
`c16c17b0460707252d5d749a72da771ca62ae5c11d571913d57902a9630f6d34`.
The oracle failures include the original view result `14` instead of `9` and
loop result `23` instead of `18`. Prepared, unplanned C passes in these runs;
the failure is in planned execution, not source loading or C compilation.
Regenerating either route against the current repaired source passes the same
oracle without changing its patch/recipe. Merely changing the installed source
does not refresh private bodies in an already generated module.

For current and helper-renamed sources, both routes produce an UltraFace
planned-IR SHA-256 of
`f0ce246fd540db81b1d563c9642be2ae96b1699f34ab01e3d7c3efc464f37ec7`,
byte-identical to the artifact validated in the preceding section. The updated
derivation also produces that same hash for both local-binding renames. This check
reuses that artifact's numerical evidence; it is not an additional independent
model-input measurement. The [execution record](reuse/control.json) retains
construction statuses, numerical failure diagnostics, and source/artifact
hashes. `control_conditions_met` means the expected successes **and expected
failures** occurred; it does not mean every generated planner is correct.
This assistant-produced record remains subject to author verification.

Reproduce after preparing the model artifacts in the preceding section:

```sh
python3 paper/experiments/reuse/compare.py \
  > build-study/derivation/control.json
```

### Structural operand selection

The current recipe has SHA-256
`ca2d2a78bc7fd0c1c6ecd0a2f07aae05488058f6ac3a2c3db9a0fc92fa2d4549`.
It no longer calls `ir.name` to locate local bindings. Its edit-site conditions
are explicit:

1. Locate the unique `< 0` guard on the state carried by the inner slot loop.
   Obtain the slot and item iterators from their block arguments.
2. Follow the guard's users through the two short-circuit conjunctions. Check
   their false-arm forwarding, indexed type equality, strict lifetime ordering,
   and the selected-slot assignment and yield. Extra side effects in those
   matched blocks or a changed branch structure are rejected.
3. Locate the unique post-selection capacity-growth comparison. Project its
   branch-carried operands back through branch inputs to the surrounding item
   loop. Recover demand and capacity from the indexed operands, then check the
   indexed write and yielded updated capacity. Crossing another loop during
   this projection is rejected rather than guessing its recurrence.
4. Insert the ranking call using those value identities. Ordinary checking
   still validates the edited module; the driver is retargeted only after the
   local edit succeeds.

The [regression check](reuse/check.cmake) now executes the numerical oracle and
repeated-plan check against both original and renamed source bindings. It also
rejects five separate changes without serialized output: `< 0` to `<= 0`,
conjunction to disjunction, strict to non-strict lifetime comparison, assignment
of `slot + 1`, and writing `counts[item] + 1` to the selected capacity. The
existing ambiguous-driver rejection is retained. These are bounded structural
conditions and counterexamples, not a proof of correctness for arbitrary
changes to allocation, alias analysis, iteration domains, or effects.

**Interpretation.** One textual edit remains sufficient for the unchanged
source. The structural recipe now tolerates the two tested local-binding
renames without modification; the unchanged text patch does not. This is a
specific robustness difference, not measured programmer productivity. A
maintainer could adapt the patch or use structured source tooling; neither
repair effort nor those alternatives have been measured. The IR recipe also
contains more checking logic and can reject harmless structural refactorings.
Both routes inherit helper repairs when regenerated and retain old private
bodies otherwise. Automatic refresh remains a separate design obligation.

This revision uses existing function, block, value, and editing facilities;
it adds no memory-specific core hook. An external framework's natural
customization route and isolated cost measurements remain unexecuted. Do not
promote this control or the within-Joggle storage reduction to an external win.

### Second derivation case: guard folding in C preparation

Executed September 17, 2026, same host and tool
(`55b0c874099162ccf5007d4317aedb11743e5c43031dc9ee09f84607a318b9a6`).
Recipe [guard/module.jog](guard/module.jog), driver template
[guard/compiler.jog](guard/compiler.jog), route [guard/route.sh](guard/route.sh),
negative controls [guard/reject.sh](guard/reject.sh) with
[reject.log](guard/reject.log), record [guard/result.json](guard/result.json),
latency trials [ultraface-guard-same-host.csv](../data/ultraface-guard-same-host.csv).
Generated modules and artifacts stay under the ignored
`build-study/derivation/guard/`. Assistant-produced; author verification pending.

**Edit.** `c.prepare(Mod)` exposes portable bodies and simplifies them through
the private helper `prepare_with`. The recipe clones `c.prepare` into the
driver module; derivation copies the private closure with a `folded_` prefix
(20 helpers, the same closure as the earlier `c.prepare` pilot). It follows
the copied call edge to `folded_prepare_with`, requires exactly one
`opt.basic` call and one `ir.trim` call in the same block with the subject as
argument, requires the simplification result to drive the `changed` branch,
and retargets that one call to the source-defined `guard.cleanup`, which runs
`opt.basic`, `bounds.fold`, `opt.fold`, and `opt.basic` again. The driver's
single `c.prepare` call is retargeted to the derived procedure. The installed
`modules/c/module.jog` is unchanged (`c82e0ddd…`).

An earlier attempt inserted the three calls directly and joined their results
with `operator ||`; the serialized module re-parsed as short-circuit `||`, so
the inserted passes did not run. Retargeting to a source-defined policy avoids
that hazard; the observation is recorded because it bounds what a direct
structural insertion may assume about serialization.

**Consequence on UltraFace.** Interval bounds prove 18 unpadded convolution
guards true and simplify 5 padded ones (`h < h_size` and `w < w_size` fold;
`h >= 0` remains where padding is real). Emitted `if (` count falls from 106
to 88; `model.c` shrinks from 205,348 to 203,146 bytes; weights are identical;
the derived `model.c` is byte-identical to the manually sequenced passes
`bounds.fold`, `opt.fold`, `opt.basic` on the installed prepared program.
Outputs are bit-identical to the installed artifact (checksum
`0dc3b1bb13b2662e`), maximum absolute error against the ONNX Runtime
references 2.98e-7 (scores) and 3.58e-7 (boxes).

**Cost and latency.** Derivation 0.065 s; derived preparation 9.56 s against
4.64 s for the installed procedure (the added folding passes run through the
interpreter over the exposed program); plan, place, and emission 5.64 s.
Same-host latency over 20 balanced fresh processes: installed 42.905 ms,
derived 43.089 ms (median absolute deviations 0.326 and 0.428 ms). The
removed guards do not change latency; the C compiler already predicts or
hoists them. This case therefore establishes an analysis-driven edit reaching
a checked artifact through a derived preparation procedure, not a speedup.

**Negative controls.** An ambiguous driver with two `c.prepare` calls, a
helper with a duplicated `opt.basic` call, a helper whose `opt.basic` call is
replaced by `opt.dce`, and a helper whose simplification result is negated
before the `changed` branch (so the call no longer drives the flag directly)
are all rejected before serialization
([reject.log](guard/reject.log)).

```sh
paper/experiments/guard/route.sh     # derive, prepare, plan, place, emit, compile, compare
paper/experiments/guard/reject.sh    # negative controls
python3 paper/experiments/reuse/external/bench/driver.py --trials 20 \
  --systems joggle "joggle_guard=joggle:build-study/derivation/guard/derived" \
  --out build-study/tvm-control/bench/same-host-guard
```

### Manuscript evidence map

#### Invocation-cost source audit

`E-invocation-source` records source inspection, not a timing experiment:

- `src/ir.cpp`, `Mod::clone`: cross-store derivation captures the resolved
  private-helper closure, then copies the complete destination store as its
  rollback snapshot before copying any helper or requested function. Closure
  discovery precedes that snapshot; neither component has an isolated timing.
- `src/eval.cpp`, `Eval::read`: an uncached query copies the subject store,
  clears the copied query cache, verifies the copy, and compares revision and
  printed structure around execution. The direct `Fn` overload uses this same
  path but does not retain a named-query result in the subject cache.
- `src/eval.cpp`, named `query`: the result cache includes environment identity,
  environment epoch, subject revision, function text, and argument values.
  This is not fine-grained dependency tracking.
- `src/eval.cpp`, `Eval::sequence`: the sequence copies the complete subject
  store as its rollback backup and verifies the input before entering its
  per-function timer. Each timed step includes resolution, execution, and
  result verification; failure restores the saved store. Summing these step
  timers therefore omits the initial backup and input verification and is not
  end-to-end invocation latency.

The rollback covers the represented store, not arbitrary native or filesystem
effects. No concurrent-publication guarantee follows from this implementation.
These observations motivate the cost breakdown in the active plan. They do not
establish the magnitude or dominant source of compilation overhead.

The manuscript comments use the following scoped evidence IDs. These identify
assistant-inspected records, not human-verified or submission-approved claims.
Author verification is pending for each row. No new measurements were made
when transferring these results to the draft.

| Claim ID | Evidence ID and locator | Draft use and boundary |
| --- | --- | --- |
| `C-invocation-cost` | `E-invocation-source`: the source audit immediately above | System realization: store-copy, validation, rollback and cache scope; no measured overhead or incremental-update claim |
| `C-derive-storage` | `E-reuse-result`: [result.json](reuse/result.json), `variants`, `variant_outputs_identical`, `weights_identical` | Abstract and Evaluation: 4.49% lower declared slots, two calls on one input; not RSS or acceleration |
| `C-derive-control` | `E-reuse-control`: [control.json](reuse/control.json), `cases.current`, `cases.helper_rename`, `cases.operand_rename`, `cases.binding_rename` | Evaluation: unchanged structural recipe tolerates tested renames; source patch obtains the same baseline plan and can be repaired |
| `C-derive-boundary` | `E-reuse-control`: `cases.historical`, `stale`; `E-reuse-check`: [check.cmake](reuse/check.cmake), five `reject_change` calls, exercised by `derive-memory` | Evaluation and Discussion: structural rejection is not semantic equivalence; stale private bodies require regeneration |
| `C-derive-external` | `E-reuse-external`: [summary.json](reuse/external/summary.json), `tvm.incremental_rebuild_seconds`, `tvm.planned_storage`, `tvm.oracle`, `joggle.stage_seconds_median`, `joggle.matches_recorded_study`; [tvm-ranking.patch](reuse/external/tvm-ranking.patch) | Evaluation, Discussion, Conclusion: same rule through TVM's source route needs a compiler rebuild and yields +1.06% planned storage under the same oracle; one model, one host; not plan quality across systems, developer effort, or inference speed |
| `C-same-host` | `E-same-host`: [ultraface-same-host.json](../data/ultraface-same-host.json) `summary_ms`, [ultraface-same-host.csv](../data/ultraface-same-host.csv); runner [bench/driver.py](reuse/external/bench/driver.py); TVM export options [tvm_export_O2.json](reuse/external/bench/tvm_export_O2.json) | Evaluation and Discussion: Joggle C within 1.15x of TVM's unscheduled C target, both 13 to 15x behind ONNX Runtime, one host, one thread, one model; not tuned-TVM or cross-host performance |
| `C-interpreter-cost` | `E-profile-sample`: [profile-plan.txt](reuse/external/profile-plan.txt), [profile-emit.txt](reuse/external/profile-emit.txt) (macOS `sample`, 2 s each, release `-O3` build) | Evaluation and Discussion: top-of-stack time is allocation, string-keyed attribute copies, and text-constructed types; qualitative attribution, not a measured speedup |
| `C-derive-guard` | `E-guard-result`: [guard/result.json](guard/result.json) `artifact`, `oracle`, `same_host_latency_ms`; [guard/reject.log](guard/reject.log); recipe [guard/module.jog](guard/module.jog) | Evaluation: second derivation on `c.prepare`'s private helper; 18 guards removed, bit-identical outputs, no latency change; rejection of ambiguous or restructured inputs; not a speedup or general simplification claim |
| `C-onnxmlir-audit` | `E-onnxmlir-source`: the ONNX-MLIR source audit above (file and line locators at `4a13c34`) | Evaluation: one sentence stating the same route shape; no executed or measured ONNX-MLIR result |
| `C-derive-composition` | `E-reuse-path`: reproduction commands in [Complete-model storage check](#complete-model-storage-check), [compiler.jog](reuse/compiler.jog), [module.jog](reuse/module.jog), `derive`; `E-reuse-result`: variant artifact hashes and oracles | Method: derive compiler code, run the original/derived planners on the same prepared model, and reuse unchanged placement/emission functions; no claim that planner derivation performs exposure or incremental recomputation |

The method and Figure 3 excerpt the current [recipe](reuse/module.jog): its
`capture`, `conjunction`, and `derive` definitions supply the structural checks
and typed insertion. The code excerpt omits matching and cleanup steps only
for presentation; the executable recipe retains them. The older C-preparation
pilot remains evidence of isolation and an inactive edit, not a storage result.

The manuscript uses these as distinct mechanism observations, not a combined
ablation. The storage case edits compiler definitions and then the model's
storage plan. The separate [MobileNetV2 trace](progressive-trace.md) records
selection and exposure in the subject model, not a derived compiler algorithm.
In Discussion, choosing a policy parameter when sufficient and deriving a body
when the internal edit exceeds that interface is a design interpretation, not
a measured usability advantage. Source-patch results remain a valid control.
