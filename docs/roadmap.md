# Roadmap

Joggle is intended to become a small compiler workbench that AI software and
hardware co-design researchers can extend without forking its core. Completion
means that a new frontend, data format, optimization, execution model, or
emitter can be supplied as a module and composed with existing work through the
same function IR. A collection of demos is not completion.

## Design sources

The project uses a deliberately narrow subset of ideas from earlier systems:

- Lift: transformations and algorithm structure compose as functions; Joggle
  should not require a second rewrite language or an operator class hierarchy.
- TVM and TileLang: users may start from tensor calls and progressively expose
  loops, storage, and target details. Joggle keeps those stages in `Fn` rather
  than imposing a built-in graph/tile pipeline.
- MLIR, IREE, and ONNX-MLIR: symbols, types, conversion legality, and explicit
  interfaces are essential. Joggle adopts those checks without dialect classes,
  generated adaptors, or an MLIR dependency.
- MIR: a small embeddable implementation, stable handles, explicit ownership,
  and a plain C boundary are more valuable here than a large framework API.
- XOC: data-driven definitions, shared queries, and composable pipelines are
  useful. Its global record registry, stringly `kind` protocols, backend-owned
  reinterpretation, and many behavior fields must not reappear in Joggle.

These are design inputs, not compatibility promises. New abstractions enter the
core only when two unrelated modules need the same structural capability.

## Completion criteria

The project is ready for sustained research use when all of the following hold:

1. Types and symbols are structural, scoped, diagnosable, and extensible by
   modules. Generic calls infer and check their result types without operator
   switches.
2. C++ and `.jog` functions can construct, inspect, clone, move, replace, and
   erase every public IR form while preserving dominance, use-def links, source
   locations, and transactional failure.
3. Ordinary `fn(Mod) -> bool` functions compose into named pipelines and
   fixed-point runs. Analyses use explicit values and revision-aware caching;
   there is no pass or analysis class hierarchy.
4. Frontends transport source semantics without defining optimization policy.
   Semantic conversion lives in explicit bridge modules and is never triggered
   by parsing or loading.
5. Target modules can define data formats, legal computations, cost queries,
   reference execution, storage choices, and emitted artifacts without adding
   target cases to the core.
6. Module discovery, installation layout, dependency diagnostics, ABI checks,
   and source compatibility are documented and tested on a clean consumer
   project.
7. Official conventional neural-network models exercise importing, conversion,
   fusion, tensor/loop transforms, storage planning, and at least two genuinely
   different target modules. All results are deterministic and reproducible.

## Milestones

### M6 — types and symbols

Status: complete.

- Replace opaque type strings internally with immutable structural type nodes.
- Resolve local, imported, qualified, overloaded, and generic function calls.
- Infer generic bindings from arguments and explicit parameters; propagate
  result and loop-element types; diagnose ambiguity and arity/type failures.
- Keep unknown external computation printable, but distinguish it explicitly
  from a resolved declaration.
- Let module-defined type constructors use the same lookup and compile-time
  value machinery as built-in types.
- Keep structural type construction symmetric: `.jog` uses `ty(name, args)`
  and C++ uses `Ty(name, args)` without rebuilding type text.

Exit gate: a module-defined parametric number format and nested tensor/list
signatures resolve without parser cases, while intentionally ambiguous and
incompatible programs fail with stable diagnostics.

### M7 — complete IR editing

Status: complete.

- Support multi-result calls and complete constant, call, `Blk`, loop, branch,
  return, and yield construction.
- Rename and erase function overloads while preserving resolved calls, overload
  uniqueness, nested ownership, and handle invalidation.
- Retype values and function result contracts, and validate every return in
  nested control flow before a transform commits.
- Reflect and edit explicit call generic terms structurally, reusing ordinary
  overload checks instead of requiring modules to parse callee spelling.
- Add nested walking, selective use replacement, cloning, moving, and `Blk`
  argument editing with explicit insertion points.
- Expose one deterministic runtime-value walk for functions and modules so
  analyses do not independently reconstruct parameters, block arguments, and
  operation results; keep compile-time generics separate.
- Give every successful mutation a module revision and make compound edits
  atomic.
