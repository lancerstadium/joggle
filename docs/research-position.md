# Research position and evidence plan

This document is a working research memo, not a paper draft. It separates the
mechanisms implemented in Joggle from hypotheses that still require experiments.
The review was last refreshed on 2026-09-04 against primary papers, project
documentation, and the current repository.

## Research question

AI hardware/software co-design rarely adds only an operator or only a backend.
One experiment may introduce a numeric format, executable operator bodies,
whole-model transformations, a target description, a resource model, a
simulator, and an emitter together. Established compiler infrastructures make
these possible, but expose several extension roles and abstraction levels.

Joggle asks a narrower question:

> Can one installable, typed Module and one `fn` abstraction carry the data and
> control of an AI co-design experiment—from imported model to residual
> operators, transformations, target artifacts, simulation, and emission—while
> retaining static checking, deterministic artifacts, and low core coupling?

The proposed contribution is not another graph IR, tile language, device model,
or code generator. Those are experiment-defined Module contents. The research
subject is the extension and composition mechanism that lets independently
defined contents meet without adding a new compiler-core category for each
role.

## Implemented evidence

The following statements are repository facts. They are the only claims the
current implementation can support.

| Mechanism | Current evidence | Boundary |
| --- | --- | --- |
| One artifact owner | `joggle::Module` owns declarations, materialized CFG/def-use bodies, imports, and content-addressed data. See [Architecture](architecture.md). | This is an implementation model, not evidence that one owner is universally preferable. |
| One callable form | Residual operators and compiler-side loading, conversion, optimization, analysis, simulation, and emission are typed `fn` calls. See [Compiler functions](compiler-functions.md). | Native behavior still exists for effects and algorithms that the source language cannot express. |
| Staged specialization | Known compiler values and Residual IR values use the same declared ports; typed Residual calls can materialize generic source bodies. | The language is deliberately incomplete and has no closure literals. |
| Installable semantics | ONNX import, f32-to-f16 conversion, NN vocabularies, and Anchor target semantics live in optional Modules rather than core target classes. | Only a small set of Modules and one target experiment exist. |
| End-to-end composition | The Anchor integration test imports the official opset-18 ResNet-18 ONNX model, converts it, maps target calls, fuses eligible epilogues, plans scratch storage, simulates a typed timeline, semantically links source kernels, and emits the resulting bundle with all immutable payloads. `unpack` verifies content identities and reconstructs the same Module in a fresh compilation. See [the ONNX integration test](../modules/anchor/tests/onnx_integration_test.cpp). | The packed artifact is deterministic and self-contained with respect to model data, but it is not machine code. |
| Order independence case | The precision integration test applies f32-to-f16 before or after ONNX-to-NN conversion; both paths produce identical planned Modules, resources, and traces. See [the precision integration test](../modules/anchor/tests/precision_integration_test.cpp). | One commuting pair does not establish general pass-order independence. |
| Deterministic analytical result | For `anchor.config<16, 4, 32, 16, 16777216, 4>`, the checked f32 baseline has 140 planned Ops, 49 events, 10,946,464 scratch bytes, and 29,453,374 modeled cycles. Nine fusions produce 122 Ops, 40 events, 7,735,200 bytes, and 29,161,690 modeled cycles. The f16 composition has 3,867,600 scratch bytes and 28,848,157 modeled cycles. | These are outputs of the declared analytical model. They are not measured latency, energy, numerical accuracy, or WCET. |
| Source-kernel semantic linking | `anchor.bundle(module) -> module` recursively specializes concrete user function bodies, inserts each unique specialization locally, strips consumed Known arguments from its call edge, and admits leaves only through shared arithmetic, literal, allocation, memory, alias, and release interfaces. `anchor.emit` serializes this bundle. A separately installed four-element square kernel links without name registration; an otherwise identical opaque declaration fails. The full ResNet-18 bundle has one entry plus 35 specializations; the fused bundle has one entry plus 42. Their 49 and 40 roots respectively become local calls, while both retain all 42 immutable resources. See [the closure test](../modules/anchor/tests/kernel_test.cpp) and [the ONNX integration test](../modules/anchor/tests/onnx_integration_test.cpp). | Semantic linking proves that declared bodies reach an admitted primitive boundary and that emission carries them. The artifact is packed and reloadable, but its primitives still lack executable target code. |
| Numerical differential case | A Module-defined `execute_f32(module, bytes) -> bytes` function evaluates the fused and planned target graph. [The numerical test](../modules/anchor/tests/numerical_test.cpp) compares all 1,000 ResNet-18 logits with an ONNX Runtime oracle generated from the same pinned model: maximum scaled error `2.58669e-06`, exact top-1 agreement, with a `1e-4` acceptance bound. | This proves one f32 model path on a host semantic executor. It does not prove f16 accuracy, broad operator coverage, physical layout execution, or target performance. |

