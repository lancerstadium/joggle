# Literature map

This file records competing mechanisms and source boundaries. The active thesis,
experiments, calendar, and writing plan live only in [plan.md](../plan.md).
The bibliography remains in [references.bib](../references.bib). A linked source
is not a claim that every result in it has been independently reproduced.

## Closest mechanism comparisons

| Work | Existing capability to credit | Joggle question to investigate |
| --- | --- | --- |
| MLIR and Transform | Extensible operations/types/interfaces; interpreted transformation IR over payload IR; handles, effects and failure semantics; transformations can themselves be optimized | Can the same function-body facilities derive useful compiler behavior across roles with less integration work, without weakening semantic checks? |
| xDSL | Lightweight Python infrastructure interoperating with MLIR representations and declarative definitions | Does Joggle offer a substantive composition/execution difference, beyond language and installation preferences? |
| TVM, TensorIR, Relax | Shared program container, graph/tensor/external-call composition, symbolic shapes, schedulable tensor blocks | Does local exposure within one function representation preserve useful editability or reduce coordination in a matched change? |
| AnyDSL and LMS | Staging and partial evaluation specialize reusable abstractions and interpreters | What remains editable when, what can be specialized, and what cost/boundary differs from staging alone? |
| Exo, Elevate, RISE/Shine | Programmable schedules/rewrites; reusable strategies and optimization of optimization structure | Does a demonstrated analysis/representation/generator case go beyond another scheduling language? |
| Spoofax, JastAdd, Nanopass | Modular language definitions, analyses, transformations, code generation and compiler construction | Which measured consequence is not already explained by modular compiler engineering? |

Primary sources:

