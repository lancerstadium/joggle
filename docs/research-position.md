# Research position

This is an evidence ledger, not a paper draft. It records only mechanisms and
measurements present in the current tree; discarded prototypes are not treated
as research evidence.

## Question

Can AI software/hardware co-design extensions compose through one typed,
versioned Module/function plane, while allowing kernel authors and target
authors to evolve independently and still produce an executable, measurable
edge deployment?

The intended contribution is not another fixed graph-to-loop pipeline. It is a
small extension mechanism in which:

- representation, source semantics, compiler transformations, analysis, and
  emission are installable Module contents;
- compiler-time and residual computation share one typed `fn` declaration;
- a target states an accepted Function boundary rather than registering a
  lowering for every source kernel;
- end-user pipelines remain ordinary source and expose controllable stages.

## Evidence retained in the current tree

The core currently demonstrates:

1. one `Module` owns declarations, materialized CFG/SSA Functions, imports,
   and content-addressed data;
2. Known and Residual values use the same function ports and source evaluator;
3. whole-Module compiler functions compose through normal typed calls;
4. transformations are transactional and caller-defined legality is explicit;
5. `Compiler::specialize` recursively expands source Functions to a typed
   caller-provided boundary, shares repeated specializations, preserves its
   input, and rejects opaque unsupported calls;
6. Module installation, locking, and native binary identity are deterministic.

These are infrastructure facts. They do not establish AI compiler usefulness,
target performance, or publication-level novelty.

## Nearest systems

- MLIR and xDSL provide extensible multi-level IR, declarative operation
  schemas, conversions, and broad compiler tooling.
- TVM Relax/TensorIR and TileLang provide mature model and kernel programming,
  scheduling, and code generation.
- IREE and ONNX-MLIR provide production-oriented model import, lowering,
  runtimes, and deployment.
- MATCH provides a particularly relevant retargetable edge-DNN design with
  hardware execution modules, memory models, cost models, and measured MCU
  deployment.

Joggle cannot claim superiority from a smaller API. The discriminating claim
must be measured: adding a data format, source kernel, target primitive,
optimization, and analysis should require fewer cross-framework seams while
retaining correctness and useful generated code.

## Candidate mechanism

Boundary-relative source specialization is the current candidate contribution.
A kernel author supplies a normal source body. A target emitter supplies a
predicate over Function capabilities. The compiler recursively specializes the
former until it reaches the latter.

This differs from a name-based lowering table only if the executable emitter
consumes the resulting primitive calls directly. Reconstructing high-level
operators in a hidden registry would falsify the mechanism.

## Required vertical slice

Publication work does not resume until all of the following exist in the new
module design:

1. import of an externally maintained ONNX model;
2. a typed semantic representation sufficient for that model but independent
   of the target;
3. at least one source-defined user kernel added without a target name-table
   change;
4. one executable edge target output, not a serialized IR container;
5. differential numerical validation against a trusted runtime;
6. measured latency, memory, binary size, compile time, and target resource
   use;
7. one independently installable numeric-format or layout extension;
8. a controlled extension task repeated in an appropriate baseline.

## Evaluation and kill criteria

The study will record core files changed, extension-local files changed,
registration/schema code, build dependencies, compile time, deterministic
rebuilds, and failures caught before emission. Runtime experiments will use the
same models, precision, kernels, and hardware whenever a performance comparison
is reported.

The thesis must be narrowed or abandoned if realistic extensions repeatedly
require new core declaration kinds, if uniform `fn` merely hides complexity in
native code, if source specialization prevents competitive optimization, or if
the controlled study shows no meaningful reduction in integration coupling.

## Venue gate

ASPLOS requires an implemented systems mechanism with architecture/runtime or
compiler/hardware consequences. TACO requires substantial architecture and code
optimization evidence. A language design plus synthetic target is insufficient
for either. Venue selection remains conditional on the new executable vertical
slice and baseline study.
