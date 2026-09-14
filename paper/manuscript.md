# Joggle: Malleable Inference Compilation with a Progressive Function IR

Working manuscript for the EuroSys 2027 fall cycle. The current file is
an argument draft, not a submission-ready paper. Pilot values are labeled and
must be replaced by frozen experiment results.

## Abstract

Inference co-design experiments often alter source semantics, loop structure,
storage policy, and executable artifacts together. Production compilers expose
these decisions through distinct graph, tensor, loop, and target abstractions,
so a bounded experiment can require new representation objects, conversions,
pass registrations, and backend cases. Joggle tests whether one typed function
IR can provide a smaller extension boundary without moving the same complexity
into operator-specific compiler code. Imported operations, reusable tensor
semantics, explicit loops, memory decisions, and artifact calls remain
`Fn`/`Blk`/`Op`/`Val` objects. Decoders, analyses, transformations, planners,
and emitters are ordinary typed module functions. Transactional editing and
whole-program verification make a failed composition leave the input unchanged.
We evaluate this design with frozen extension tasks, staged execution of
conventional models, compiler cost, numerical correctness, workspace, code
size, and latency. The current implementation executes ten ONNX models through
strictly compiled C. On MobileNetV2, an operator-independent affine policy
rewrites 54 loop bodies, reduces generated source by 20.8%, and reduces latency
by 26.5% in a same-runner Linux diagnostic while preserving the stored output;
the result remains 12.24 times slower than one-thread ONNX Runtime. These results
show that separately distributed modules can change real inference bodies and
carry the edits to executable artifacts. They also expose the boundary of the
approach: one extensible representation does not substitute for production
kernel libraries or automatic target tuning.

## 1. Introduction

The unit of change in neural-network hardware/software co-design is rarely an
isolated graph operator. A new numeric format changes types and constants; a
new accelerator primitive changes the implementation chosen for a family of
calls; a storage experiment changes lifetimes and interfaces; and a scheduling
study must observe and rewrite loops before an artifact target accepts the
program. These changes cross boundaries that production compilers introduce
for good reasons, including stable frontend semantics, target-independent
optimization, scheduling, code generation, and runtime deployment.