- Preserve readable bindings during printing without exposing generated SSA
  names.

Exit gate: one textual module and one C++ module independently build and rewrite
the same nested-loop kernel, including a multi-result transformation and a
forced rollback.

### M8 — composition and analyses

Status: complete. Source wrappers, embedding sequences, and CLI sequences all
invoke the same ordinary functions. Each sequence is one transaction and uses
one rollback snapshot rather than one copy per step. It emits the same
structural per-step report; bounded fixed points and revision-keyed read-only
queries require no pass or analysis hierarchy. Optional C++ overloads return
steady-clock durations separately, leaving canonical reports deterministic.
Single embedding runs also accept typed `Attr` arguments after `Mod`, resolve
the matching ordinary overload, record the arguments in the structural report,
and retain whole-transform rollback. This makes parameterized policy functions
directly usable without generated option objects or wrapper passes. The same
canonical literals are available from `run`, `query`, and `emit` through
repeatable CLI `--arg` options; source parsing and configuration do not diverge.

- Compose ordinary transform functions into pipelines without a pass class or
  new surface keyword.
- Provide bounded fixed-point execution, per-step diagnostics, change counts,
  timing hooks, and deterministic pipeline reports.
- Cache pure analysis function results by module revision and explicit inputs;
  mutation invalidates them automatically.
- Implement reusable constant folding, dead-call elimination, common
  subexpression elimination, canonicalization, and region fusion as modules.

The bundled optimization fixed point now derives progress from module
revisions, rejects non-positive bounds, and rolls back when the last permitted
round still changes IR. Its zero-policy entry derives a sufficient bound from
the graph size; a deep reverse dead-use chain guards against silent partial
cleanup. The same module now exposes partial evaluation over an explicit set of
ordinary functions and source-preserving root-block copy propagation. Nested
copies and root initializers carried into structured control retain the
assignments required by the imperative surface form. Batch
value replacement and
operation erasure collapse chains, check dominance once, and rebuild use lists
once, making application-sized rewrites independent of the number of selected
calls.

Exit gate: pipelines can be assembled in source and through the embedding API,
produce identical results, and expose which step changed the module.

### M9 — distributable modules

Status: complete. Deterministic local discovery, inspection, dependency and
native-binding validation, staged installation, collision rejection,
source-compatible staged upgrade, and validated removal are implemented without
a registry or second manifest. Upgrade alpha-normalizes generic names, accepts
additive overloads, rejects removed or changed declarations, validates the new
dependency/native closure, and restores the installed directory on a failed
commit. The installed package is exercised by a clean external CMake consumer.
An out-of-tree native module is also built against that package, installed
through the installed CLI, loaded, called, upgraded, and removed.
Runtime loading is likewise transactional across its complete dependency
closure, including module maps, native bindings, dynamic-library handles,
loading state, and the environment epoch.
Module-local helpers now use `local fn`; cross-module lookup, direct invocation,
inspection, and compatibility checks expose only the callable surface while
explicit reflection can still discover internal rule functions.

- Keep source, native library, tests, documentation, dependencies, and the
  public compatibility surface in one module directory.
- Add deterministic discovery, inspection, validation, installation, and
  upgrade/removal commands; loading remains explicit and side-effect free.
- Keep the exported native entry stable and evolve the size-tagged ABI by
  append-only fields until a deliberate major break.
- Test installed use from an external CMake consumer on supported platforms.

Exit gate: an out-of-tree module can be packaged, installed, discovered, run,
upgraded compatibly, and removed without editing or rebuilding Joggle.

### M10 — neural-network workflow