- [MLIR, CGO 2021](https://doi.org/10.1109/CGO51591.2021.9370308);
  [Transform, CGO 2025](https://doi.org/10.1145/3696443.3708922);
  [Transform documentation](https://mlir.llvm.org/docs/Dialects/Transform/);
  [interfaces](https://mlir.llvm.org/docs/Interfaces/);
  [dynamic dialects](https://mlir.llvm.org/docs/DefiningDialects/).
- [xDSL](https://arxiv.org/abs/2311.07422).
- [Relax, ASPLOS 2025](https://doi.org/10.1145/3676641.3716249);
  [TensorIR](https://arxiv.org/abs/2207.04296);
  [TVM architecture](https://tvm.apache.org/docs/arch/);
  [TVM runtime](https://tvm.apache.org/docs/arch/runtime.html).
- [AnyDSL](https://compilers.cs.uni-saarland.de/papers/anydsl.pdf);
  [LMS](https://www.cs.purdue.edu/homes/rompf/papers/rompf-scala16.pdf).
- [Exo](https://people.csail.mit.edu/yuka/pdf/exo_pldi2022_full.pdf);
  [Exo 2](https://arxiv.org/abs/2411.07211);
  [Elevate](https://arxiv.org/abs/2002.02268);
  [RISE/Shine](https://thok.eu/publications/2022/rise.pdf).
- [Spoofax](https://doi.org/10.1145/1869459.1869497);
  [JastAdd](https://doi.org/10.1016/j.scico.2007.02.003);
  [Nanopass](https://doi.org/10.1017/S0956796805005605);
  [MAGIK](https://www.usenix.org/conference/dsl-97/incorporating-application-semantics-and-control-compilation);
  [Datalog IR modules](https://doi.org/10.1145/3689484.3690737).

### Core claim check

Checked on 2026-09-16 against the primary passages below. Coverage is limited
to these mechanism claims, not full-paper reading, a systematic novelty search,
retraction screening, or verification of all bibliography fields. Section
locators refer to the linked technical texts; preprint pagination can differ
from the published proceedings. Existing citation keys are retained.

| Manuscript location | Primary passage inspected | Supported comparison and correction |
| --- | --- | --- |
| Related Work: programmable implementations | [Transform paper](https://arxiv.org/pdf/2409.03864), Sections 3.3--3.4, especially PDF pp. 9--10; [official dialect documentation](https://mlir.llvm.org/docs/Dialects/Transform/) | Scripts can themselves be analyzed, inlined, simplified, and introspected. The manuscript now credits this directly. Compare editing a represented algorithm with the implementation boundary of an invoked primitive; do not claim that transformation-on-transformation is new. |
| Related Work: staging | [AnyDSL paper](https://compilers.cs.uni-saarland.de/papers/anydsl.pdf), Sections 2--2.1 and 3.1, printed pp. 119:3--119:6 | The paper distinguishes closure-based shallow embedding from explicit syntax supporting later rewrites. The revised comparison identifies this embedding boundary without denying Thorin IR or general specialization. Added the paper's stated 30-page count to its bibliography entry. |
| Related Work: invocation | [TVM runtime documentation](https://tvm.apache.org/docs/arch/runtime.html#packedfunc), PackedFunc and Module sections | Type-erased cross-language calls include callbacks, compiler passes, and deployed functions. A call interface is not an algorithm-body representation. This does not imply that the rest of TVM lacks editable IR. |
| Related Work: module boundaries | [xDSL paper](https://arxiv.org/pdf/2311.07422), Sections 1--2, PDF pp. 1--3; [current project overview](https://xdsl.dev/) | Sidekick compilation exchanges IR and definitions and can insert a Python prototype into an MLIR flow. The paragraph now identifies this reuse boundary rather than describing xDSL only as lightweight. The 2024 revision inspected does not establish a current absence of any transformation-sharing facility. |
| Related Work: mixed representations | [Relax paper](https://arxiv.org/html/2311.02103), Sections 3.3, 4.2, 4.4, 4.6 | Functional graph calls connect to destination-passing tensor/library implementations. Partial lowering, analysis feedback, and joint caller/callee rewriting are explicit capabilities. The text now credits those mechanisms rather than only mixed storage in IRModule. |

The resulting comparison concerns **what implementation structure is exposed to
an edit and how the edited definition is executed**. It is not an expressiveness
ranking: these passages do not establish that competitors cannot reproduce the
Joggle workflow. The decisive control should carry the same internal algorithm
change through each system's natural extension path, preserving original and
revised behavior. Current Joggle isolation tests and separate extension tasks
remain capability evidence, not measurements of that comparative benefit.

### Rhetorical pattern from the closest papers

A 2026-09-16 title/abstract search for user-extensible compilers, programmable
compiler algorithms, compiler metaprogramming, and transformation languages
did not reveal a closer mechanism paper than the works already above. This is
a bounded search, not an exhaustive novelty claim. The Transform dialect,
Exo 2, and Relax abstracts nevertheless provide a useful presentation pattern:

1. start from the workload or user pressure, then identify the specific control
   boundary rather than asserting that compilers are generally inflexible;
2. name a small mechanism vocabulary and state the represented objects on which
   it operates; and
3. close with concrete case studies and their measured consequence.

Joggle should follow that order while keeping a stricter evidence boundary.
Its abstract can name function/closure copying, module resolution and
replacement validation, and consumer-directed exposure, but the current case
studies do not establish reduced revision effort or competitive inference
performance. Transform already claims reusable composition without rebuilding,
Exo 2 already claims extensible inspection/action/reference mechanisms, and
Relax already claims one representation spanning graphs, tensor programs, and
external calls. Joggle's comparison must therefore stay on editable algorithm
bodies, independent derived execution, checked module replacement, and their
costs. The current revised abstract follows this problem--mechanism--evidence--
boundary sequence and keeps `fourteen` as its only quantitative fact.

### Implementation-boundary comparison

The 2026-09-16 follow-up inspects the following passages for the programmable-
implementations subsection. These are assistant-inspected sources; human
author verification remains pending. No comparative execution was performed.

| Claim ID | Evidence and locator | Bounded interpretation |
| --- | --- | --- |
| `C-related-transform` | `E-transform-script`: [paper](https://arxiv.org/pdf/2409.03864), Section 3.4; `E-transform-implementation`: [official tutorial, Chapter 2](https://mlir.llvm.org/docs/Tutorials/transform/Ch2/), `TransformOpInterface::apply` implementation | Transform scripts admit optimization; the tutorial's primitive action is C++ reached through an interface method. This distinguishes represented orchestration from that implementation, not all possible MLIR extensions. |
| `C-related-exo` | `E-exo-schedule`: [author paper](https://arxiv.org/pdf/2411.07211), Sections 2--3 and 5.1--5.2, PDF pp. 2--3 and 6--8 | Python schedules compose checked actions over object procedures. Versioned cursors support forwarding. This is substantive programmable compiler reuse, not merely a fixed scheduling API. |
| `C-related-staging` | `E-anydsl-specialization`: [author paper](https://compilers.cs.uni-saarland.de/papers/anydsl.pdf), Sections 2.1 and 3.1, Figure 2, printed pp. 119:4--119:6 | Closure composition plus partial evaluation removes abstraction overhead. The shallow-embedding restriction concerns subsequent embedded-program rewriting, not the existence of Thorin or the ability to change library source. |
| `C-onnxmlir-audit` | `E-onnxmlir-source`: pinned ONNX-MLIR `4a13c34`, `src/Compiler/CompilerPasses.cpp` lines 355--360, 376--385, 480--492 (OneShotBufferize, BufferLoopHoisting, buffer-deallocation pipeline, OptimizeAllocationLiveness); `src/Compiler/CompilerOptions.cpp` lines 261--266 and `src/Conversion/ONNXToKrnl/Math/Elementwise.cpp` lines 84--85 (`--enable-krnl-buffer-reuse`, per-operation) | Storage decisions are upstream MLIR passes with one per-operation flag, so a cross-operation ranking change is a C++ pass plus rebuild, the same route shape as TVM. Source audit on 2026-09-17; not executed or measured. See the [derivation study](../experiments/derive-prepare.md#pinned-external-interface-audit). |

The local comparison distinguishes changing a compiler algorithm's decision
from specializing an algorithm for supplied inputs or composing trusted
transformations. It does not claim that competitors cannot encode or implement
the same algorithm. Joggle's structural validity checks do not replace the
semantic obligations enforced by domain-specific transformations. Its source-
defined bodies and opaque native boundary must be described together.

Exo 2's bibliography entry now uses the published record while retaining its
existing citation key. The paper title page and
[publisher-deposited Crossref record](https://api.crossref.org/works/10.1145/3669940.3707218)
agree on the DOI and authors; the latter supplies the proceedings title,
2025 publication date, and pages 426--444. The extended arXiv PDF has 35 pages;
that is not the proceedings page count. No full-bibliography audit is implied.

### TVM evolution: what it teaches

TE exposes computation/schedule separation; TensorIR makes schedulable program
structure explicit; Relay and Relax address graph/program and dynamic-shape
needs. These are overlapping architectural developments, not six consecutive
replacements of one IR. The user's TVM-history article motivates inspection of
the primary designs; it is not evidence that every abstraction adds waste.

Relax already places graph functions, tensor programs and external calls in a
cross-level architecture. TVM's IRModule can contain distinct function variants;
PackedFunc and runtime modules connect compiler callbacks, compiled functions,
devices and RPC. Joggle must not claim that TVM lacks a common container,
mixed-level execution, or broadly reusable invocation. The concrete question is
which body representation and editing rules an author uses during a change,
and whether differences matter in an actual composed artifact.

### Transforming a transformation is prior art, not a novelty shortcut

MLIR Transform makes strategies executable IR, with explicit payload handles and
effects. Its own transformations can be optimized. Elevate and Exo 2 likewise
make strategy composition substantive. Describing an optimizer as `f3(f2)`
does not establish a new capability.

Joggle's candidate distinction is reuse of inspectable function bodies across
computation and compiler roles, with executable derived versions. Measure the
representation, legality/effect boundary, reuse, and cost. A copied function
with a changed constant proves an API path, not compiler acceleration.

### Module organization and prior art

Spoofax already composes syntax, analysis, transformation, generation, and editor
services. MAGIK already exposes IR editing to dynamic extensions. A text module,
one invocation spelling, rollback, or an installation command alone is not the
research result. Avoid the former yes/no ecosystem matrix: absence from a paper
or one failed implementation is not proof that an ecosystem cannot do it.

## Current pressures and specialized systems

### Scenario evidence

The approved motivation follows a new representation from numerical exploration
to target implementation and repeated revision. The wording and mechanism
mapping live in [plan.md](../plan.md#argument-to-preserve), not in a second outline.
The following primary records were checked on 2026-09-15. Paper metadata and
abstracts, and the MX repository's README, support these bounded statements;
this is not a claim of full-paper reading or reproduced performance.

| Source | Observation supported by the source | Design inference and boundary |
| --- | --- | --- |
| [Microscaling Data Formats for Deep Learning](https://arxiv.org/abs/2310.10537), 2023 preprint | MX combines narrow element formats and block scaling | A representation change can affect both numerical meaning and physical implementation; it does not prove that Joggle supports MX |
| [Microsoft MX emulation library](https://github.com/microsoft/microxcaling), project documentation | Emulates representable values using wider arithmetic; offers model replacements and C++/CUDA extensions | Numerical exploration and native deployment are distinct tasks; the project's structure is not a measured integration disadvantage |
| [TileLang](https://arxiv.org/abs/2504.17577), 2025 preprint | Separates kernel dataflow from customizable scheduling choices | Investigate algorithm changes beyond available configuration; do not claim TileLang lacks low-level control |
| [KernelBench](https://proceedings.mlr.press/v267/ouyang25a.html), ICML 2025 | Evaluates correctness and speed, including execution/profiling feedback for iterative refinement | Feedback cost is relevant to repeated candidates; compile-time dominance and Joggle agent-success gains remain unmeasured |

Retain the distinction between a semantics-preserving layout revision and a
numerical-rule revision. The latter needs a fresh accuracy/error evaluation.
The existing Conv packing/ABI study represents only the former class and does
not become an MX study through new Motivation wording.

| Pressure | Relevant work | What the source can motivate, not prove about Joggle |
| --- | --- | --- |
| Rapidly changing graph structures | [PluS](https://www.usenix.org/conference/atc25/presentation/wu-ruofan) | Limits of fixed rule/template coverage |
| Numeric representations and hardware mappings evolve together | [Ladder](https://www.usenix.org/conference/osdi24/presentation/wang-lei), [sparse MCU stack](https://proceedings.mlsys.org/paper_files/paper/2025/hash/8cb5b08f912600de3de07c6503599ba8-Abstract-Conference.html) | Coupling among encoding, access, conversion and mapping |
| User-controlled kernels | [TileLang](https://arxiv.org/abs/2504.17577), Exo, Halide, Triton | Concise control and mature scheduling are real alternatives, not missing features |
| Automatic backend construction | [ACT](https://act-compiler.github.io/assets/pdf/act-arxiv.pdf), [ATLAAS](https://arxiv.org/abs/2604.13523) | Joint instruction/memory modeling; generated rather than handwritten integration |
| Cross-level optimization | [Mirage](https://arxiv.org/abs/2405.05751), [Axon](https://arxiv.org/abs/2606.26344) | Search/synthesis algorithms and target models, not merely a rewrite interface |
| Agent-generated optimization | [Autocomp](https://arxiv.org/abs/2505.18574), [ARGUS](https://mast.stanford.edu/pubs/argus_agentic_gpu_optimization_guided_by_data_flow_invariants/), [KernelBench](https://proceedings.mlr.press/v267/ouyang25a.html) | Need for executable validation and useful feedback; no implied Joggle agent-success gain |
| Deployment and runtime specialization | [IREE](https://iree.dev/), [ExecuTorch](https://proceedings.mlsys.org/paper_files/paper/2026/hash/236f915dd02af4f11927f67330b21d4b-Abstract-Conference.html), [FlashInfer](https://proceedings.mlsys.org/paper_files/paper/2025/hash/dbf02b21d77409a2db30e56866a8ab3a-Abstract-Conference.html) | Complete deployment paths, small runtimes, and specialized cache/JIT mechanisms |

[ONNX-MLIR's accelerator guide](https://onnx.ai/onnx-mlir/AddCustomAccelerators.html)
is the starting point for its native extension comparison. Build/dialect/runtime
boundaries must be counted from preserved implementation changes, with existing
facilities reused. Build inconvenience is measurable adoption cost, not proof
of weaker optimization or expressiveness.

## Mechanisms worth transferring

These are design leads, not features claimed by the current implementation.

- [weval](https://cfallin.org/pubs/pldi2025_weval.pdf): interpreter specialization
  motivates specializing compiler procedures against stable configuration.
  It does not imply that arbitrary mutations or analyses can be safely folded.
- [Traversal fusion](https://arxiv.org/abs/1904.07061): combining compatible
  traversals can reduce repeated work. Applicability needs actual access/effect
  conditions, not merely two functions traversing the same IR.
- [Compiled database queries](https://www.vldb.org/pvldb/vol4/p539-neumann.pdf):
  removing interpretation/materialization overhead motivates examining compiler
  query execution. Dataflow assumptions do not automatically hold for IR edits.
- [Rust incremental queries](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation-in-detail.html):
  dependencies and invalidation are prerequisites for incremental reuse.
  Revision-wide memo invalidation is not equivalent.
- [RCU](https://docs.kernel.org/RCU/whatisRCU.html): stable readers and separate
  update versions suggest deriving inactive code rather than mutating executing
  definitions. This analogy does not require implementing kernel-style RCU.
- [MIR](https://github.com/vnmakarov/mir): a compact execution backend is an
  engineering option after profiling; JIT alone does not repair tensor locality,
  packing, or vectorization.

## Reading and comparison discipline

Organize manuscript discussion by the problem made programmable:

1. compiler construction and extensible IR: MLIR, xDSL, ONNX-MLIR, Spoofax,
   JastAdd, Nanopass, Polyglot, Silver;
2. staging and programmable transformation: AnyDSL, LMS, Lift, RISE, Elevate,
   Shine, Halide, Exo, Tiramisu, TACO, DaCe, HeteroCL;
3. multi-level programs: TVM, Relay, TensorIR, Relax, IREE, Glow, XLA;
4. automatic optimization/backend construction: Ansor, MetaSchedule, Roller,
   Welder, Mirage, Axon, ACT, ATLAAS, TAIDL, 3LA;
5. representation and deployment: Ladder, SparseTIR, VTA, Gemmini, Timeloop,
   MAESTRO, TFLM, CMSIS-NN, DORY, PULP-NN, MCUNet, vMCU, MATCH;
6. correctness and generated programs: NNSmith, HirGen, metamorphic testing,
   agent-generated kernel work.

This list includes pending reading, not a claim of completed full-text review.
Retain at least 50 relevant, actually used and verified references as requested;
do not manufacture relevance to meet the count. For each manuscript claim,
record the source passage, supported boundary, target assumptions, evaluation
scope, and strongest counterexample. Distinguish documented ability from a
measured baseline and a design analogy from an implemented mechanism.

Current evidence cannot establish universal usability, automated safe
composition, fast inference, or an agent advantage. The research plan asks
specific experiments to distinguish a useful new mechanism from a concise
implementation of established ideas.
