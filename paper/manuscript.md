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

## 2. Motivation: the vertical slice is the experiment

### 2.1 A co-design change crosses compiler boundaries

Consider a researcher evaluating a fused, low-precision projection on a small
edge accelerator. The idea is not only a new kernel. The imported model may
express the computation as several source operators; the numeric format changes
types and constant packing; the accelerator instruction admits only particular
tile and layout choices; the storage planner must preserve alignment and
lifetime constraints; and the artifact must expose a callable boundary for the
host. An honest experiment must be able to inspect and change every one of
these decisions, preserve a fallback, and show where an unsupported case stops.

Production compilers separate these concerns intentionally. MLIR provides
dialects, interfaces, conversions, and passes; TVM separates high-level model
representation from tensor programs and schedules; ONNX-MLIR moves from ONNX
operations through lower-level dialects; and IREE connects compiler IRs to a
deployment runtime. These boundaries support large ecosystems. For the
researcher, however, the unit of experimentation becomes a *vertical slice*
through several otherwise independent extension mechanisms. We call a point
at which the same experimental decision must be re-expressed or recovered in a
new mechanism an **extension discontinuity**. The claim is not that every
conversion is harmful. The question is whether the accumulated discontinuities
are necessary for a bounded co-design experiment.

### 2.2 Control and automation solve different halves