Status: in progress. Binary ONNX and TFLite transport, real-model generic
fusion, same-signature semantic bridging, progressive body expansion, and
structural tensor type construction are complete. Named ONNX dimensions now
reuse integer function generics and anonymous dynamic dimensions remain open
terms; a dependency-local codec test covers both without a downloaded model.
ONNX graph attributes now become ordinary functions with explicit lexical
captures. The official Tiny-YOLOv3-11 partial semantic gate covers 269 tensor
constants, 291 calls, and four `Loop` bodies without adding a second region IR.
Exact local-function and result-signature reflection lets the semantic module
derive both loop-carried and scan outputs from those bodies. This closes all
eight Loop results and four dependent Reshapes, moving the model's pinned type
frontier from 280 open results to 219; the remaining frontier is retained as an
explicit complex-network gate rather than mislabeled as full support. The
semantic matrix remains separate from that protocol claim.
The 1.2 MB official UltraFace RFB-320 model adds a different edge-oriented,
shape-heavy detector. Its 244 tensor constants and 242 calls move from 240 open
results to zero, then convert, verify, and round-trip. This is implemented by
one tensor-literal decoder shared by initializer and Constant encodings and one
Slice relation shared by attribute- and input-based ONNX schemas. Tiny-YOLOv3
remains the explicit partial frontier; UltraFace is a complete semantic gate
for the relations it contains.
The official SSD-MobileNetV1-12 model extends the matrix beyond compact graphs:
1,567 constants, 5,985 nodes, eight nested graphs, Resize, and
NonMaxSuppression survive import, verification, canonical round trip, and a
pinned type-closure gate. Graph captures and Loop protocol operands now refine
ordinary child-function parameters. Partial symbolic Conv, Split/Squeeze,
Resize, and post-processing relations close all 6,790 open result types at a
revision-checked fixed point without claiming full execution support.
ShuffleNet V2 closes and converts a channel split/shuffle network. DenseNet-121
erases all 910 intermediate annotations and recovers them solely from
signature, constants, and schema relations. Erased-signature GoogLeNet and
EfficientNet-QDQ likewise close to zero; operator-oriented EfficientNet INT8
pins its still-unsupported QLinear boundary instead of guessing it. An optional
heavy BiDAF round-trip gate isolates large-attribute parser scalability from
the normal semantic matrix.
The frontend-neutral `opt.untyped` query exposes the remaining type frontier,
and the CLI can invoke parameterized analyses through the same cached,
read-only `query` boundary used by embedding code. A data-driven unary relation
now covers Exp, Sigmoid, Ceil, and round-to-even in addition to the existing
normalization primitives, with ordinary inspectable function bodies.
Open attributes now cover `Val` as well as `Fn` and `Op`, separating tensor
quantization, layout, and provenance from computation options. ONNX preserves
source value identity, and a dependency-local quantized TFLite Add gate proves
value metadata survives import and round-trip. A frontend-neutral `quant`
module now exposes scale, zero point, parameter axis, round-to-nearest-even,
saturation bounds, and element conversion as ordinary computation; it does not
turn raw integer arithmetic into implicit quantized semantics.
Grouped 2-D convolution now has an
inspectable body, and the explicit `onnx.nn` relation propagates all official
MobileNetV2 intermediate types before converting every compute node through
shared Conv, BatchNormalization, ReLU, Add, global-pool, and reshape semantics.
Average and maximum 2-D pooling now share dilation- and layout-explicit
semantics across ONNX and TFLite.
The complete converted ONNX model also expands one semantic body per compute
node and round-trips as ordinary nested IR. The second real frontend imports
the official TensorFlow Hub MobileNetV2 with reflection-driven options and no
core or reader operator switches. Its independent `.jog` bridge converts all
66 compute calls through explicit logical-axis operands and exposes every
shared body. Shared tensor functions now define trailing-axis broadcasting;
both frontend bridges validate against that relation and lower Add to the same
inspectable `nn.add` body; Sub and Mul now use the same path. ONNX Flatten and
rank-two-or-higher MatMul reuse `tensor.reshape` and `tensor.matmul`, while
Transpose reuses rank-generic `tensor.permute`, instead of adding frontend-
shaped semantics. `opt.unresolved`
reports six open source call
families before that bridge and none afterward. The two target gates remain.
Named extents now flow through broadcast binary operations, Flatten,
broadcast-batched MatMul, and Transpose using structural type terms.
Generic-list expansion
reuses caller dimension bindings, so those converted calls can expose their
normal tensor bodies without requiring a concrete batch size. Shape relations
cancel exact factors and retain anonymous dimensions when a runtime product is
not representable as one existing term; rank information no longer disappears
with that product.
QuantizeLinear and DequantizeLinear now propagate shape and element type, then
compatible three-input forms convert to generic `quant` bodies. All 258
quantizers and 652 of 653 dequantizers across three local QDQ vision models use
that path; one inconsistent imported result remains explicit. Conv
and pooling require concrete spatial arithmetic but preserve a symbolic batch
dimension; optional Conv bias is validated and reuses the existing composed
`nn.conv2d` body. This makes the compute path of a conventional QDQ ResNet
representable while keeping quantization policy explicit and inspectable.
Leading MatMul dimensions now use the shared broadcast relation and one
rank-generic tensor body, while the 2-D overload remains the compact case.
An axis-generic line-offset relation supports shared Softmax semantics; TFLite
materializes its last axis and ONNX converts an explicit normalized axis.
An axis-list overload now also expresses joint reduction across a suffix.
`onnx.opset` reads the model descriptor, allowing pre-13 ONNX Softmax to use
the flattened suffix and version 13 or later to use one axis. Missing versions
remain open rather than inheriting an accidental default.
A multi-axis coordinate relation now supports an inspectable tensor mean body.
The ONNX relation normalizes negative axes, models `keepdims` structurally, and
retains duplicate or out-of-range axes. A symbolic multi-axis reduction expands
through the same generic body. The imported quantized BERT graph now exercises
all 50 `ReduceMean` nodes after shape and integer-quantized type propagation.
Their computation is converted to the shared multi-axis mean body while the
quantized operators retain their own explicit `quant` semantics.
The shared library also covers broadcast division and power plus elementwise
square root, reciprocal, and hyperbolic tangent. A symbolic decomposed
normalization/GELU chain passes inference, conversion, body expansion, and
verification without introducing fused operator classes. Those paths now reach
every transformer layer in the imported BERT model.
The first shape-program mechanism is now present without a second IR. Textual
modules can traverse `Val -> Op`, inspect bounded byte constants, and derive
symbolic Reshape types through Shape/Gather/Slice/Unsqueeze/Squeeze/Concat/Cast
chains. Compatible tensor holes are refined without overwriting imported type
contracts; ConstantOfShape, OneHot, DynamicQuantizeLinear, MatMulInteger,
Split, and transposed batched MatMul complete structural propagation through a
12-layer quantized BERT graph. All 70 Reshape calls and 774 other covered
floating tensor calls convert through shared semantics. All 72 dynamic
quantizers, 84 integer matrix multiplications, 150 casts, and 12 Microsoft
scaled/transposed MatMul calls also convert to generic `quant` and `tensor`
bodies; the vendor namespace is no longer a compute boundary.
Shape computation is no longer only inferred. Shared bodies now cover Shape,
Gather, positive-step Slice, OneHot, fill, and binary Concat. The ONNX relation
folds each N-input Concat into that binary algebra and expresses each Split
result as a slice, preserving result names and arbitrary non-frontend tags.
On the imported BERT graph this converts all 55 Concats, all 5 Shapes, the
Gather, all 5 source Slices, the 2-result Split, OneHot, and
ConstantOfShape. Only two Squeezes and one Identity with conflicting imported
symbolic result contracts remain explicit; a repeated conversion is
byte-identical.
Consumer capability declarations can now carry ordinary function bodies.
`ir.match` reuses overload specificity across an explicit implementation set,
and environment-aware expansion preserves the implementation module's helper
visibility transactionally. `opt.apply` therefore selects shape-, format-, or
width-specific implementations to a bounded fixed point without a target
registry, kernel syntax, or one-operator C++ binding. Bound exhaustion uses the
general compile-time `assert` boundary and restores the exact input module.
Successful expansions are recorded in the existing structural `run` report
with their source symbol, selected implementation signature, and revision
delta, keeping observation on the real execution path. The same report can be
written by the CLI through `--report` using the public canonical `Attr` printer.

