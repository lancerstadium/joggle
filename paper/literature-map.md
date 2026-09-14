# Research map: where Joggle can and cannot contribute

This document is the evidence ledger for the EuroSys paper. It is not a list of
systems that resemble Joggle. Its purpose is to expose the strongest competing
explanations, identify a falsifiable systems problem, and derive the experiments
that would be required to support a paper.

Only primary papers, proceedings pages, and project documentation are treated as
evidence. A work enters the manuscript bibliography only after its metadata and
the claim attached to it have both been checked. The target remains at least 50
*used* references, but citation count is not an evaluation metric.

## 1. The problem is not “too many IRs”

Several tempting versions of the Joggle story are already solved or are too weak
for EuroSys:

- **Reusable multi-level compiler infrastructure.** MLIR explicitly targets
  reusable, extensible infrastructure across abstraction levels, domains,
  targets, and execution environments. A smaller IR is not, by itself, a new
  systems idea. [MLIR, CGO 2021](https://research.google/pubs/mlir-scaling-compiler-infrastructure-for-domain-specific-computation/)
- **Cross-level composition.** Relax represents graph computation, loop-level
  tensor programs, and external calls together, with first-class symbolic shape
  tracking. “Joggle can mix abstraction levels” is therefore not a sufficient
  contribution. [Relax, ASPLOS 2025](https://arxiv.org/abs/2311.02103)
- **User-extensible targets.** Exo externalizes target instructions, memories,
  accelerator state, and scheduling policies into user code while checking
  transformation safety. “Users can register hardware behavior” is already an
  established design point. [Exo, PLDI 2022](https://people.csail.mit.edu/yuka/pdf/exo_pldi2022_full.pdf)
- **Generated accelerator backends.** ACT generates instruction selection and
  memory allocation from parameterized tensor-ISA descriptions; ATLAAS lifts
  RTL-derived semantics into such descriptions. Handwritten modules are not
  automatically more agile than generated backends.
  [ACT](https://act-compiler.github.io/assets/pdf/act-arxiv.pdf),
  [ATLAAS](https://arxiv.org/abs/2604.13523)
- **Concise high-performance kernels.** TileLang, Triton, Halide, and Exo already
  provide substantially more mature kernel-authoring surfaces than Joggle.
  [TileLang](https://arxiv.org/abs/2504.17577)
- **Automatic cross-level optimization.** Mirage and Axon jointly explore
  algebra, fusion, layout, scheduling, and instruction selection. A generic
  rewrite API is not comparable to their optimizers.
  [Mirage](https://arxiv.org/abs/2405.05751),
  [Axon](https://arxiv.org/abs/2606.26344)

The TVM lineage reinforces this conclusion. Halide/TE separated algorithm from
schedule; TensorIR made schedulable blocks explicit and inspectable; Relay added
program semantics; Relax restored cross-level composition and dynamic shapes.
Multiple abstractions were introduced because different information is useful at
different times, not because compiler designers failed to seek uniformity.

## 2. A sharper systems problem: extension boundaries move

The literature instead exposes a temporal problem. Most systems make an
extension productive after one boundary has become stable:

| System family | Boundary made programmable | What is deliberately outside that boundary |
| --- | --- | --- |
| MLIR / ONNX-MLIR | Dialects, conversions, passes, runtime hooks | The extension author coordinates the involved dialect and driver contracts |
| Relax / TensorIR | Graph–tensor–library composition and schedules | Target-specific semantics and artifact conventions remain separate extension concerns |
| Halide / TileLang / Triton | Kernel algorithm, dataflow, and scheduling | Whole-model import, fallback, deployment ABI, and arbitrary target evolution |
| Exo | Trusted user schedules, instructions, memories, and configuration | Whole-model semantic import and automatic backend construction |
| ACT / ATLAAS | Tensor-ISA or recovered RTL semantics | Model-level experimentation and user-controlled mixed implementation policies |
| Ladder / sparse edge stacks | A chosen datatype, encoding, mapping, and kernel path | A general mechanism for revising a different vertical experiment |
| Mirage / Axon | A fixed search/synthesis problem and target model | The surrounding deployment stack and evolving research interface |

These boundaries are sound engineering choices. The unresolved case is early
model–hardware co-design, where the boundary itself is an experimental variable.
A low-precision or sparse idea may revise, together:

1. the source relation or fallback semantics;
2. the numeric or storage representation;
3. tensor structure and loop/data-layout policy;
4. a parameterized instruction or external kernel;
5. the policy that selects among implementations; and
6. the artifact ABI, constants, and validation contract.

Exo observes that accelerator hardware/software interfaces are diverse and that
the rates of change across the stack are inverted relative to conventional
software. ACT shows that scratchpads and parameterized tensor instructions need
joint formal treatment. Ladder shows that an evolving datatype requires storage,
access, conversion, schedule, and hardware policy to move together.
[Ladder, OSDI 2024](https://www.usenix.org/conference/osdi24/presentation/wang-lei)
These results do not prove Joggle is needed; they establish that vertical change
is real and that several different fixed boundaries are useful.

We call the candidate gap **temporal extensibility**:

> Can a researcher revise a vertical model–machine experiment while keeping its
> semantic fallback, structural implementation, target assumptions, selection
> policy, validation, and emitted artifact connected in one distributable unit?

The quantity of interest is not source brevity. It is **evolution continuity**:
how much unrelated host machinery must change, how many contracts must be kept
in sync, and what previously working paths survive when the experiment changes.

Temporal extensibility is only one dimension of the proposed substrate. The
actual researcher-facing problem has five closures:

1. **Bootstrap closure:** the tool, dependencies, configuration, and time needed
   to reach the first inspectable program. Current TVM source installation
   requires a C++20 toolchain, CMake, LLVM, Python/FFI packaging, and selected
   optional SDKs; ONNX-MLIR requires a pinned LLVM/MLIR build and project-specific
   dependencies. These are justified costs of their ecosystems, but they matter
   for a research workbench and must be measured rather than described as
   “heavy.”
2. **Extension closure:** all source, declarations, registrations, build changes,
   and runtime pieces required for one independently distributable extension.
   TVM BYOC, for example, cleanly separates pattern registration, partitioning,
   code generation, and execution, while compiled codegen/runtime support still
   enters the TVM build. ONNX-MLIR similarly provides a documented accelerator
   plugin path with build, class, pass/dialect, conversion, and runtime hooks.
3. **Interaction closure:** whether a user can inspect, invoke, compose, replace,
   and debug semantic and target decisions through one public model, rather than
   learning one API per phase. Uniformity is valuable only if failures remain
   typed and local; otherwise it merely hides distinctions.
4. **Deployment closure:** whether the same extension carries constants,
   workspace, ABI, fallback, validation, and provenance into an ordinary
   executable artifact. A transformed IR fragment or generated kernel alone is
   not a completed experiment.
5. **Performance closure:** whether the artifact is competitive after all of the
   above. Low ceremony cannot excuse a 10--100x whole-model gap. Generic target
   policies and external primitives must be able to improve real functions
   without turning the host into a catalog of operator cases.

Joggle's intended value is the conjunction: a low-ceremony, distributable
compiler substrate whose program remains progressively inspectable while both
portable and machine-specific mechanisms are composed. Evolution continuity
explains why the pieces should share a lifecycle; the five closures define what
“lightweight and usable” must mean empirically.

## 3. Candidate Joggle mechanism

Joggle's candidate answer is **progressive exposure over typed functions**.
A source relation begins as a call with a typed contract. Modules may expose
more of its implementation only when a transformation or target needs that
detail: tensor structure, scalar/loop structure, storage policy, target
primitive, or artifact action. Portable and specialized implementations remain
alternatives in the same program rather than belonging to separate host paths.

The mechanism has four parts:

1. **One evolving program state.** Higher-level calls and exposed function
   bodies may coexist. This is not a claim that all abstraction levels are the
   same, nor that one universal syntax eliminates useful structure.
2. **Typed module functions.** A distributable module may define semantic
   relations, representations, structural transforms, target capabilities,
   choices, validation, and artifact actions without adding a new host-side
   subsystem for each category.
3. **Checked choice.** Handwritten policy, bounded search, synthesis, or an agent
   can propose candidates through the same interface; target queries, legality
   checks, oracle execution, and rollback remain compiler-owned.
4. **Artifact closure.** Constants, workspace, calls, ABI, and provenance are
   part of the successful result. Producing a pretty IR fragment is not
   deployment.

The novelty, if demonstrated, is therefore not “all compiler concepts are
functions.” It is that a small common mechanism preserves continuity while a
vertical extension changes and while different decision makers are substituted.

## 4. Generality and specialization are not opposites

Joggle should not be positioned as only a special-purpose edge compiler. The
mechanism is intended to host both:

- portable relations and transformations that work across targets;
- specialized datatypes, layouts, memories, instructions, and artifacts;
- manually authored decisions for inspectability;
- bounded search or synthesis for automation; and
- agent-generated proposals behind the same checked boundary.

This is **mechanism generality**, not evidence that the current implementation
matches the workload breadth of TVM, IREE, ONNX Runtime, or ExecuTorch. IREE is
an MLIR-based AOT compiler/runtime spanning datacenter through edge and reports
embedded runtime configurations as small as 30 KB. ExecuTorch preserves PyTorch
semantics while supporting pluggable on-device backends. Those are deployment
breadth baselines, not systems we can dismiss as heavyweight.
[IREE](https://iree.dev/),
[ExecuTorch, MLSys 2026](https://proceedings.mlsys.org/paper_files/paper/2026/hash/236f915dd02af4f11927f67330b21d4b-Abstract-Conference.html)

Edge inference is the stress case because unsupported paths are expensive and
resource behavior is visible. It is not the definition of the abstraction.
Recent sparse-MCU work illustrates the complete evidence chain expected from a
vertical result: encoding, kernels, ISA support, compiler integration, complete
CNN/ViT models, accuracy, and measured speedup.
[MLSys 2025 sparse MCU stack](https://proceedings.mlsys.org/paper_files/paper/2025/hash/8cb5b08f912600de3de07c6503599ba8-Abstract-Conference.html)

Likewise, LLM popularity does not justify adding a large model as decoration.
Attention is valuable only if it exercises a distinct boundary: dynamic shapes,
KV-cache representation, fused normalization/contraction, or JIT specialization.
FlashInfer is a relevant counterexample because it combines composable cache
formats, attention templates, JIT compilation, and runtime scheduling into a
purpose-built engine with end-to-end results.
[FlashInfer, MLSys 2025](https://proceedings.mlsys.org/paper_files/paper/2025/hash/dbf02b21d77409a2db30e56866a8ab3a-Abstract-Conference.html)

## 5. Strongest competing explanations

A credible paper must try to disprove itself against these explanations:

### C1. Joggle is a smaller reinvention of MLIR plus Relax

MLIR already supports extensible multi-level IR, and Relax already supports
cross-level calls and partial lowering. A surface-language comparison cannot
refute this. Joggle must show a matched evolving extension whose implementation
and revision touch fewer *independent contracts* while preserving fallback and
artifact correctness.

### C2. Exo or ACT is the correct abstraction once the hardware matters

Exo gives stronger kernel scheduling control; ACT gives stronger automatic
backend construction and formal coverage. Joggle must not compete on their
chosen tasks. Its possible advantage is coordinating a still-moving experiment
that includes model semantics and deployment policy as well as target behavior.

### C3. A few project files only measure software style

Changed lines/files are meaningless without a controlled change task. The study
must freeze initial behavior, issue an unforeseen revision after both baselines
work, and measure invalidated contracts, host changes, fallback preservation,
revalidation effort, and final behavior.

### C4. Flexibility merely moves complexity into conventions

Uniform functions can erase useful distinctions and make legality implicit. We
must report type/verification failures, ambiguous resolution, rollback behavior,
and the amount of module-local boilerplate. If failures become later or less
diagnosable, the abstraction loses.

### C5. Transparent generated C is too slow to matter

Current Joggle C is substantially behind a production runtime on several full
models. This is a scientific blocker, not a footnote. The paper needs a generic
target policy—access analysis, tiling, packing/layout, vectorization and external
primitive selection—that improves multiple functions and models without
operator-name cases. Otherwise the result is a language prototype, not an
inference system.

## 6. Falsifiable claims and required experiments

### E1 — Evolution continuity

Implement one vertical extension containing a source relation, portable fallback,
custom representation, structural policy, target primitive, selector, validation,
and artifact rule. Once working in Joggle and two native baselines, reveal two
pre-registered revisions, for example:

- add grouping or a variable tile parameter to the primitive;
- change the scratchpad/alignment and packed-constant contract.

Compare with TVM/Relax+TensorIR and ONNX-MLIR/MLIR using each system's documented
native path. Report changed host files, extension files, central registrations,
contracts requiring coordinated edits, invalidated artifacts, fallback
preservation, build/compile time, and oracle correctness. ONNX-MLIR's documented
accelerator path explicitly involves build configuration, an accelerator class,
dialect/pass registration, conversion hooks, and runtime code, which makes it a
useful—but not automatically inferior—matched baseline.
[ONNX-MLIR accelerator guide](https://onnx.ai/onnx-mlir/AddCustomAccelerators.html)

**Failure condition:** Joggle only reduces textual boilerplate, or the revision
requires hidden core changes/conventions that are absent from the accounting.

### E2 — Decision-maker substitution

Hold program, candidates, legality checks, and target description fixed. Replace
a handwritten chooser with bounded enumeration and then a generated/agent policy
through the same public interface. Report host changes, rejected proposals,
rollback, target queries, search cost, correctness, and selected artifact.

Mirage and Axon are performance/automation references, not expected speed
baselines for a lightweight CPU substrate. They establish that semantic search
and cross-level optimization are substantive algorithms; Joggle's narrower claim
is that such decision makers can share a checked integration boundary.

**Failure condition:** each chooser needs a new execution path, or generated
choices can bypass validation and artifact checks.

### E3 — Target-policy usefulness

Build one generic policy over exposed function bodies: affine/access analysis,
loop transformation, immutable-weight packing, alignment/vector contracts, and
external primitive selection. It must benefit several contraction/convolution
shapes and at least two complete models without matching frontend operator names.

Report dense operator matrices as speedup over each matched baseline, plus
whole-model latency, peak memory/workspace, compile time, artifact size, accuracy,
and unsupported cells. Compare ONNX models with ORT, TVM, and ONNX-MLIR; compare a
TFLite model with LiteRT. ncnn/IREE are included only where the exact model,
datatype, thread count, and execution path can be matched.

**Failure condition:** gains exist only for a hand-selected tiny operator, or
full models remain far behind production runtimes without a diagnosed boundary.

### E4 — Representation and failure boundary

Use pinned, ordinary models spanning CNN classification, detection, ViT, and a
compact attention/language workload. Separate decode, import, relation,
exposure, preparation, compilation, execution, and numeric validation. Add
property/fuzz tests for module loading, ONNX protobuf, TFLite FlatBuffers, native
ABI, IR editing, and transaction rollback.

**Failure condition:** “supported” means only parsed, or unsupported behavior is
collapsed into one opaque failure count.

## 7. Related-work organization for the paper

The final Related Work should argue by mechanism, not by project chronology:

1. **Multi-level compiler composition:** MLIR, ONNX-MLIR, Relay/Relax,
   TensorIR, IREE, Glow.
2. **Controllable tensor and kernel construction:** Lift/RISE/Elevate, Halide,
   Exo, Triton, TileLang, TACO, HeteroCL.
3. **Automatic transformation and synthesis:** Ansor, Roller, Welder, Mirage,
   Axon, ACT, ATLAAS, LLM-aided compilation.
4. **Representation/hardware co-design:** Ladder, SparseTIR, PULP-NN, DORY,
   MCUNet, VTA, sparse-MCU stacks.
5. **Deployment closure and reliability:** ONNX Runtime, LiteRT/TFLM, ncnn,
   IREE, ExecuTorch, NNSmith, HirGen, metamorphic testing.

Each paragraph must end with a precise distinction, not a generic “unlike prior
work.” The distinctions above are hypotheses until E1–E4 produce evidence.

## 8. Reading queue to reach bibliography depth

The next full-text verification pass must cover at least these clusters:

- TVM, Relay, TensorIR, Relax, BYOC, MetaSchedule/Ansor;
- MLIR Transform dialect, ONNX-MLIR, IREE, Glow, XLA/OpenXLA;
- Lift, RISE, Elevate, Halide, Tiramisu, TACO, DaCe, HeteroCL, Exo;
- Triton, TileLang, Ladder/BitBLAS, Welder, Roller, Mirage, Axon;
- ACT, TAIDL, ATLAAS, 3LA, VTA, Gemmini, Timeloop, MAESTRO;
- TFLM, CMSIS-NN, DORY, PULP-NN, MCUNet/MCUNetV2, vMCU, MATCH;
- ONNX Runtime, LiteRT, ncnn, ExecuTorch, MNN;
- NNSmith, HirGen, compiler bug studies, metamorphic testing;
- ViT, BERT/GPT-style attention, FlashInfer, and one edge-LLM system only where
  they exercise a named compiler boundary.

For each work, record: problem, boundary made programmable, information kept
across stages, who makes decisions, target/runtime assumptions, artifact
contract, evaluation scope, and the strongest counterexample it creates for
Joggle.

## 9. Consequence for title and narrative

The working title should describe the mechanism and the unresolved problem, not
promise an inference-performance victory that has not been measured:

> **Joggle: Malleable Compilation for Evolving Models and Machines**

“Progressive exposure” is the mechanism name inside the paper. “Evolving models
and machines” states the temporal systems problem. The title remains provisional
until E1 and E3 succeed.

The Motivation should follow this chain:

1. a concrete vertical change whose semantics, representation, target behavior,
   choice, and artifact evolve together;
2. why three strong existing approaches each solve a different stable boundary;
3. why the cost is temporal coupling rather than merely the number of IRs;
4. the progressive-exposure hypothesis and its failure cases; and
5. the evidence required to accept the hypothesis.

This work used AI-assisted search and synthesis. Every citation and quantitative
claim must be checked against the linked primary source and the final artifact;
queued readings and agent-derived interpretations are not evidence.