Kernel languages give authors precise control at a lower level. [Halide-style](https://doi.org/10.1145/2491956.2462176)
algorithm/schedule separation and the function-rewrite lineage of Lift and
RISE make transformations explicit; TileLang exposes tile, memory, layout, and
thread decisions. These systems are appropriate when the kernel is already the
unit of work. They do not by themselves define how a new source relation,
storage contract, and exported model interface travel with that kernel.

Automatic systems attack the authoring burden from the other side. [Ansor-like](https://www.usenix.org/conference/osdi20/presentation/zheng)
search explores tensor programs, while [Mirage](https://arxiv.org/abs/2405.05751) searches across graph, block, and
thread levels. [Axon](https://arxiv.org/abs/2606.26344) goes further: it synthesizes target instructions from
semantic specifications, explores tiling and fusion, and checks equivalence
with SMT over unbounded tensors. This is an important direction, but synthesis
still needs a place to obtain semantic structure, hardware capabilities,
fallbacks, and artifact constraints. Search can choose decisions; it does not
make the surrounding experimental control plane disappear. A useful workbench
should permit an Axon-like synthesizer, a hand-written schedule, or a simple
rule to be packaged as policy rather than making any one of them the compiler's
mandatory architecture.

### 2.3 Edge deployment makes the unsupported path visible

Edge runtimes are strong controls precisely because their supported paths are
fast and engineered. The difficulty appears when a study changes a data type,
operator, kernel ABI, or target primitive outside that path. Runtime-specific
registration can restore execution, but it couples the experiment to that
runtime. Transparent ahead-of-time C takes the opposite trade-off: it is easy
to inspect, compile, and connect to unfamiliar devices, but generic scalar code
can be far slower than a production kernel library. Our own current evidence
shows this gap rather than hiding it: a module-defined affine policy improves
the generated MobileNetV2 artifact, yet it remains 12.24 times slower than
one-thread ONNX Runtime on the same shared runner. Thus extensibility is only
useful if the representation exposes enough structure for layout, packing,
vectorization, external kernels, and target-owned profitability policies.

### 2.4 Hypothesis and measurable predictions

Joggle tests whether the vertical slice can remain one progressively exposed
typed function representation. Imported calls, reusable tensor bodies,
explicit loops, storage facts, and artifact calls are different states of the
same public objects. Distributable module functions decide which state to
expose or change; failed mutations roll back; target capabilities make the
remaining frontier explicit. This leads to four questions:

- **RQ1, continuity:** Can conventional CNN, detection, ViT/attention, and
  compact language-model workloads progress from import to inspectable
  computation and executable artifacts without operator cases in the core or
  emitter?
- **RQ2, extension boundary:** On matched tasks, how many distinct definitions,
  registrations, conversions, native files, and build dependencies are needed
  in Joggle, ONNX-MLIR, TVM, and selected edge/kernel systems?
- **RQ3, composition:** Do independently installed modules compose with stable
  output, transactional failure, and useful unsupported-frontier diagnostics?
- **RQ4, artifact quality:** Under matched model, host, threading, and numerical
  contracts, what are the compile cost, code size, workspace, accuracy, and
  latency boundaries of the generated artifacts?

These questions make the hypothesis falsifiable. Joggle fails if its apparent
uniformity merely moves operator or target cases into the host, if modules
cannot rewrite real function bodies, if modern model families stop at opaque
calls, or if the exposed structure cannot support competitive target-aware
code generation.

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

The structural compatibility record is generated directly from pinned Zoo
tests rather than transcribed from test output. Ten models complete semantic
conversion and canonical round trip. TinyYOLOv3 retains 219 unknown results
after inference; SSD-MobileNetV1 infers all result types but retains 386 source
calls after conversion. The source-call reduction comes from shared
broadcast-capable comparison functions, a maximum/minimum Clip composition,
and a generic tensor-tiling function reached through source-semantic mappings.
None adds an ONNX operator to the target emitter. Models absent from the configured cache are
omitted, not counted as passes. Structural completion is kept separate from
the ten-model numerical execution claim above.

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

This gap is not mysterious and should not be normalized as the cost of
malleability. The present backend emits portable scalar loops. It has no mature
weight packing, cache blocking, SIMD code selection, target-tuned kernel
library, or empirical schedule search. In contrast, the production runtime is
itself an optimized execution system, not a neutral C interpreter. We have not
yet completed matched throughput measurements against TVM or ONNX-MLIR, so the
paper makes no claim about their ordering. The conservative expectation is that
a tuned TVM path, and any ONNX-MLIR path that obtains stronger loop/vector or
library code, will also beat the current artifact. The planned operator study
will locate the loss across 25 two-dimensional, 27 contraction, and separate
convolution shapes before choosing general tiling, packing, vectorization, and
target-feedback mechanisms.

A clean same-job Linux diagnostic also separates fusion legality from
profitability. Starting from byte-identical MobileNetV2 canonical IR, greedy
`tile.fuse` reduces represented loops from 156 to 110 and external-weight C
from 192,209 to 188,244 bytes, but raises the generated-C median from 356.360
to 384.927 ms over 20 fresh-process observations. Adjacent one-thread ONNX
Runtime medians are 10.359 and 10.447 ms. Both generated artifacts pass the
same 1,000-element oracle and use byte-identical weights. This shared-runner
result does not establish a controlled latency effect, but it rejects loop
count and source size as sufficient profitability criteria and keeps selection
in a target-owned module policy rather than the legality rewrite.

A separate second-frontend study starts from the official floating-point
TFLite MobileNetV2 FlatBuffer rather than converting the ONNX subject. Joggle
decodes 107 constants and 66 source calls, relates and exposes every selected
call, emits 126,178 bytes of C plus a 13,956,388-byte weight payload, and plans
three workspace slots containing 2,860,032 `f32` elements. The strict C11
artifact agrees with the 1,001-element LiteRT 2.2.0 oracle within
`1.0132789611816406e-6`. On one pinned Intel Xeon Platinum 8370C shared runner,
20 balanced fresh-process trials have medians of 225.667 ms for generated C and
6.424 ms for one-thread LiteRT with its default XNNPACK CPU delegate, a 35.13x
gap. This is evidence that the TFLite path reaches a correct executable and a
second negative runtime comparison. It is not pooled with ONNX Runtime because
the serialized model and production stack differ, and it is not controlled-host
performance.

Table 1 reports two independently dispatched system runs. Each subject runs in
a fresh process under a one-thread contract, and each cell is the median of 20
trials. The generated model, input, weights, reference, and executable have
identical hashes across CPU classes, and every stored-output check passes.

<!-- BEGIN GENERATED: systems -->
**Table 1. Independent-system Linux diagnostics (median milliseconds).**

| CPU class | Model | Joggle C | ONNX Runtime | Joggle / ORT |
| --- | --- | ---: | ---: | ---: |
| AMD EPYC 7763 | MobileNetV2 | 167.315 | 10.346 | 16.17x |
| AMD EPYC 7763 | MNIST | 0.530 | 0.050 | 10.55x |
| Intel Xeon 8370C | MobileNetV2 | 173.787 | 7.345 | 23.66x |
| Intel Xeon 8370C | MNIST | 0.570 | 0.054 | 10.59x |
<!-- END GENERATED: systems -->

The cross-CPU results reproduce correctness and the negative performance
boundary, not one stable slowdown factor. Table 2 instead measures the generic
MobileNetV2 policy within each workflow execution on one pinned CPU. Reordering
54 affine-proved bodies and canonicalizing their index trees reduces generated
source from 192,209 to 152,275 bytes. The canonical/plain latency ratio changes
from 0.7348 to 0.7358 even though absolute latency changes substantially.

<!-- BEGIN GENERATED: policy -->
**Table 2. MobileNetV2 module-policy diagnostics (median milliseconds).**

| Run | Plain C | Canonical C | Canon / plain | Adjacent ORT | Canon / ORT |
| --- | ---: | ---: | ---: | ---: | ---: |
| A | 139.679 | 102.638 | 0.7348 | 8.382 | 12.24x |
| B | 178.796 | 131.557 | 0.7358 | 11.261 | 11.68x |
<!-- END GENERATED: policy -->

These remain shared-runner diagnostics rather than publication results. The
workflows do not control host load, temperature, or frequency, and the two
policy runs report the same processor class.

### 5.5 Extension surface

The extension study freezes four vertical tasks: add a reusable computation,
add a structural scheduling policy, route a family of calls to an external
kernel, and introduce a parameterized numeric format. Each task has the same
input program, observable result, and forbidden shortcuts. We record authored
files and lines, native registrations, build changes, dependencies, generated
artifacts, and the first unmet mandatory requirement separately. These
dimensions will appear as aligned panels rather than a pass/fail or aggregate
``ease'' score.

![Authored extension surface for four frozen tasks. Bars report measured source
files, nonblank non-comment lines, and source bytes; all axes start at zero. A
cross marks a system that stopped before a measurable implementation, while
hatching marks measured partial source that still stopped at a mandatory
endpoint.](figures/extension-surface.pdf)

All four Joggle tasks reach their required artifacts. The matching TVM controls
reach the computation, policy, and external-kernel artifacts but stop at the
custom-type registration required by the numeric-format task. ONNX-MLIR reaches
the computation and accelerator-scoped policy artifacts; its documented generic
call path does not capture the first required MatMul case, and its
numeric-format extension has no second executable target required by the task.
These are preserved unsupported boundaries, not failures silently replaced by
weaker tasks. Every successful executable reproduces its shared oracle.

The current measurements describe implementation surface, not developer time
or usability. They also reveal why a single line count is misleading: source,
registration, build, and artifact obligations have different meanings, and an
unsupported endpoint cannot be plotted as zero work. The final infrastructure
figure therefore uses one panel per dimension, one shared system legend, and a
distinct unsupported marker. A general claim of lower extension coupling
remains blocked until a clean independent reproduction confirms every retained
row.

## 6. Related work

**Inference compiler and deployment stacks.** TVM connects graph optimization,
tensor programs, schedules, cost models, and target code generation
[Chen et al. 2018](https://www.usenix.org/conference/osdi18/presentation/chen).
MLIR instead makes a family of domain-specific representations, interfaces,
conversions, and pass machinery reusable
[Lattner et al. 2021](https://doi.org/10.1109/CGO51591.2021.9370308);
ONNX-MLIR applies that organization to ONNX semantics and loop-oriented
lowering [Jin et al. 2020](https://arxiv.org/abs/2008.08272), while TinyIREE
extends an MLIR-based path to embedded deployment artifacts and runtimes
[Liu et al. 2022](https://doi.org/10.1109/MM.2022.3178068). These boundaries
are not accidental overhead: they support independent evolution, optimization
contracts, and broad target coverage. They become a research cost when one
experiment must restate the same semantic or hardware decision at several
boundaries. Joggle's question is therefore narrower than replacing a production
stack: can a bounded vertical experiment retain typed structure while using one
public edit surface from imported relation to artifact? ONNX-MLIR is the closest
system comparator and TVM the tensor-compiler control; neither source size nor
one successful extension establishes that Joggle is generally easier to extend.

**Author-controlled tensor programs.** Halide separates an algorithm from a
schedule [Ragan-Kelley et al. 2013](https://doi.org/10.1145/2491956.2462176);
Lift and RISE make typed data-parallel structure and rewrite strategies explicit
[Steuwer et al. 2017](https://doi.org/10.1109/CGO.2017.7863730);
[Steuwer et al. 2022](https://arxiv.org/abs/2201.03611). Exo goes further by
externalizing target instructions, memories, and scheduling policy into user
libraries while checking transformed programs
[Ikarashi et al. 2022](https://doi.org/10.1145/3519939.3523446). Hidet embeds
composable task mappings in tensor programs to express assignment and ordering
beyond loop-only schedules
[Ding et al. 2023](https://doi.org/10.1145/3575693.3575702), and TileLang
exposes tiled dataflow, layout, memory, and thread decisions on TVM IR
[Wang et al. 2025](https://arxiv.org/abs/2504.17577). These systems show that
control must be both expressive and economical; merely exposing loops is
insufficient. Joggle differs in scope: it does not prescribe a tile, worker, or
instruction model in the core, and asks user modules to add those policies to
ordinary functions. The cost is visible in the current C results: without a
mature contraction, packing, and vector policy, openness does not yield
competitive code.

**Automatic transformation and search.** Ansor learns a cost model while
exploring generated tensor programs
[Zheng et al. 2020](https://www.usenix.org/conference/osdi20/presentation/zheng);
ROLLER reduces tuning cost by constructing programs from hardware-aligned tiles
[Zhu et al. 2022](https://www.usenix.org/conference/osdi22/presentation/zhu).
Welder makes memory traffic across operators explicit through a tile graph
[Shi et al. 2023](https://www.usenix.org/conference/osdi23/presentation/shi),
whereas Ladder jointly exposes custom numeric types, data transformations, and
hardware-aware schedules
[Wang et al. 2024](https://www.usenix.org/conference/osdi24/presentation/wang-lei).
Graph and tensor superoptimizers enlarge a different dimension: TASO generates
verified graph substitutions
[Jia et al. 2019](https://doi.org/10.1145/3341301.3359630), Tensat uses equality
saturation to avoid committing to a rewrite order
[Yang et al. 2021](https://arxiv.org/abs/2101.01332), Glenside makes layout-aware
access patterns rewritable
[Smith et al. 2021](https://doi.org/10.1145/3460945.3464953), and Mirage and
Axon search across several levels
[Wu et al. 2024](https://arxiv.org/abs/2405.05751);
[Kothari et al. 2026](https://arxiv.org/abs/2606.26344). Axon additionally
synthesizes target instructions from semantic specifications and reports target
execution as most of its search cost. These works do not make an experimental
control plane unnecessary: every search still requires semantics, legal
candidates, target feedback, fallbacks, and an artifact boundary. Joggle's
intended role is to host such a chooser as an optional module, not to claim a
new search algorithm. The submission must therefore measure candidate
generation, rejection, execution, and caching rather than only the winner.

**Whole-model and edge co-design.** Rammer co-schedules inter- and
intra-operator work using hardware-neutral task abstractions
[Ma et al. 2020](https://www.usenix.org/conference/osdi20/presentation/ma),
demonstrating that operator-local scheduling can leave important parallelism
hidden. At the extreme edge, TensorFlow Lite Micro uses a compact interpreter
and explicitly selected kernels
[David et al. 2021](https://proceedings.mlsys.org/paper_files/paper/2021/file/6c44dc73014d66ba49b28d483a8f8b0d-Paper.pdf);
DORY couples topology-aware tiling, explicit transfers, and generated C for
scratchpad-based MCUs
[Burrello et al. 2021](https://doi.org/10.1109/TC.2021.3066883); MCUNet
co-designs the network search space with whole-network memory scheduling
[Lin et al. 2020](https://proceedings.neurips.cc/paper_files/paper/2020/hash/86c51678350f656dcc7f490a43946ee5-Abstract.html).
ncnn provides a lightweight optimized runtime and a custom-layer path, making
it a useful deployment control rather than an IR lineage. These systems clarify
Joggle's evaluation boundary. Transparent AOT C can simplify inspection and
unusual target integration, but must be compared against mature runtime paths
on the same serialized computation, input, threading policy, and host.
Workload coverage alone is not evidence of useful edge compilation; latency,
peak workspace, artifact footprint, correctness, and unsupported cases must be
reported together.

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