Open function metadata now also supports semantic relation discovery.
`ir.where` selects `Fn` values without reserving a relation vocabulary, and
`ir.invoke<R>` transactionally executes a typed `fn(Mod, Op) -> R` boundary.
ONNX inference and conversion relations self-identify in their owning module,
removing both central operator-name dispatch chains while leaving import and
conversion explicit. Those relations are module-local implementation while
the owning module can still discover them through explicit reflection; module
inspection and cross-module resolution expose only the small bridge API.
Module-owned phases preserve conversion order, and an
explicit relation-list entry performs its own phase selection, allowing an
external module to append type and conversion relations without copying the
driver. The separate TFLite bridge uses the same boundary while retaining a
different quantization policy. A malformed relation signature is rejected
without changing model text or revision. Host transform entry selection uses
normal `Mod` overload resolution, so the default and explicit-relation forms
share the same symbol.

- Keep binary codecs such as ONNX and TFLite separate from semantic bridge
  modules.
- Define reusable tensor and scalar computation libraries with function bodies
  where semantics can be expressed in Joggle and native references otherwise.
- Implement declarative source-to-library conversion, shape/type propagation,
  fusion, layout and storage transforms, and loop exposure.
- Allow target modules to select supported calls, attach costs, simulate exact
  behavior, and emit their chosen representation.