This evidence establishes a coherent vertical slice and a testable abstraction.
It does not yet establish competitive inference performance or a publishable
systems result.

## Nearest systems and the actual gap

The comparison is organized by the problem each system solves. It is not a
feature-count table.

| System family | Established strength | Overlap with Joggle | Boundary that Joggle must test |
| --- | --- | --- | --- |
| [MLIR](https://research.google/pubs/mlir-scaling-compiler-infrastructure-for-domain-specific-computation/) and [xDSL](https://doi.org/10.1145/3696443.3708945) | Extensible multi-level SSA infrastructure; xDSL makes MLIR-compatible prototyping lighter through Python and shared textual/IRDL descriptions. | Extensible types, operations, rewrites, serialization, and reusable compiler infrastructure. | Joggle replaces neither system. It must show that packaging co-evolving representation and compiler behavior behind typed Module interfaces removes measurable integration work without losing essential checking or interoperability. |
| [TVM](https://www.usenix.org/conference/osdi18/presentation/chen), [Relax](https://tvm.apache.org/docs/deep_dive/relax/index.html), and [TensorIR](https://doi.org/10.1145/3575693.3576933) | End-to-end model optimization, a graph abstraction, primitive tensor programs, procedural schedules, autotuning, and mature code generation across hardware. | Model rewriting, fusion, target mapping, and user-controlled low-level optimization. | Joggle does not currently compete on generated-code quality. Its narrower hypothesis is that graph, kernel, format, target, and analysis choices need not be fixed framework layers. A controlled extension study is required; architectural taste is not evidence. |
| [IREE](https://iree.dev/) and [ONNX-MLIR](https://github.com/onnx/onnx-mlir) | Production-oriented MLIR pipelines, native code, runtimes, deployment configurations, and broad model/target support. | ONNX ingestion and end-to-end compilation for edge deployment. | Joggle is currently a research substrate with no comparable runtime or native-code path. The valid comparison is extension coupling and co-design iteration, not deployment breadth. |
| [TileLang](https://proceedings.iclr.cc/paper_files/paper/2026/hash/76fb92288bf90360c527efb0d1c2aba6-Abstract-Conference.html) and [PTO-DSL](https://github.com/huawei-csl/pto-dsl) | Expert-facing kernel programming with explicit tile, memory-placement, data-movement, scheduling, or NPU-native control. | User-defined target primitives and executable operator bodies. | Joggle should host or call such a kernel vocabulary as a Module, not clone it in the core. A tile, stream, or NPU syntax is therefore an extension case rather than Joggle's universal IR. |
| [MATCH](https://arxiv.org/abs/2410.08855) | Retargetable edge-DNN deployment through customizable hardware execution modules, memory descriptions, operator APIs, cost models, and measured execution on heterogeneous MCUs. | This is the closest target-domain competitor: configurable operators, hardware models, graph transformation, resource reasoning, and edge deployment. | Joggle must demonstrate a benefit beyond renaming MATCH/TVM concepts: formats, operator semantics, transformations, analyses, and targets must be independently installable and composable, with lower measured framework coupling, on more than one real target. |
| [Astra](https://doi.org/10.1145/3297858.3304072) | Compiler/runtime co-design that enumerates whole-program variants and uses predictable repeated training iterations plus fine-grained measurements to select and prune online. | It shows why a static cost model need not make every final policy decision. | Joggle has no online exploration today. Repeated inference may provide feedback opportunities, but cold-start limits, input variation, and edge resource budgets can invalidate Astra's amortization argument. This is a future hypothesis, not a present contribution. |

### Extension-seam audit

A focused primary-source audit on 2026-09-04 sharpened the comparison. TVM's
[BYOC workflow](https://tvm.apache.org/docs/how_to/tutorials/bring_your_own_codegen.html)
explicitly requires pattern registration, graph partitioning, code generation,
and runtime execution, while its
[custom-datatype mechanism](https://tvm.apache.org/2020/09/26/bring-your-own-datatypes)
uses a separate datatype and lowering registry. IREE's
[compiler API](https://iree.dev/reference/bindings/c-api/) exposes plugins for
targets, dialects, passes, and pipelines, and its
[deployment model](https://iree.dev/guides/deployment-configurations/) pairs
compiler target backends with runtime HAL drivers. TileLang's
[backend layout](https://github.com/tile-ai/tilelang/blob/main/tilelang/backend/README.md)
separately registers pass pipelines, host code generation, device code
generation, target operations, and runtime integration. ONNX-MLIR's
[operation guide](https://github.com/onnx/onnx-mlir/blob/main/docs/ImportONNXDefs.md)
combines generated dialect definitions with explicit hooks for custom
verification, import, shape inference, and later lowering.

Those seams are intentional engineering choices, not defects. A plain C
emitter would therefore reproduce an established capability without testing
Joggle's thesis. The discriminating experiment is *source closure*: after a
new kernel is installed, can the same typed declaration be specialized through
its nested source bodies to a small admitted primitive boundary, with no new
operator-name registry or opaque native implementation? The implemented
semantic linker now answers that structural question for one custom kernel and
the standard ResNet-18 path, and the artifact emitter carries its linked
Functions. The next executable emitter must map only the admitted primitive
boundary; reconstructing the high-level graph through another name-dispatch
table would count as evidence against the thesis.

The open gap is therefore not “a lighter MLIR” or “a more general TileLang.” It
is whether a small typed extension plane can let co-design researchers change
several compiler roles together without encoding those roles in the framework,
and whether that flexibility survives real execution constraints.

## Candidate paper thesis

A defensible thesis, subject to the experiments below, is:

> A typed Module/function extension plane reduces the integration boundary for
> AI hardware/software co-design: representation, transformation, executable
> semantics, target policy, and feedback artifacts can evolve as installable
> units while remaining end-to-end checkable and reproducible.

Three contributions could support that thesis:

1. A precise language and artifact model for Module identity, typed function
   composition, Known/Residual staging, and Module-owned extension values.
2. A compiler implementation showing independently installable formats,
   operators, transformations, targets, simulations, and emitters in one
   standard-model path without a fixed dialect/pass/backend ladder.
3. An evaluation that measures extension coupling and end-to-end consequences
   on real edge targets, including cases where analytical planning is corrected
   by execution feedback.

The third item is absent. Until it exists, Joggle is promising infrastructure,
not an ASPLOS result.

## Experiments required before writing the paper

### 1. Semantics and negative cases

- State the typing and staging rules precisely enough to predict when a call is
  Known, Residual, materializable, or rejected.
- Test incompatible Module versions, behavior identity, dependent types,
  failed transactions, non-convergent rewrites, and illegal target mappings.
- Extend the existing f32 ONNX Runtime differential test to each claimed
  format and model family. The current ResNet-18 case establishes one path;
  structural bodies alone do not establish broader numerical correctness.

### 2. Controlled extension study

Implement the same bounded additions in Joggle and the nearest appropriate
baseline: one numeric format, one fused operator with an explicit kernel, one
target resource/timing artifact, and one model-wide optimization. Record:

- framework-core files changed and extension-local files changed;
- registration/schema boilerplate and rebuild dependency;
- build, load, compile, and serialization time;
- artifact size and deterministic rebuild rate;
- which cross-extension compositions are statically checked.

Lines of code alone are not a usability result. The study must publish the
tasks, patches, failure criteria, and reviewer-reproducible artifacts.

### 3. Standard workloads and real targets

- Use versioned, externally maintained ONNX models or benchmark suites rather
  than invented toy networks; cover at least two materially different model
  families supported by the implemented operators.
- Add at least two distinct target paths relevant to the project, such as an
  FPGA implementation and a custom RISC-V implementation, without introducing
  either target into the core.
- Measure numerical agreement, compile time, peak memory, binary size, latency,
  throughput, and energy where available. Calibrate every reported analytical
  quantity against hardware or a validated cycle-accurate reference.
- Compare with TVM/MATCH, IREE, or ONNX-MLIR only where the same model, precision,
  kernel library, and hardware make the result meaningful.

### 4. Compiler/runtime feedback, only if it pays

Following Astra's lesson, a target Module may eventually enumerate a bounded
set of deployment variants while a runtime-facing Module records measurements
and selects a stable version. The experiment must report exploration overhead,
time to amortization, cache persistence, input sensitivity, and behavior under
resource changes. If edge inference does not provide enough safe repetitions,
this mechanism should be omitted rather than forced into the paper.

## Kill criteria and non-claims

Stop or narrow the thesis if any of these conditions holds:

- a realistic extension repeatedly requires a new core declaration kind,
  artifact owner, or role registry;
- the controlled study shows no material reduction in integration coupling over
  MLIR/xDSL/TVM;
- the uniform `fn` surface merely moves complexity into opaque native behavior;
- composition flexibility prevents effective optimization or useful diagnostics;
- one model, one format, or one synthetic target remains the only evidence;
- analytical timing cannot be calibrated, or online exploration cannot amortize
  its cost.

The current repository must not claim native code generation, hardware speedup,
cycle accuracy, numerical accuracy beyond the checked f32 ResNet-18 path, WCET,
energy improvement, broad ONNX coverage, or novelty over all compiler extension
systems.

## Venue gate

[ASPLOS 2027](https://www.asplos-conference.org/asplos2027/cfp/) explicitly
expects a substantive advance in architecture, operating systems, programming
languages, or their intersection, with implemented scope and trade-offs stated
clearly. Joggle fits only after a real compiler/runtime or compiler/hardware
mechanism is evaluated on hardware. The 2027 September cycle is not credible
from the present evidence state; rushing an infrastructure description would
fail the venue's stated bar.

[ACM Transactions on Architecture and Code Optimization
(TACO)](https://dl.acm.org/journal/taco) is a plausible journal route if the
work matures into a precise extensibility model plus substantial code
optimization and architecture evaluation. It should not be confused with the
Tensor Algebra Compiler. A language-only design or an unvalidated analytical
target is insufficient for this route as well.

## Immediate engineering boundary

The next implementation work should strengthen evidence, not add another core
abstraction:

1. make one executable emitter map the bundled Functions' admitted primitives,
   then execute the already packed artifact and calibrate the Anchor timeline;
2. extend differential correctness to every claimed format and model family;
3. only then add bounded variant selection and measurement feedback as ordinary
   Module-defined types and functions.

Tile, stream, ISA, FPGA, runtime-feedback, and device-specific concepts remain
Module content. If one of them must become a new universal Joggle object, that
is evidence against the current thesis and must trigger a design review.
