# Research map: malleable compilation and progressive IR

This document is the evidence ledger for the EuroSys paper. It is not a list of
systems that resemble Joggle. Its purpose is to expose the strongest competing
explanations, identify a falsifiable systems problem, and derive the experiments
that would be required to support a paper.

Only primary papers, proceedings pages, and project documentation are treated as
evidence. A work enters the manuscript bibliography only after its metadata and
the claim attached to it have both been checked. The target remains at least 50
*used* references, but citation count is not an evaluation metric.

## The problem is not another missing extension point

Joggle must be situated as compiler infrastructure before inference becomes the
evaluation domain. Older systems already establish almost every individual
ingredient that a superficial novelty claim might name:

- **Language workbenches and modular compilers.** JastAdd composes declarative
  attributes and rewrites into modular language extensions; Spoofax integrates
  syntax, analysis, transformation, code generation, and separately deployable
  editor plugins; Nanopass makes successive languages and small typed passes
  explicit. These systems show that modular language definition, one reusable
  transformation formalism, and incremental compiler construction are not new.
  [JastAdd](https://doi.org/10.1016/j.scico.2007.02.003),
  [Spoofax](https://doi.org/10.1145/1869459.1869497),
  [Nanopass](https://doi.org/10.1017/S0956796805005605)
- **Extensible IR ecosystems.** MLIR makes new operations, types, interfaces,
  dialects, and passes coexist in one infrastructure. xDSL deliberately trades
  production-compiler coupling for a Python sidekick that shares textual IR and
  declarative IR definitions with MLIR. This is the closest broad alternative to
  a lightweight research substrate and must become a primary baseline rather
  than a passing citation.
  [MLIR](https://research.google/pubs/mlir-scaling-compiler-infrastructure-for-domain-specific-computation/),
  [xDSL](https://arxiv.org/abs/2311.07422)
- **Controllable compilation.** The MLIR Transform dialect provides a uniform,
  extensible IR for selecting payload operations, composing existing compiler
  transformations, checking pre/postconditions, and integrating search without
  rebuilding the compiler for each script. Joggle cannot claim fine-grained,
  safe, reusable transformation control as novel.
  [Transform dialect, CGO 2025](https://doi.org/10.1145/3696443.3708922)

These works are antecedents, not the definition of Joggle's contribution.
Language workbenches establish modular syntax and semantics; MLIR/xDSL establish
open IR ecosystems; the Transform dialect establishes programmable, checked
transformation; TVM establishes reusable compiler and runtime functions. A
conjunction of old features is not by itself a new systems problem.

The new operating condition is **continuous specialization**. Model structures,
numeric formats, target capabilities, and optimization producers change while a
compiler experiment is still being constructed. Production stacks repeatedly
respond by adding useful representation and registration boundaries; specialized
DSLs shorten one part of the path by fixing the surrounding boundary. Joggle
asks whether a program and its compiler configuration can instead acquire
specialized detail locally, without replacing the program representation or
rebuilding the host for each new compiler role.

### AI-era pressure: more moving interfaces and more authors

The paper must ground this condition in current systems rather than merely
prefixing an old extensibility problem with “AI”:

| Changed condition | Consequence for compiler infrastructure | Joggle hypothesis |
| --- | --- | --- |
| Model families converge but continue to add local structures and fusions | Hard-wired graph rules lag; fixed templates break on small structural variation | Keep abstract relations and exposed bodies in one editable program |
| Datatypes and machine primitives evolve together | Semantics, storage, access, scheduling, and artifact contracts must be revised as one experiment | Add specialization through ordinary typed modules rather than a new host category |
| Experts, search, synthesis, and agents all produce compiler decisions | Inputs, authority, feedback, rejection, and replay must not depend on the producer | Give every producer the same typed functions, structural diagnostics, and transactions |
| A successful experiment must move between people and machines | A source patch or one-off optimization trace is not a reproducible compiler configuration | Install, resolve, upgrade, and retain the module and its dependency closure |

[PluS](https://www.usenix.org/conference/atc25/presentation/wu-ruofan)
provides direct evidence for the first row: recent expert graph optimizations
arrive faster than rule-based compiler support, while template systems are
sensitive to model variation. [Ladder](https://www.usenix.org/conference/osdi24/presentation/wang-lei)
provides the datatype-to-hardware example. [Autocomp](https://arxiv.org/abs/2505.18574)
and [ARGUS](https://mast.stanford.edu/pubs/argus_agentic_gpu_optimization_guided_by_data_flow_invariants/)
show that generated optimization depends on correctness, performance, and
structured compiler feedback. These papers motivate the changed operating
condition; Joggle must still demonstrate that progressive IR materially shortens
the path from a change to a checked artifact.

### Nearest-prior-work audit

The following matrix is a novelty audit, not a paper result table. A filled cell
means the cited system makes that property a documented part of its extension
model; it does not rank implementation quality.

| System | Adds vocabulary or semantics | Runs analyses or IR edits | Unifies compiler and artifact calls | Source-level package lifecycle | Checks dependent bindings on upgrade | One unit spans all roles |
| --- | --- | --- | --- | --- | --- | --- |
| JastAdd | yes | yes | no | specification composition | no | no |
| MAGIK | application semantics | yes, over lcc IR | no | dynamic native extension | no | no |
| LMS | staged DSL vocabulary | yes | code generation | trait composition | no | pipeline composition, not one package |
| MLIR | dynamic ops and types | passes and transforms | separate runtime mechanisms | plugin/dialect registration | API version only | no |
| TVM | extensible runtime objects | PackedFunc passes | yes, via PackedFunc/Module | registry and compiled modules | no module-source upgrade contract | no |
| Datalog IR modules | typed relations | compiler passes are external | bundles, not native artifacts | separate compilation and partial linking | link-time requirements | no |
| Joggle | typed vocabulary and semantics | typed transactional functions | typed functions construct artifacts | install/upgrade/uninstall | reverse-dependent call resolution | **yes, by construction** |

This audit prevents three overclaims. MAGIK already gives dynamically loaded
extensions direct IR edit access. LMS already names horizontal and vertical
extensibility. TVM already uses one callable abstraction across passes,
frontends, compiled modules, devices, and RPC. The Joggle claim is the remaining
conjunction: an extension's vocabulary, compiler actions, choices, and artifact
actions are source-level members of one typed package whose dependency and
upgrade semantics cover calls between those roles.

[MAGIK](https://www.usenix.org/conference/dsl-97/incorporating-application-semantics-and-control-compilation),
[LMS](https://www.cs.purdue.edu/homes/rompf/papers/rompf-scala16.pdf),
[MLIR dynamic dialects](https://mlir.llvm.org/docs/DefiningDialects/),
[TVM runtime](https://tvm.apache.org/docs/arch/runtime.html),
[Datalog IR modules](https://doi.org/10.1145/3689484.3690737)

### The idea stack

The paper should present Joggle as one coherent stack of ideas, not a bag of
features:

1. **Problem — continuous specialization.** The source program, optimization
   strategy, and target interface evolve concurrently; time to a checked
   artifact is a first-class systems cost.
2. **Representation — progressive IR.** One program may retain an
   abstract relation while selected functions expose tensor, loop, storage, or
   external-call detail. Specialization is local and does not erase the portable
   path.
3. **Interface — typed compiler functions.** Semantics, analyses, edits,
   selectors, validators, and artifact builders share structural types and live
   `Mod`/`Fn`/`Blk`/`Op`/`Val` handles. The producer may be handwritten code,
   search, synthesis, or an agent without changing the consumer contract.
4. **Safety — transactional authority.** Read-only functions cannot mutate;
   mutating functions commit only after verification; stale or foreign handles
   are rejected. This turns generated policy into a bounded compiler input.
5. **Reproduction — module lifecycle.** The dependency graph governs install,
   composition, upgrade, rollback, and removal. Upgrade validates call targets
   and signatures in reverse dependents, not only the upgraded file.
6. **Outcome — malleable compilation.** A module can preserve reference
   semantics and fallback while adding target-specific implementations and
   ordinary artifacts. Generality and specialization become coexisting program
   states rather than competing compiler architectures.

This stack supports three positive claims that are broader than inference:

- **Short idea-to-artifact path:** revise a cross-cutting idea without rebuilding or editing
  a host subsystem for every role it touches.
- **Open specialization:** add uncommon datatypes, analyses, policies, targets,
  or artifact formats without asking the core to predict their vocabulary.
- **Reproducible automation:** generated transformations become versioned,
  typed, inspectable modules with explicit authority and artifact provenance,
  rather than opaque scripts or prompts.

Each claim creates an experiment obligation. Agile co-design needs matched
extension and revision studies. Open specialization needs at least three
qualitatively different modules plus one non-neural case. Reproducible
automation needs chooser substitution, adversarial invalid proposals, rollback,
and replayable artifacts. Whole-model performance remains necessary to show the
substrate does not make specialization impractical, but it is not the definition
of the contribution.

Inference and emerging hardware remain the strongest evaluation domain because
they force all roles to interact. They do not define the system's applicability.
The paper therefore needs one non-neural extension micro-study to establish that
the core mechanism is not operator-specific, while reserving full performance
evaluation for inference where the implementation is mature enough to be fair.

## 1. The contribution is a new unit of modularity

Joggle's contribution is not a replacement for mature compiler abstractions.
The nearest systems establish the ingredients and make the remaining unit of
modularity precise:

- **MLIR: extensible IR components.** MLIR explicitly targets
  reusable, extensible infrastructure across abstraction levels, domains,
  targets, and execution environments. A smaller IR is not, by itself, a new
  systems idea. [MLIR, CGO 2021](https://research.google/pubs/mlir-scaling-compiler-infrastructure-for-domain-specific-computation/)
- **Relax: cross-level program composition.** Relax represents graph computation, loop-level
  tensor programs, and external calls together, with first-class symbolic shape
  tracking. “Joggle can mix abstraction levels” is therefore not a sufficient
  contribution. [Relax, ASPLOS 2025](https://arxiv.org/abs/2311.02103)
- **Exo: user-extensible target control.** Exo externalizes target instructions, memories,
  accelerator state, and scheduling policies into user code while checking
  transformation safety. “Users can register hardware behavior” is already an
  established design point. [Exo, PLDI 2022](https://people.csail.mit.edu/yuka/pdf/exo_pldi2022_full.pdf)
- **ACT/ATLAAS: generated accelerator backends.** ACT generates instruction selection and
  memory allocation from parameterized tensor-ISA descriptions; ATLAAS lifts
  RTL-derived semantics into such descriptions. Handwritten modules are not
  automatically more agile than generated backends.
  [ACT](https://act-compiler.github.io/assets/pdf/act-arxiv.pdf),
  [ATLAAS](https://arxiv.org/abs/2604.13523)
- **TileLang/Triton: concise high-performance kernels.** TileLang, Triton, Halide, and Exo already
  provide substantially more mature kernel-authoring surfaces than Joggle.
  [TileLang](https://arxiv.org/abs/2504.17577)
- **Mirage/Axon: automatic cross-level optimization.** Mirage and Axon jointly explore
  algebra, fusion, layout, scheduling, and instruction selection. A generic
  rewrite API is not comparable to their optimizers.
  [Mirage](https://arxiv.org/abs/2405.05751),
  [Axon](https://arxiv.org/abs/2606.26344)

The TVM lineage reinforces the opportunity. Halide/TE separated algorithm from
schedule; TensorIR made schedulable blocks explicit and inspectable; Relay added
program semantics; Relax restored cross-level composition and dynamic shapes.
Multiple abstractions were introduced because different information is useful at
different times, not because compiler designers failed to seek uniformity.

## 2. The systems problem: extensions are split by role

Most systems make one compiler role programmable. A research concept becomes a
*whole extension* when it spans several roles and those pieces must be installed,
validated, revised, and distributed together:

| System family | Boundary made programmable | What is deliberately outside that boundary |
| --- | --- | --- |
| MLIR / ONNX-MLIR | Dialects, conversions, passes, runtime hooks | The extension author coordinates the involved dialect and driver contracts |
| Relax / TensorIR | Graph–tensor–library composition and schedules | Target-specific semantics and artifact conventions remain separate extension concerns |
| Halide / TileLang / Triton | Kernel algorithm, dataflow, and scheduling | Whole-model import, fallback, deployment ABI, and arbitrary target evolution |
| Exo | Trusted user schedules, instructions, memories, and configuration | Whole-model semantic import and automatic backend construction |
| ACT / ATLAAS | Tensor-ISA or recovered RTL semantics | Model-level experimentation and user-controlled mixed implementation policies |
| Ladder / sparse edge stacks | A chosen datatype, encoding, mapping, and kernel path | A general mechanism for revising a different vertical experiment |
| Mirage / Axon | A fixed search/synthesis problem and target model | The surrounding deployment stack and evolving research interface |

These are powerful engineering choices. Joggle introduces an orthogonal unit:
the complete research concept whose implementation crosses them. Early
model–hardware co-design makes this need visible because the interface itself
remains an experimental variable.
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
Together, these systems establish both the value of specialized boundaries and
the recurring need to connect decisions across them.

We call the desired property **extension continuity**:

> Can a researcher revise a whole compiler extension while keeping its
> semantic fallback, structural implementation, target assumptions, selection
> policy, validation, and emitted artifact connected in one distributable unit?

The quantity of interest is not source brevity. It is continuity:
how much unrelated host machinery must change, how many contracts must be kept
in sync, and what previously working paths survive when the experiment changes.

Temporal extensibility is only one dimension of the proposed substrate. The
actual researcher-facing problem has five closures:

1. **Bootstrap closure:** the tool, dependencies, configuration, and time needed
   to reach the first inspectable program. Current TVM source installation
   requires a C++20 toolchain, CMake, LLVM, Python/FFI packaging, and selected
   optional SDKs; ONNX-MLIR requires a pinned LLVM/MLIR build and project-specific
   dependencies. These are justified costs of their ecosystems, but they matter
   for a research system and must be measured rather than described as
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

The contribution is therefore not “all compiler concepts are functions.” It is
a first-class whole-extension unit: the same typed module owns its public
vocabulary, executable compiler behavior, dependencies, safe IR edits,
implementation choices, and artifact actions, and preserves those relationships
as the module evolves.

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

The final Related Work should argue by the extension boundary each system makes
programmable, not by project chronology:

1. **Language and compiler construction:** JastAdd, Polyglot, Spoofax,
   Nanopass, Silver, xDSL. These establish modular syntax, semantics, passes,
   and lightweight IR construction; the question is whether one installed unit
   also remains continuous through target choice and artifacts.
2. **Multi-level compiler composition:** MLIR, ONNX-MLIR, Relay/Relax,
   TensorIR, IREE, Glow.
3. **Controllable tensor and kernel construction:** Lift/RISE/Elevate, Halide,
   Exo, Triton, TileLang, TACO, HeteroCL.
4. **Automatic transformation and synthesis:** Ansor, Roller, Welder, Mirage,
   Axon, ACT, ATLAAS, LLM-aided compilation.
5. **Representation/hardware co-design:** Ladder, SparseTIR, PULP-NN, DORY,
   MCUNet, VTA, sparse-MCU stacks.
6. **Deployment closure and reliability:** ONNX Runtime, LiteRT/TFLM, ncnn,
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

## 9. Frozen title and narrative

> **Joggle: Malleable Compilation with a Progressive Intermediate
> Representation**

The project owner froze this title on 2026-09-15. It names the system property
(malleable compilation) and mechanism (progressive intermediate representation)
without restricting the system to inference, edge devices, emerging hardware,
or agents. Those are motivating conditions and evaluation domains, not the
definition of Joggle.

The Motivation should follow this chain:

1. continuous specialization as the new operating condition;
2. TVM's six-generation evolution as evidence that each new demand adds useful
   representation boundaries and therefore lengthens the research path;
3. why broad compiler infrastructures and narrow kernel DSLs occupy two strong
   but different points in the generality--specialization tradeoff;
4. progressive IR as a way to add special detail locally while retaining one
   typed program and editing model;
5. typed modules as reproducible compiler configuration, including a compact
   interface that humans and generated procedures can both use safely; and
6. measured idea-to-artifact path, diagnostics, artifacts, and performance as
   evidence required to accept or reject the hypothesis.

This work used AI-assisted search and synthesis. Every citation and quantitative
claim must be checked against the linked primary source and the final artifact;
queued readings and agent-derived interpretations are not evidence.