The repository now includes an executable out-of-tree implementation module:
one generic `.jog` function replaces rank-two `tensor.matmul` with an `i-k-j`
loop order, then composes with ordinary C preparation and read-only emission.
Its numerical gate proves that kernel customization needs neither a target
class nor a second kernel IR. This is an extension-mechanism case, not a claim
that one loop order is generally faster. The optional official ONNX MatMul gate
applies the same module after semantic conversion and checks its result through
both VM and compiled C against the pinned ONNX output.

The complementary `examples/edge` gate treats a bodyless monomorphic tensor
function as an external C ABI contract, automatically emits its qualified
prototype, links a separately compiled implementation, and executes the
result. It requires no core or emitter case for that kernel. Generic and
dynamic external ABIs remain deliberately open rather than receiving an
unstated specialization policy.

The CLI now provides one target-neutral artifact boundary: `emit` executes an
ordinary read-only `fn(Mod) -> str/bytes` and writes exactly those bytes. A
pure `.jog` C99 module emits and executes fixed-shape tensor loops, local scalar
calls, and structured branches while rejecting unexposed dependency calls. Its
separate `prepare` function expands a high-level tensor addition through the
shared tensor body to a fixed point; direct emission remains read-only.
A separate target-neutral `mem.plan` function now computes static tensor live
intervals and reusable same-element-type slots as open metadata. The C module
optionally consumes the plan; a three-stage tensor chain compiles and executes
with two physical buffers for three logical intermediates. Dynamic allocation,
inter-function planning, and production-grade target runtimes remain open.
The official MNIST application is the normal whole-network numerical gate. It
passes one protobuf input through import, semantic conversion, VM execution,
memory planning, generated C and generated header compilation, then compares
all ten outputs with the official protobuf result. This medium case is fast
enough for routine validation and caught an emitter bug where a folded literal
assignment inside max pooling was lost. The fix exposes the operation's binding
form through generic `ir.form`; C no longer infers mutation from equal names.
After full semantic-body exposure, the official MobileNetV2 model completes C
preparation, static memory planning, C99 emission, strict compilation, and an
all-output comparison against the official ONNX Zoo protobuf result. The gate
also caught and now guards a source-preservation error in nested assignment
copy propagation. The emitted
function uses 268 typed slots instead of 422 separate tensor arrays, reducing
declared mutable tensor storage from 119,677,248 to 27,198,592 bytes. The
ordinary `c.place` transform can place those slots in static C storage without
giving placement metadata a core meaning.