MLIR makes such multi-level systems reusable through extensible dialects and
passes [Lattner et al. 2021](https://doi.org/10.1109/CGO51591.2021.9370308).
TVM combines graph optimization, tensor programs, schedules, and target-aware
search [Chen et al. 2018](https://www.usenix.org/conference/osdi18/presentation/chen).
ONNX-MLIR represents ONNX and loop-level computation in distinct dialects
[Jin et al. 2020](https://arxiv.org/abs/2008.08272), while IREE extends an MLIR
stack through deployment artifacts and runtimes that scale to embedded systems
[Liu et al. 2022](https://doi.org/10.1109/MM.2022.3178068). These systems target
capabilities and deployment breadth beyond the scope of a small research
workbench.

This paper asks a different question: what is the smallest compiler substrate
that remains useful when a co-design researcher must change several of these
boundaries at once? “Small” alone is not a contribution. Removing distinct IRs
can merely move complexity into special cases, weaken verification, or produce
poor code. A useful answer must preserve typed structure, permit independent
extensions, compose deterministically, expose unsupported programs, and
produce executable artifacts whose limitations can be measured.

Joggle tests one answer. Its program state consists of functions, blocks,
operations, values, structural types, and open attributes. An operation may
initially call a source-schema function such as an ONNX operation, later call
an inspectable neural-network function, and finally contain explicit loops and
scalar operations after function expansion. It does not migrate between graph,
tensor, loop, and target IR object models. Module functions inspect or mutate
this state through the same public API; reading a model, inferring types,
selecting an implementation, planning storage, and emitting C are explicit
invocations rather than privileged pipeline stages.

This design is influenced by the function and rewrite orientation of Lift
[Steuwer et al. 2017](https://doi.org/10.1109/CGO.2017.7863730) and RISE & Shine
[Steuwer et al. 2022](https://arxiv.org/abs/2201.03611), but it does not impose
a closed data-parallel pattern vocabulary. It also differs from tile-level
languages such as TileLang, which expose GPU memory, layout, thread binding,
and scheduling concepts to kernel authors
[Wang et al. 2025](https://arxiv.org/abs/2504.17577). Joggle leaves such
concepts to user modules and asks whether the core function representation is
sufficient to host them. The evaluation must determine where that choice
succeeds and where a dedicated representation is necessary.

The paper makes three candidate contributions:

1. A function-oriented compiler design that progressively exposes conventional
   neural-network models from schema calls to structured loop bodies without a
   mandatory representation boundary.
2. A source module mechanism in which frontend bridges, semantic
   implementations, analyses, transformations, capability checks, and artifact
   generation use ordinary typed functions and a transactional editing API.
3. An empirical evaluation of extension surface, composition failure,
   conventional-model coverage, numerical correctness, and generated-artifact
   quality, including negative compatibility and performance results.

The first two items are implemented design candidates. The third remains the
submission-critical study; no contribution claim is final until its protocol
and results are frozen.

## 2. Research questions

- **RQ1, progressive representation:** Can imported model calls, reusable
  semantics, explicit loops, storage decisions, and target preparation remain
  understandable and verifiable in one function representation?
- **RQ2, extension surface:** Across an end-to-end ONNX-MLIR baseline and
  task-specific mechanism controls, which files, native registrations,
  generated definitions, core changes, and build dependencies are required?
- **RQ3, composition:** Do independently defined modules compose with stable
  output, transactional failure, and useful unsupported-frontier diagnostics?
- **RQ4, artifact quality:** Under matched model, threading, and correctness
  contracts, how do generated artifacts compare with independent production
  systems? Separately, can user modules change real function bodies without a
  core or emitter modification?

## 3. Design

### 3.1 One progressive function representation

Malleability is not used as a synonym for configurability. For one installed
module and one input program, we require three observable properties. First,
the module discovers candidates through typed structure rather than frontend or
operator-name cases. Second, it can replace the selected decision through the
same public `Fn`/`Blk`/`Op`/`Val` API used by other modules. Third, the edited
program either reaches an executable artifact with the change intact or fails
at an explicit capability boundary. The extension study measures native
registrations and core changes, the composition study exercises transactional
failure, and the model study checks the artifact boundary. These are the paper's
tests of malleability; source brevity alone is not one.

The representation has seven public concepts: `Mod`, `Fn`, `Blk`, `Op`, `Val`,
`Ty`, and `Attr`. Calls and structured control flow are operations; tensor
shapes and numeric formats are structural types; source-schema fields and
target facts are open attributes. This is not a claim that every compiler
should use one IR. It is a testable constraint intended to keep a co-design
experiment readable while it moves from model-scale dataflow toward explicit
computation.

### 3.2 Semantics are executable functions

A frontend decoder preserves source operators and metadata. A separate bridge
can retarget a source call to a shared function once its schema conditions are
met. The shared function has a typed body, so a later pass can inspect the call,
replace it with another compatible implementation, or expand its body into the
caller. The artifact target handles only scalar, memory, call, and control-flow
forms. For example, variadic ONNX Min and Max are first converted into binary
chains of ordinary broadcast-capable `nn.minimum` and `nn.maximum` functions;
the C emitter has no Min, Max, or ONNX case.

### 3.3 Extensions are module functions

A module is a source package with typed functions, dependencies, visibility,
and optional native bindings. Metadata selects functions for conventions such
as inference or conversion, but it does not create a second pass language.
Read-only queries, mutating transforms, decoders, and artifact functions share
the invocation model. The study will test whether this uniformity reduces the
surface and coupling of matched extensions or merely shifts complexity into
module code.
For structural transforms, an edit of one operation returns its replacement
`Op`, while a whole-module traversal returns only whether it changed the
module. This lets a source policy compose checked edits directly without a
second result abstraction, metadata marker, or module rescan.
Helpers may separately promise snapshot-relative referential transparency with
a `memo` attribute. Scalar values, types, IR capabilities, and recursive lists
participate in cache keys. Capability identity includes handle generation and
the owning store revision; result capabilities retain the same revision
dependencies, and an invocation that changes an input store is not cached.
The cache remains local to one top-level invocation, avoiding process-global
analysis state while permitting repeated structural queries within an
unchanged snapshot. Deterministic reports count both source-function
invocations and memoized returns; this makes repeated module-level analysis
visible without adding timings to the reproducible report.

Structural transformations also expose policy-facing queries before mutation.
The affine access query accepts ordinary integer expressions rather than an
operator descriptor. Besides coefficient-wise exact affine arithmetic, it can
use static half-open loop ranges to prove that an integer quotient is constant.
This recognizes grouped and tiled addresses such as `m / 160` for
`m in 0..160`, while rejecting a quotient that varies over the represented
iteration box. The rule is independent of frontend and callee name.
For scalar promotion, `tile.scalar_cost` returns zero for an illegal candidate
and otherwise reports lane count times recursive source-body operation count.
The example blocking policy can enforce a whole-invocation duplication limit
before applying split, reorder, or scalarize. This metric is deliberately a
target-neutral proxy rather than a claim to predict emitted bytes or latency;
the evaluation must determine whether it is informative enough for useful
selection.

Storage and artifact policy remain separate module functions. `mem.separate`
proves a deliberately narrow call-site relation from planned slots and
immutable payloads. The C module can consume that fact across every call to a
private function before strengthening its definition; one unclassified or
repeated pointer rejects the contract, and exported entries are never inferred.
The proof neither introduces a memory IR nor gives C semantics to the planner.

### 3.4 Capability-driven exposure and failure

Targets publish an ordinary predicate over operations. Exposure repeatedly
expands or selects implementations until the target accepts the program or a
fixed point leaves an explicit frontier. Mutating invocations are transactional:
failure must not expose a half-rewritten program. Partial ONNX models are
therefore reported by stage and remaining source calls rather than counted as
supported end to end.

Alternative implementations are ordinary overloaded functions. A read-only
module policy can inspect one compatible candidate or the complete compatible
set and can receive an ordinary configuration dictionary. The compiler checks
that a selector returns at most one member of that set. Thus a resource budget
can alter choices at individual call sites without adding a target class or an
operator-specific compiler branch; whether this interface is sufficient for a
useful co-design study remains an evaluation question.

### 3.5 Artifact interfaces are derived, not duplicated

The C target derives public symbols from qualified source functions, preserves
valid parameter and value names, and reserves compiler-generated stems for
anonymous temporaries and storage slots. Its source, header, and structured API
descriptor use the same naming and signature functions. The descriptor records
the C representation class, shape, byte count, pointer passing, and optional
weight payload for every exported entry. The application test generates its
allocation, call, comparison, and timing harness from that descriptor, including
multiple tensor inputs and outputs; it does not maintain a second model ABI in
handwritten C.

## 4. Implementation

Joggle's host implementation is a C++20 library with a public embedding API
and a small command-line driver. The host owns parsing, immutable type and
attribute values, IR storage, verification, overload resolution, module
loading, and transactional invocation. It does not contain a neural-network
operator enumeration or target-specific lowering table. Handles carry store
identity, numeric identity, and generation; edits reject foreign, stale, or
non-dominating values before changing the module. Successful changes advance a
store revision, which also bounds snapshot-relative analysis memoization.

Most compiler behavior is written in the same `.jog` language presented to an
extension author. Bundled modules supply scalar and tensor semantics, ONNX and
TFLite bridges, conservative bounds, structural loop transforms, storage
planning, a deterministic virtual machine, and C artifact generation. A module
is a directory containing one entry source, optional source fragments,
and at most one optional native library. Dependency closure and visibility are
derived from `use` declarations; a generated header, dialect table, or pass
registry is not part of the module contract.

External formats are deliberately split at the transport boundary. Optional
native codecs decode protobuf or FlatBuffer bytes while preserving source
symbols and attributes. Source bridge modules perform type refinement and
semantic conversion explicitly. Consequently, adding another frontend does
not require adding its operation names to `nn`, `tile`, or the C emitter. The
same rule applies at the other end: the C and VM modules publish ordinary
capability predicates, preparation functions, and artifact functions.

The test suite exercises the command-line and embedding paths, module
installation, native-module loading, malformed input, transaction rollback,
stale handles, structural edits, generated C execution, VM execution, and
optional ONNX/TFLite paths. Linux and macOS clean builds and a Linux
AddressSanitizer/UndefinedBehaviorSanitizer configuration run in continuous
integration. Large model artifacts remain checksum-pinned external inputs and
are excluded from the default CI path; the evaluation scripts must therefore
record explicitly which optional gates were available.

## 5. Evaluation

### 5.1 Experimental method

We use three protocols, each aligned with a research question and each kept
separate from its results. First, the model study measures progressive
representation (RQ1) with checksum-pinned ONNX Model Zoo subjects. For every
available model, the harness records four ordered gates: protobuf decoding and
canonical round trip, result-type inference, source-call conversion, and
semantic round trip. A model passes only the gates it actually reaches; a model
missing from the configured cache is omitted rather than counted. A separate
execution gate compiles generated C with strict C11 warnings and compares one
fixed input against the stored Zoo output. This is a test of the compiled
numerical path, not dataset accuracy or general operator coverage.

Second, the extension study measures the structural surface of co-design
changes (RQ2 and RQ3). Before implementing a comparison, we freeze its input,
observable requirements, oracle, counted files, and forbidden shortcuts. The
four tasks replace a matrix implementation, define a structural scheduling
policy, connect external matrix and convolution kernels, and add a parametric
saturating integer format. Joggle and each comparison system must follow their
documented extension path. We retain authored nonblank, non-comment lines,
files crossed, generated definitions, native registrations, build changes,
dependencies, artifacts, and diagnostics. A failed mandatory requirement is
reported as unsupported; we neither weaken the task nor substitute a smaller
standalone example. These measurements expose extension boundaries, but do not
measure developer time, comprehension, or usability.

Third, the artifact study measures RQ4 with schema-2 manifests that name one
Joggle artifact and one independently implemented ONNX Runtime subject. Each
command performs three untimed warm-ups, times one inference, validates its
output, and exits; the runner launches a fresh process for each subject in each
of 20 trials. Rotated and reversed ordering balances position over complete
cycles. Both subjects receive one-thread environment settings and, on Linux,
are pinned to the same available CPU. The runner rejects malformed protocol
rows, validation failure, a dirty checkout, or unavailable affinity, and
records compiler/runtime versions, commands, host state, artifact hashes, and
raw samples. We report medians because the present shared-runner records are
diagnostic; publication measurements additionally require an identified idle
host, an explicit load threshold, stable power and thermal conditions,
dispersion, and independent-machine replication.

The generic MobileNetV2 policy is a within-workflow ablation, not an
independent-system comparison. One workflow creates plain and transformed C
from the same canonical program, verifies both against the same stored output,
then runs each artifact in a separate balanced manifest beside ONNX Runtime on
one pinned CPU. This design attributes the within-run artifact difference to
the checked-in transformation under the recorded build and input contract; it
does not make Joggle competitive with ONNX Runtime or control the shared host.

### 5.2 Compatibility and correctness

The repository currently records pilots, not publication measurements. Ten
ONNX models execute against stored reference outputs. TinyYOLOv3 retains a
type frontier and SSD-MobileNetV1 retains 386 source calls after conversion.
Across executed models, the stored maximum absolute differences range from
approximately `1.34e-7` to `2.10e-5`. These checks establish numerical paths,
not task-level accuracy.

The structural compatibility table is now generated directly from twelve
pinned Zoo tests rather than transcribed from test output. Ten models complete
semantic conversion and canonical round trip. TinyYOLOv3 retains 219 unknown
results after inference; SSD-MobileNetV1 infers all result types but retains
386 source calls after conversion. The source-call reduction comes from shared
broadcast-capable comparison functions, a maximum/minimum Clip composition,
and a generic tensor-tiling function reached through source-semantic mappings.
None adds an ONNX operator to the target emitter. Models absent from the configured cache are
omitted, not counted as passes. Structural completion is kept separate from
the ten-model numerical execution claim above.

<!-- BEGIN GENERATED: model-frontier -->
**Table 1. Pinned ONNX Model Zoo structural frontier.**

| Model | Nodes | Type inference | Semantic conversion |
| --- | ---: | --- | --- |
| mnist-8 | 12 | pass | pass |
| mobilenetv2-7 | 155 | pass | pass |
| squeezenet1.1-7 | 66 | pass | pass |
| squeezenet1.0-13-qdq | 171 | pass | pass |
| resnet18-v1-7 | 69 | pass | pass |
| tinyyolov2-8 | 33 | pass | pass |
| tiny-yolov3-11 | 291 | partial (219 unknown) | not_run |
| ultraface-rfb-320 | 242 | pass | pass |
| ssd-mobilenetv1-12 | 5985 | pass | partial (386 calls) |
| shufflenet-v2-12 | 261 | pass | pass |
| densenet-12 | 910 | pass | pass |
| googlenet-12 | 143 | pass | pass |
<!-- END GENERATED: model-frontier -->

DenseNet exposes both progress and a compiler-scaling boundary. After
conversion and selection of the then-current out-of-tree spatial
implementation used by smaller models, its 65,429,147-byte IR did not complete
`c.prepare` within a 600-second pilot cutoff at revision `741b972`. Replacing
repeated block scans
with block-local indexing and batching exposure edits reduced the same
single-run pilot to 271.92 seconds at revision `d09571a`. The resulting strict
C agrees with the official stored output within `7.6293945e-6`, making
DenseNet the tenth executed ONNX model. Its unisolated median remains 574.763
ms versus 19.673 ms for one-thread ONNX Runtime, an approximately 29.2x gap.
Neither the single compiler timing nor the latency rows are publication-grade;
preparation scaling and generated-code quality remain open problems.

The compiled application gate now checks that generated model source, public
header, structured API, and generated harness agree under strict C11 warnings.
The harness mechanism has compiled for both the official MNIST application and
a two-input/two-output interface. This establishes interface consistency, not
broader model execution or performance.

### 5.3 Compiler cost

Compiler-side scaling has a concrete mechanism pilot but not yet a formal
result. Snapshot-relative memoization binds IR handles to their generation and
owning-store revision, rejects insertion when a call edits an input store, and
revalidates result-store dependencies at each hit. On the exact-split
MobileNetV2 `spatial.block` path, a mechanically stripped control and the
checked-in candidate emitted byte-identical IR in four alternating processes.
Deterministic source calls fell from 2,939,981 to 1,272,463; two-run wall-time
medians were 42.099 and 28.341 seconds. The host was not isolated and the sample
is too small for a performance claim. The result currently establishes a safe
ablation path and identifies repeated structural analysis as measurable work.
The later UltraFace block experiment exposed a separate quadratic CSE lookup:
each candidate scanned the complete preceding module prefix before testing
block ownership. Indexing the unchanged equivalence fields per `Blk` reduces a
single `opt.basic` diagnostic from 244.26 to 11.28 seconds while producing
byte-identical IR and the same 597 edits. This is a mechanism check from one
unisolated run, not a compiler-throughput result; it also demonstrates why
end-to-end model bodies, rather than only small extension fixtures, belong in
the evaluation.
The same path exposed a path-sensitive affine-analysis key: including the
complete DFS history prevented shared SSA address expressions from reusing a
result. Keying by the verified acyclic SSA value instead, together with
revision-aware loop-local access caches, reduces recursive affine calls from
254,955 to 145,171 and one `spatial.block` diagnostic from 40.30 to 28.71
seconds. Both variants perform 15,877 edits and emit byte-identical IR. This is
also a one-run mechanism diagnostic; controlled repetitions remain required.

A deterministic MobileNetV2 structural check exposed a separate coverage
boundary in the affine proof. Coefficient-wise division alone recognized 17
reorderable contraction loops, all on the depthwise path. Adding the
range-proven constant-quotient rule recognizes 54 loops, including the
pointwise 1x1 path, without an ONNX or Conv condition. Reordering all recognized
loops preserves the official output with maximum absolute difference
`2.09808349609375e-5` under strict generated C. This is a legality and coverage
result, not a latency result. The existing scalar-blocking example takes more
than four minutes when its primitive peel edits each trigger a hidden
whole-module cleanup. Moving cleanup to the composing policy boundary makes a
bounded 4,000-unit structural run complete in 40.52 seconds, including cleanup,
planning, placement, and C emission, on the development host. Its generated C
remains a negative latency diagnostic, so the reproducible Linux policy run now
evaluates the lightweight reorder rather than presenting scalar blocking as an
optimized artifact.

Source emission exposed a third repeated-work boundary on the placed UltraFace
IR. The original external-payload path rescanned the full operation sequence
for each of 240 constants, while ABI, type, and naming queries were repeatedly
interpreted despite an unchanged snapshot. Building one value-keyed payload
layout and applying the existing revision-aware memo contract to pure queries
reduces a report-enabled run from 191.51 to 38.65 seconds. The number of
interpreted source-function bodies falls from 2,099,082 to 237,342 after memo
hits, and both variants emit byte-identical 329,117-byte C. This is a
single-host mechanism diagnostic, not a controlled compiler-throughput claim.

### 5.4 Artifact quality

Generated C is presently the main negative result. Depending on the model, the
recorded unisolated pilots are about 7--101 times slower than one-thread ONNX
Runtime. Internal A/B runs of structural rewrites are therefore excluded from
the primary performance evidence: comparing two Joggle variants can validate
that a pass changes real function bodies and preserves results, but cannot show
that Joggle is competitive. Those diagnostics establish three narrower facts.
First, one operator-independent loop policy rewrites canonical bodies across
MobileNetV2, SqueezeNet, and UltraFace. Second, a structural budget bounds code
replication before mutation. Third, call-site analysis can prove private
non-aliasing without adding an operator case when such calls remain after
specialization. The standard MobileNetV2 path contains only its exported entry,
so that analysis correctly makes no change there. The repository retains raw
internal records for regression and ablation, but the paper's performance
table will compare independent systems on identical model, input, thread, and
correctness contracts while recording each system's compiler and runtime
versions.

Table 2 reports two independently dispatched system runs. Each subject runs in
a fresh process under a one-thread contract, and each cell is the median of 20
trials. The generated model, input, weights, reference, and executable have
identical hashes across CPU classes, and every stored-output check passes.

<!-- BEGIN GENERATED: systems -->
**Table 2. Independent-system Linux diagnostics (median milliseconds).**

| CPU class | Model | Joggle C | ONNX Runtime | Joggle / ORT |
| --- | --- | ---: | ---: | ---: |
| AMD EPYC 7763 | MobileNetV2 | 167.315 | 10.346 | 16.17x |
| AMD EPYC 7763 | MNIST | 0.530 | 0.050 | 10.55x |
| Intel Xeon 8370C | MobileNetV2 | 173.787 | 7.345 | 23.66x |
| Intel Xeon 8370C | MNIST | 0.570 | 0.054 | 10.59x |
<!-- END GENERATED: systems -->

The cross-CPU results reproduce correctness and the negative performance
boundary, not one stable slowdown factor. Table 3 instead measures the generic
MobileNetV2 policy within each workflow execution on one pinned CPU. Reordering
54 affine-proved bodies and canonicalizing their index trees reduces generated
source from 192,209 to 152,275 bytes. The canonical/plain latency ratio changes
from 0.7348 to 0.7358 even though absolute latency changes substantially.

<!-- BEGIN GENERATED: policy -->
**Table 3. MobileNetV2 module-policy diagnostics (median milliseconds).**

| Run | Plain C | Canonical C | Canon / plain | Adjacent ORT | Canon / ORT |
| --- | ---: | ---: | ---: | ---: | ---: |
| A | 139.679 | 102.638 | 0.7348 | 8.382 | 12.24x |
| B | 178.796 | 131.557 | 0.7358 | 11.261 | 11.68x |
<!-- END GENERATED: policy -->

These remain shared-runner diagnostics rather than publication results. The
workflows do not control host load, temperature, or frequency, and the two
policy runs report the same processor class.

### 5.5 Extension surface

The extension-surface study and controlled performance study are not complete.
Two of four system-baseline tasks pass, one has a preserved unsupported
outcome at its first mandatory case, and one remains incomplete. These results
do not support a general claim that Joggle is easier to extend, more compatible,
or faster than another compiler.

<!-- BEGIN GENERATED: extension-surface -->
**Table 4. Frozen extension tasks and observed authored source surface.**

| Task | Joggle | TVM control | ONNX-MLIR system path |
| --- | --- | --- | --- |
| implementation | pass, 20 lines | pass, 62 lines | pass, 167 lines in six files |
| policy | pass, 69 lines | pass, 126 lines | pass, 201 lines |
| external-kernel | pass, 138 lines | pass, 355 lines | unsupported at first required MatMul case |
| numeric-format | pass, 284 lines | incomplete | incomplete |
<!-- END GENERATED: extension-surface -->

The ONNX-MLIR policy result is a separate accelerator-scoped implementation,
not a relabeling of its built-in fusion flag. In 201 non-comment lines across
seven files, it assigns caller-supplied weights to calls and other operations
and discovers one-result, one-use, equal-shape producer-consumer pairs without
matching ONNX operator names. On the unchanged fixture it finds two pairs: an
extent limit of zero accepts none and preserves three affine loops, while a
limit of 100 accepts both and produces one loop. Both native artifacts pass the
same oracle with zero maximum absolute error, and the independent measurement
fixture reports the expected weighted costs 5 and 9. The underlying ONNX-MLIR
hook is still module-wide, however, so this result demonstrates an
accelerator-scoped policy path rather than per-candidate selective fusion.

Lines are nonblank, non-comment authored source under each frozen task's
inclusion rules. They expose where an extension crosses files and registration
boundaries; they do not measure development time, comprehension, or usability.

The first reproducible extension-footprint pilot freezes four tasks and runs
their named tests. Its three out-of-tree tasks contain 20, 69, and 138 source
lines; the bundled numeric-format task contains 284 source lines across a
source semantic module, a native binding, and two target companions. These are
descriptive implementation footprints, not usability or productivity results.
The implementation contract now also has a generated, digest-pinned opset-13
ONNX fixture with its exact `2x3` and `3x2` inputs. Joggle imports that model,
expands the ordinary `ikj` function, and reproduces the same TensorProto oracle
through both its deterministic VM and strictly compiled C. The same model now
passes a native ONNX-MLIR `0.4.2` extension at pinned revision `4a13c34a` and
its documented LLVM revision. The preserved lowered IR contains explicit
`i-k-j` affine loops, and its shared library returns `[58, 64, 139, 154]` with
zero maximum absolute error.

The first three frozen contracts now also pass on a pinned TVM `v0.26.0`
baseline: one generic explicit i-k-j matrix body, one structural schedule
policy, and one generic external-call implementation exercised by the unchanged
C harness. Their exact experiment sources and build recipe are preserved, but
the numeric-format contract remains incomplete. These are mechanism-level
controls, not a substitute for an end-to-end neural-network compiler. The
ONNX-MLIR implementation task uses its documented accelerator path and adds
six files containing 167 nonblank, non-comment lines: the conversion and
accelerator class, three build files, and a runtime compatibility symbol. It
also requires one generated native registration and five global macro
definitions, but changes no existing framework-core file and adds no external
dependency. Joggle's matching body contains 20 source lines and TVM's control
contains 62. These are separately reported surface observations, not measures
of difficulty, comprehension, or developer time.

The external-kernel task exposes a distinct documented-path boundary. Although
the ONNX-MLIR driver describes `--ops-for-call=Conv MatMul` as its example,
running `--ops-for-call=MatMul` on the frozen `2x3` by `3x2` input emits three
ordinary affine loops and no `krnl.call`. At the pinned revision, Conv registers
the generic call pattern and receives the option, whereas MatMul registers only
its ordinary lowering. The exact successful command, emitted IR, source
digests, and failed mandatory requirement are preserved as an unsupported
outcome; no later requirement is counted as passing after that failure. The
numeric-format contract and an uninterrupted clean-build repetition remain
incomplete, so there is still no full matched RQ2 result or comparative
extensibility claim. Standalone MLIR type/dialect experiments may
decompose registration and conversion work, but cannot be reported as the
system comparison because they omit ONNX ingestion and artifact generation.

## 6. Related work

MLIR addresses compiler extensibility by making multiple domain-specific IRs,
operation interfaces, conversions, and pass infrastructure reusable
[Lattner et al. 2021](https://doi.org/10.1109/CGO51591.2021.9370308).
ONNX-MLIR applies that approach to ONNX semantics and loop-oriented lowering
[Jin et al. 2020](https://arxiv.org/abs/2008.08272), exposes documented
operation-generation and accelerator integration paths, and produces native
artifacts ([official documentation](https://onnx.ai/onnx-mlir/)). It is
therefore the system-level comparator. IREE extends an MLIR stack through
host/device partitioning, deployment artifacts, and embedded runtime
configurations
[Liu et al. 2022](https://doi.org/10.1109/MM.2022.3178068). Joggle does not
argue that one representation replaces these abstractions. It tests whether a
smaller source-level module boundary is sufficient for bounded experiments
whose changes would otherwise cross several of them.

TVM combines graph optimization, tensor programs, schedules, cost models, and
target code generation
[Chen et al. 2018](https://www.usenix.org/conference/osdi18/presentation/chen).
TileLang gives kernel authors explicit control over tiled dataflow, memory,
layout, and thread binding while building on TVM IR
[Wang et al. 2025](https://arxiv.org/abs/2504.17577). These systems are the
appropriate baselines for production code quality or kernel-control tasks.
Joggle instead exposes loops and storage through ordinary functions and leaves
hardware concepts in user modules. A matched study must determine whether that
choice reduces extension coupling; current generated-C pilots establish that
it does not by itself deliver competitive performance.

Lift and RISE & Shine make typed functional patterns and explicit rewrite
strategies central to optimization
[Steuwer et al. 2017](https://doi.org/10.1109/CGO.2017.7863730);
[Steuwer et al. 2022](https://arxiv.org/abs/2201.03611). Joggle shares their
emphasis on inspectable functions and transformations but keeps imported calls,
explicit structured loops, and external declarations in one open function
representation instead of requiring a closed data-parallel pattern vocabulary.
This is a design tradeoff to evaluate, not an assertion that either form is
universally simpler.

TensorFlow Lite Micro targets inference on fragmented, memory-constrained
microcontrollers through an interpreter and explicitly registered operator
implementations
[David et al. 2021](https://proceedings.mlsys.org/paper_files/paper/2021/file/6c44dc73014d66ba49b28d483a8f8b0d-Paper.pdf).
ncnn is a dependency-light C++ inference framework with optimized CPU and
Vulkan paths and a registered custom-layer interface
([project](https://github.com/Tencent/ncnn),
[extension guide](https://github.com/Tencent/ncnn/wiki/how-to-implement-custom-layer-step-by-step)).
They define the deployment context Joggle must respect: transparent compiler
artifacts are useful for research, but cannot be presented as substitutes for
mature optimized runtimes. A custom-operation comparison is valid only under a
matched model, target, and oracle.

## 7. Limitations and threats to validity

The present implementation favors visibility and extension over automatic
optimization. Dependence proofs cover conservative affine cases, scheduling
policies are manually selected, dynamic shapes remain partial, and generated C
lacks the packed kernels, vectorization strategy, and broad target tuning of a
production runtime. The single progressive IR may reduce conversion code for
some experiments while making abstraction boundaries less explicit for others.
The evaluation must report both outcomes.

The current authors built both the system and its examples, creating familiarity
and experimenter bias. Source volume is objective but not a measure of
comprehension, difficulty, or productivity. The RQ2 protocol therefore freezes
inputs, observable behavior, and forbidden shortcuts; preserves exact patches,
registrations, dependencies, build records, and diagnostics; and reports each
dimension separately. It does not combine them into an ease-of-use score.

Model coverage currently overrepresents static vision networks. Numerical
agreement with one stored input does not establish task accuracy, robustness,
or general operator support. Performance pilots now include shared Linux
runners from two CPU classes, but they cannot support cross-system speed claims
without load, frequency, and thermal control. Final results require pinned
artifacts and revisions, isolated repeated trials with dispersion, task-level
metrics, at least one non-vision workload, and an independently managed artifact
reproduction. Unsupported models and transformations that lose performance
remain part of the reported frontier.

## 8. Conclusion

Joggle explores a deliberately narrow compiler design point for neural-network
co-design research: one typed function representation and one distributable
module-function mechanism from imported calls to explicit computation and
artifacts. The implementation now demonstrates that frontends, semantic
bodies, structural analyses, loop and storage edits, implementation selection,
and two artifact paths can share this boundary. It also exposes the boundary's
costs: model support is incomplete, automatic profitability is absent, and
generated C remains far behind a production runtime. Whether the design
meaningfully lowers extension coupling is therefore an empirical question, not
an implemented feature. The controlled extension and artifact studies must
answer it before this conclusion can make a stronger claim.