Construction-time carried-state pruning preserves that output gate while
removing bindings that every nested body merely forwards. On the same expanded
MobileNetV2 artifact, live IR values fall from 1,445,296 to 31,414 and a local
Release `stat.summary` process falls from approximately 2.07 GB to 394 MB peak
resident memory. These are application-gate observations rather than a general
parser benchmark; operation and block counts are unchanged.
The optional `sat` module now owns one parameterized materialization function:
caller-supplied width limits and storage types recursively retype the format,
specialize local helpers, and preserve constructor generics. Thin `sat.c` and
`sat.vm` bridges select different storage maps without adding a format case to
core, C, or VM. C compiles and executes scalar and fixed-shape tensor values;
VM independently executes the same `tensor<sat<5>, [4]>` semantics. Format
materialization now retypes every selected value with one atomic batch edit
instead of rediscovering structured carried-value families for each value. The
shared editor rejects conflicting family requests before mutation. The `vm`
module establishes the independent second-target
boundary: pure `.jog` reflection emits a deterministic image and a native
module executes typed `i64`, `f32`, and `f64` arithmetic, structured branches
and range loops, and static tensors with an exact instruction-step count. The
same integer and floating-point nested-loop matrix multiplications run through
C and VM with their native element widths. The explicit `vm.prepare` function
now supplies one ordinary structural capability predicate to generic
`opt.expose`; the same mechanism serves C with a different predicate. It folds,
removes copies, and exposes the existing shared tensor-add body without a
target class, operator registry, or hidden emitter lowering. ONNX initializers
and Constant nodes plus TFLite buffers now
retarget to one result-typed `tensor.literal(bytes)` data primitive. C and VM
both execute its size-checked raw payload, so target modules no longer need
frontend-specific weight operations. The conversion is exercised by thirteen
official ONNX Zoo model gates and the official TFLite MobileNet gate. Core
gained no frontend, target, or tensor-literal case. The hash-pinned ONNX v1.19.0
backend MatMul case now completes binary import, conversion, dead-data cleanup,
ordinary body expansion, and output comparison through both VM and compiled C.
The same exposed MobileNetV2 now passes VM-owned preparation, emits a complete
VM image, and completes the official input/output comparison through both VM
and compiled C. The VM records exactly 95,592,386,975 steps; the magnitude is
evidence against treating full scalar exposure as the optimized execution
form. The module-defined non-native format gate now also crosses that
independent target without teaching it the format.

Exit gate: official models from two frontends pass through one shared semantic
library and run through at least two targets without core operator switches.

### M11 — research instrumentation

Status: in progress. `stat.summary` provides a deterministic, read-only
structural snapshot through an ordinary module query. `stat.sum` can aggregate
an external `fn(Mod, Op) -> int` measure through the typed invocation boundary,
and its configured form forwards one typed structural policy value to
`fn(Mod, Op, A) -> int`. The same callback can therefore represent a family of
device models without generated wrappers or a core interface. Generic
capability traversal in `opt` uses the same boundary. The module still makes no
timing or device-model claims of its own. The new `bounds`
module proves revision-scoped integer intervals for constants, range
iterators, safe scalar arithmetic, branches, and unchanged loop-carried values.
Overflow and unsupported recurrences remain unknown. This supplies evidence
for custom-width and address-generation experiments without allowing an
emitter to silently change the program's integer semantics.

- Record pipeline decisions, costs, code size, memory use, and deterministic
  cycle estimates through module functions and stable structured output.
- Report extension cost at the actual boundary: module source and native code,
  core files changed, dependencies introduced, exposed functions, and tests.
  Reconstruct the same measures for representative frontend, semantic,
  optimization, format, and target additions rather than treating aggregate
  repository size as an extensibility result.
- Separate three empirical questions: which conventional models the shared
  semantics can represent, whether independent modules compose without core
  cases, and what resources complete pipelines consume. A partial import,
  successful type closure, body exposure, compilation, and numerical execution
  are distinct outcomes in every table.
- Make cloud/edge partitioning, JIT specialization, custom formats, LUT/logic
  implementations, and WCET-oriented analysis independent research modules.
- Provide reproducible experiment manifests and artifact hashes without making
  any one research policy part of the compiler core.

Exit gate: a clean checkout reproduces at least one end-to-end co-design study,
including baselines, ablations, failure cases, and generated artifacts.

## Compatibility policy

- `module.jog` is the source of declarations; native code cannot replace its
  signatures.
- The C ABI carries version and byte size in data, never in exported names.
- Pre-1.0 source changes prefer one documented migration over permanent aliases.
- Canonical text is deterministic within a release. Round-trip and installed
  consumer tests guard changes to syntax, public headers, and module layout.
- Core remains C++20 plus the standard library. Optional dependencies belong to
  the modules that need them.

Each milestone lands as reviewable commits only after strict compiler warnings,
default offline tests, optional-module tests, sanitizers, failure rollback, and
the relevant real-model gate pass. A milestone is reopened when its abstraction
requires operator-, frontend-, or target-specific logic in the core.
