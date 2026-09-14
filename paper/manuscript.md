# Joggle: Malleable Compilation with a Progressive Intermediate Representation

## Abstract

Modern compilation research is optimized for execution time but increasingly
constrained by the time needed to express and evaluate a new compiler idea. AI
models acquire new
subgraphs, data representations, and stateful operators; machines acquire new
instructions and memories; and implementations may be supplied by experts,
search, synthesis, or agents. In a conventional multi-level stack, one such
experiment can cross a model IR, tensor program, schedule, target interface,
and artifact path. Partial lowering lets these forms coexist, but the researcher
still programs and maintains their boundaries.

Joggle introduces **progressive IR**, in which implementation detail is exposed
locally while program identity and the editing model remain stable. Abstract
calls, reusable tensor bodies, explicit loops, storage decisions, and external
implementations are states of one typed Fn/Blk/Op/Val program; a consumer
requests only the detail it cannot accept. Frontends, analyses, transformations,
selectors, and emitters are ordinary typed module functions over this program,
with checked resolution and transactional mutation. The result is a short,
inspectable path from a compiler idea to a reproducible artifact without fixing
one graph, kernel, or target vocabulary in the core. We evaluate that path using
controlled compiler changes, conventional models, complete artifacts, and
matched production baselines, while reporting generated-code performance as a
separate requirement rather than inferring it from usability.

## 1. Introduction

A new AI compiler idea is rarely confined to one kernel. Consider a grouped
low-precision projection for an edge processor. Evaluating it in a real model
requires recognition of the source computation, a portable meaning, packed
constants, loop and layout choices, storage, instruction selection, fallback,
and an application-callable artifact. A revision to group size or scratchpad
layout crosses all of them. The systems question is how quickly that revision
can become an inspectable, correct, and measurable program.

Current compilers answer changing requirements by introducing strong
representations at the points where new information becomes useful. The history
of [TVM](https://www.usenix.org/conference/osdi18/presentation/chen) is
instructive. NNVM separated graph optimization from tensor implementation;
Relay added functions, types, and control flow; TensorIR made scheduled tensor
blocks explicit; and [Relax](https://arxiv.org/abs/2311.02103) added symbolic
shapes, cross-level calls, and partial lowering. The resulting Relax--TensorIR
stack is substantially more capable than any earlier stage. Its cost is not
simply the number of IRs: a researcher must decide when a fact belongs to a
Relax function, TensorIR PrimFunc, schedule, runtime call, or registration path,
and must preserve the contracts among them as the experiment changes.

This cost is increasingly visible in AI systems because both sides of the
compiler boundary move. Model architectures partly converge around attention
and contractions yet continue to introduce local operators and fusion schemes.
[PluS](https://www.usenix.org/conference/atc25/presentation/wu-ruofan) identifies
the resulting lag between emerging graph optimizations and their support in
rule-based compilers, while template systems remain brittle to small model
changes. Hardware evolution simultaneously introduces low-precision formats,
sparse encodings, tensor instructions, asynchronous movement, and heterogeneous
memories. Systems such as [Ladder](https://www.usenix.org/conference/osdi24/presentation/wang-lei),
[Exo](https://doi.org/10.1145/3519939.3523446), and
[ACT](https://doi.org/10.1145/3839457) each make an important portion of this
space programmable. None of these observations makes their mechanisms obsolete;
together they show that the compiler's research interface changes at nearly the
same rate as its inputs and targets.

The producers of compiler decisions are also changing. Handwritten schedules
now coexist with search, synthesis, and generated kernels. Autocomp feeds
correctness and hardware measurements back into an LLM-driven tensor-accelerator
search, while [ARGUS](https://mast.stanford.edu/pubs/argus_agentic_gpu_optimization_guided_by_data_flow_invariants/)
shows that sparse pass/fail feedback is insufficient and instead exposes
structured counterexamples over data-flow invariants. This does not turn Joggle
into an agent framework. It raises a more general infrastructure requirement:
compiler decisions must be directly inspectable, bounded by typed interfaces,
rejected without corrupting program state, and retained as reproducible program
artifacts regardless of who proposed them.

Joggle addresses the time from such a proposal to a working artifact. Its key
idea is **progressive IR**. A program does not migrate wholesale from graph to
kernel to target representation. Instead, individual functions reveal detail
only as a consumer requires it: a source relation may remain a call, acquire a
reusable tensor body, expose scalar loops for structural optimization, or bind
an external implementation. Abstract and concrete regions coexist, but unlike
partial lowering across distinct IR kinds, every state uses the same typed
Fn/Blk/Op/Val structure and the same inspection and editing operations.

Progressive IR is paired with a uniform module language. Import, analysis,
rewriting, implementation selection, and emission are typed functions rather
than privileged host subsystems. A text module can carry new vocabulary,
portable behavior, transforms, target policy, and artifact actions under one
dependency and installation boundary. Human code, bounded search, synthesis,
or an agent can invoke the same functions; mutation commits only after complete
verification. This makes Joggle small and immediately usable without defining
the research space in advance.

The claim is not that specialized IRs are unnecessary or that a small compiler
inherits the optimization maturity of TVM, ONNX Runtime, or ONNX-MLIR. It is
that a progressive representation reduces **idea-to-artifact latency** when the
compiler boundary itself is under study, while retaining enough structure for
real optimization and deployment. Ease of use must therefore be measured by
clean setup, the visible steps and independent contracts in controlled changes,
intermediate inspectability, failed-edit recovery, and complete artifacts—not
by syntax screenshots or repository size alone. Execution latency, memory, code
size, and numerical correctness remain independent outcome measures.

This paper makes three contributions:

1. **Progressive IR**, a representation in which abstract relations, reusable
   tensor bodies, explicit loops, storage decisions, and external calls remain
   locally exposable states of one typed program rather than whole-program
   stages.
2. **A uniform compiler module mechanism** that lets independently installed
   source define vocabulary, analysis, transactional transformation, choice,
   and artifact construction through ordinary typed functions.
3. **An idea-to-artifact evaluation methodology** that measures setup and change
   paths, intermediate inspectability, failure recovery, deployment closure,
   and generated-code quality against matched native baselines.

## 2. Motivation: shorten the path from idea to artifact

### 2.1 AI systems require continuous specialization

AI software does not present a stable source language to a stable machine. A
model revision may add a normalization, attention variant, cache layout, sparse
encoding, or quantized subgraph. A machine revision may add an instruction,
memory scope, transfer mechanism, or alignment rule. Each change is local in the
design discussion but vertical in the compiler.

Consider a packed low-precision projection. Evaluating it in a real model needs
source recognition, a portable definition for unsupported cases, types for
scales and packed constants, structural access to loops, a storage policy, an
instruction or library binding, a selector, and an artifact ABI. Changing its
group size may revise every item. Calling it a custom operator hides everything
after recognition; calling it a kernel hides everything before invocation. The
research object is a specialization that remains connected from source meaning
to measured artifact.

This pattern is no longer exceptional. [PluS](https://www.usenix.org/conference/atc25/presentation/wu-ruofan)
observes that model families partly converge while local structures and graph
optimizations continue to change, leaving rule-based compilers behind recent
expert optimizations and template systems brittle to small structural changes.
[Ladder](https://www.usenix.org/conference/osdi24/presentation/wang-lei) shows the
same vertical coupling for evolving low-precision formats: storage, access,
conversion, and scheduling must change together before a nominally cheaper
datatype produces a faster program. These are instances of one systems problem:
the compiler must accept useful specialization before its abstractions and
libraries have stabilized.

### 2.2 New requirements have accumulated new IR boundaries

The evolution of TVM makes the design tradeoff concrete. NNVM provided a graph
IR but lacked types and control flow. Relay treated models as typed functional
programs. TE separated tensor computation from an external schedule, while
TensorIR made blocks and each scheduling step explicit and verifiable. Relax
then introduced first-class symbolic shapes and allowed Relax functions,
TensorIR functions, and external calls to coexist in one module through partial
lowering. Each generation repaired a real expressiveness or optimization limit;
the sequence is evidence for specialized abstractions, not a historical mistake.

It also exposes a growing user path. To change one vertical experiment, a
researcher may need to understand a frontend relation, Relax structure and
legalization, a TensorIR PrimFunc, schedule primitives, target intrinsics,
runtime calls, and the registration and build mechanisms connecting them.
Coexistence removes the need to lower the whole program at once, but it does not
remove the semantic boundary between those objects. MLIR and ONNX-MLIR provide
similarly capable boundaries through dialects, interfaces, conversions, passes,
accelerator hooks, and runtimes. These costs buy mature ecosystems and code
quality; the problem is that they recur inside the inner loop of a still-moving
research idea.

Our frozen baseline makes this distinction measurable rather than rhetorical.
The current TVM path uses a pinned source tree, native build, matching Python/FFI
environment, and different APIs for TensorIR construction, scheduling, external
code, and runtime invocation. ONNX-MLIR accelerator changes enter a pinned
LLVM/MLIR-based source build and multiple compiler hooks. A Joggle module is a
text directory loaded without rebuilding the host. The paper must report clean
setup time, commands, dependencies, independently edited contracts, and final
artifacts for identical tasks; a visual difference in syntax is suggestive but
not evidence by itself.

### 2.3 Generality and specialization should not be a choice

General compiler infrastructures can represent many programs and targets, but
their extension author must coordinate several expert interfaces. Specialized
kernel languages make one performance-critical boundary concise, but normally
begin after model recognition and end before complete deployment. The usual
choice is therefore broad reach with a long modification path, or a short path
through a predefined domain.

Joggle seeks a third point: a small general core in which specialization is
ordinary user program structure. Its core fixes only typed functions, blocks,
operations, values, structural types, modules, and safe edits. A module may add
a datatype, tensor definition, analysis, transform, target fact, selector, or
artifact action without introducing a corresponding host-side plugin class.
Modules can depend on, install, upgrade, and replace one another, so a successful
specialization becomes a reproducible compiler configuration rather than an
unrecorded patch to a checkout.

The representation counterpart is **progressive specialization**. An abstract
call and an explicit loop are not required to inhabit successive whole-program
IRs. A consumer exposes the body of only the relation it cannot accept; another
region may remain abstract or bind directly to an external implementation. The
program therefore acquires target detail locally while retaining portable
meaning elsewhere. Generality resides in the uncommitted core and portable
definitions; specialization resides in installable definitions and explicit
program edits. They coexist instead of being separate compiler modes.

### 2.4 A new population of compiler authors

Search and synthesis already generate schedules and implementations. Coding
agents broaden the population further, but do not remove the need for compiler
structure. [Autocomp](https://arxiv.org/abs/2505.18574) uses domain knowledge,
correctness checks, and hardware measurements in every iteration of an
LLM-driven accelerator optimization loop. [ARGUS](https://mast.stanford.edu/pubs/argus_agentic_gpu_optimization_guided_by_data_flow_invariants/)
finds sparse pass/fail feedback inadequate for coordinated GPU optimization and
instead returns counterexamples tied to threads, data, and program points.

The implication for Joggle is not an embedded chatbot. It is that a compiler
research interface should be learnable from compact text, expose the actual
program being changed, return typed and structural failures, and make a rejected
edit leave no residue. The same properties benefit a human learning the system.
A human, enumerator, synthesizer, or agent can write or invoke module functions;
none receives a privileged path around resolution, verification, transactions,
or artifact checks. Module installation also turns a successful generated
procedure into named, versioned, replayable source rather than a one-off trace.

This produces a testable secondary question: under a fixed specification and
documentation budget, can an unmodified coding agent complete representative
compiler changes more reliably and with fewer repair cycles in Joggle than
through native TVM or ONNX-MLIR paths? Such an experiment must freeze the model,
prompt, context, attempts, and oracle. Until measured, machine authorability is
a design pressure, not a claimed result.

### 2.5 Claim and measurable predictions

Joggle claims that progressive IR reduces **idea-to-artifact latency** for
compiler work that crosses abstraction boundaries. This predicts more than a
short language example. A new specialization should require no unrelated core
change; its abstract and exposed states should be directly inspectable; a
revision should preserve portable fallback and application ABI; invalid edits
should fail locally and atomically; and the resulting module and artifact should
rebuild from a clean installation.

The hypothesis fails if Joggle merely hides multiple representations behind
names, if generic functions defer errors caught earlier by specialized IRs, if
module composition requires central cases, or if usable code quality requires
moving each optimization back into the host. Section 5 therefore separates
change-path evidence from whole-model correctness, compile cost, latency,
workspace, and artifact size. Ease cannot excuse slow output, and performance
cannot excuse an irreproducible research path.

## 3. Progressive intermediate representation

### 3.1 One program, increasing implementation detail

Joggle programs contain seven public concepts: modules, functions, blocks,
operations, values, structural types, and open attributes. There is no separate
graph object, schedule tree, pass class, or target node in the host. Calls and
structured control flow are operations. Tensor shape and numeric format are
types. Source schema fields, target facts, and analysis results are attributes
only when they are properties of represented objects rather than hidden global
state.

Progressive IR changes a function body, not the identity of the program.
The running projection begins as a source call. A source module relates it to a
reusable tensor function after checking the source schema. The function may
remain abstract while graph-scale transformations operate on calls. A consumer
that rejects the abstract call requests more detail, causing the semantic body
to be instantiated as explicit loops and scalar operations. A target policy may
rewrite those loops or replace the call with a compatible external
implementation. At each state, ordinary IR inspection can observe what is
present and what remains opaque.

The invariant is not that all abstraction levels are equally convenient.
Rather, every exposed state remains a typed function program accepted by the
same verifier and edit API. Specialized modules may establish stronger local
contracts, but crossing that contract does not require moving the whole model
into another host representation.

### 3.2 Capability-directed exposure

A target is represented by an ordinary predicate over operations. Preparation
walks reachable functions and asks whether the target accepts each operation.
Accepted operations remain intact. A rejected call may select a compatible
implementation or expose its current body. The process reaches either a fixed
point accepted by the target or an explicit frontier containing the remaining
operations and their types.

This rule supports mixed implementation strategies. A mature matrix operation
may remain an external library call, a custom packed projection may select a
device module, and an uncommon elementwise computation may expose into portable
loops in the same artifact. The target does not require the model to undergo
one global lowering event before these choices can differ.

### 3.3 Modules are the extension boundary

A module is a versioned source package with typed functions, dependencies,
visibility, and an optional native codec or binding. Metadata may mark a
function for discovery, but it does not introduce a second object model. A
frontend decoder, type inference rule, analysis, transformation, selector,
memory planner, and emitter are invoked through the same typed call mechanism.

This uniformity is what allows general and specialized code to compose. A
target-independent access analysis can discover affine structure in an exposed
body. A device module can consume that result to choose a tile or instruction.
A different module can replace the chooser with search or synthesis without
changing the analysis, IR, or artifact contract. New source formats similarly
stop at their bridge module; shared tensor functions and targets do not acquire
frontend operation names.

### 3.4 Checked choice and transactional change

Alternative implementations are overloaded functions with structural types.
A selector receives the compatible set and may return at most one member. The
selection mechanism does not prescribe whether the choice is a static rule,
cost model, empirical search, solver, or agent. It does prescribe its authority:
the candidate must be type-compatible, and the resulting program must pass the
same verification and target capability checks.

Mutating module calls execute transactionally. Foreign or stale handles,
non-dominating values, invalid types, and failed verification abort the
invocation and restore the prior program. This property matters for generated
policies and multi-step structural transforms: a failed candidate does not
leave a partially rewritten module for the next candidate or emitter.

### 3.5 Artifact closure

An experiment is complete only when the chosen decisions reach an artifact.
Artifact modules therefore consume the same represented program and derive
external interfaces from exported functions. The C path derives source,
declarations, symbols, buffer shapes, byte counts, and immutable payloads from
one signature model. The application harness consumes the generated descriptor
rather than maintaining a second handwritten ABI. A deterministic virtual
machine provides an independent execution path for semantic and transformation
tests.

Artifact closure exposes rather than hides missing target work. Portable scalar
C is a legal fallback, but its latency reveals absent tiling, packing,
vectorization, library selection, or profitability policy. Those mechanisms can
be added as target modules and evaluated on the same model path.

## 4. System

Joggle's host is a C++20 library with a textual module language and embedding
API. The host owns parsing, immutable structural types and attributes, IR
storage, verification, overload resolution, module loading, transactional
invocation, and deterministic printing. It contains no neural-operator
enumeration, schedule vocabulary, or target lowering table.

Bundled modules provide scalar and tensor semantics, ONNX and TFLite bridges,
structural loop and access analyses, memory planning, C emission, and virtual
machine execution. Optional native codecs decode protobuf and FlatBuffer bytes
into source calls without adding their names to the tensor or target modules.
The same separation applies to artifacts: target modules publish capability,
preparation, and emission functions instead of host subclasses.

Handles carry store identity and generation. Successful edits advance a store
revision; invalid or stale references are rejected. Read-only module functions
may opt into revision-scoped memoization, allowing repeated structural queries
without process-global analysis state. The implementation runs cleanly under
Linux and macOS CI, including address and undefined-behavior sanitizers; large
model artifacts are checksum-pinned outside the default repository.

## 5. Evaluation

The evaluation is organized around four questions. Every numerical result must
come from a clean revision, preserved model and input hashes, an explicit host
and thread contract, correctness validation, balanced trial order, and raw
samples. Unsupported, not run, and measured zero are distinct states.

### 5.1 RQ1: Does progressive IR survive real models?

The model study uses official or publisher-hosted ONNX and TFLite artifacts from
four workload families: mobile classification, detection, convolutional
backbones, and transformer vision. It records decoding, type inference,
semantic relation, exposure, target preparation, strict compilation, and oracle
execution as separate gates. This avoids calling a parsed model supported or an
inferred model executable.

The current artifact executes ten ONNX models through strict C and validates one
TFLite MobileNetV2 path against LiteRT. XCiT-Tiny contains 1,310 nodes and 1,333
initially unknown results; Joggle infers all of them and currently stops with
four `Expand` and three `Tile` calls in positional-embedding shape computation.
This is a transformer representation result, not a transformer execution claim.
The final study will report executable models and structural frontiers in
separate groups.

### 5.2 RQ2: How quickly can an idea reach a checked artifact?

The study first measures bootstrap closure from a pinned clean environment:
dependencies, downloaded and built bytes, wall time, peak disk, commands, and
the first inspectable model. These quantities explain researcher friction but
are not the main result; mature ecosystems legitimately spend more to provide
more capabilities.

The central experiment then implements one complete specialization containing source
recognition, portable semantics, a parameterized packed representation,
structural policy, external target primitive, selector, validation, and
artifact behavior. Joggle, TVM/Relax, and ONNX-MLIR/MLIR use their documented
native paths. After every implementation passes the same oracle, two revisions
that were withheld from the implementer change the primitive parameterization
and the scratchpad/packing contract.

The primary evidence is a full-width change table and aligned idea-to-artifact
path diagram. For each initial implementation and revision it records host and
extension files changed, independent contracts coordinated, central
registrations, build obligations, invalidated artifacts, preserved fallback,
diagnostic location, compile time, and final correctness. Lines of code remain
secondary and are never collapsed into an “ease” score. The static four-task
study is retained as supporting coverage, not promoted as the core novelty.

A secondary authorability study gives the same frozen tasks, documentation
budget, model, and oracle to an unmodified coding agent. It reports completion
rate, attempts, context consumed, build and repair cycles, rejected invalid
edits, and core changes for Joggle and the native baseline paths. This tests
whether the compact textual interface transfers to a new compiler author; it
does not substitute model output for a human usability study.

### 5.3 RQ3: Can target modules improve complete artifacts?

The kernel study contains 441 deterministic ONNX cases. A 25-column matrix
covers Add, Multiply, ReLU, SiLU, GELU, Softmax, ReduceMean, RMSNorm, and
LayerNorm. A 27-column matrix covers MatMul, MatMul+Add, MatMul+ReLU,
Softmax+MatMul, RMSNorm+MatMul, SiLU+MatMul, SwiGLU, and QKV projection.
Convolution uses a separate shape grammar for standard, pointwise, depthwise,
fused activation, and pooling cases. Every table cell is the same ratio:
baseline median latency divided by Joggle median latency. Row groups compare
one-thread ONNX Runtime, TVM, and ONNX-MLIR on identical inputs; LiteRT appears
only for an identical TFLite artifact.

Kernel matrices locate profitable shape regions; they do not replace model
results. The whole-model study keeps one model order across four aligned panels:
inference latency, peak RSS or planned workspace, compile/load time, and
artifact footprint. A compact adjacent table reports absolute values,
numerical error, and task metric. Mobile CNN, detection, ViT-class, and compact
language or attention models appear only when their complete paths execute.

The current general loop policy rewrites 54 affine-proved MobileNetV2 bodies and
reduces generated-C latency by 26.5% in a same-runner diagnostic. The result
remains 12.2x slower than one-thread ONNX Runtime. This identifies the next
required mechanisms: multi-level tiling, immutable-weight packing, vector and
alignment contracts, library or target-instruction selection, and a
traffic-aware fusion policy. Publication results require these mechanisms to be
selected through the public module interface and evaluated on more than one
operator or model.

### 5.4 RQ4: What does the flexibility cost?

We separate host compilation, module interpretation, analysis, exposure,
transformation, storage planning, source emission, native compilation, and
runtime loading. Model size and number of exposed operations are independent
axes. Ablations measure revision-scoped memoization, indexed structural
queries, and transactional rollback without presenting one Joggle variant as a
system baseline.

The paper reports peak compiler memory, artifact growth, failed-candidate cost,
and frontier diagnostics alongside time. Generative tests cover parsing,
module loading, serialized frontends, native ABI boundaries, and legal IR edit
sequences. This question determines whether a uniform extension mechanism is a
usable substrate or merely moves complexity into interpreted module code.

## 6. Related work

### Extensible compiler construction

[JastAdd](https://doi.org/10.1016/j.scico.2007.02.003) uses rewritable reference-attributed grammars to build modular
language extensions and pluggable analyses. [Polyglot](https://doi.org/10.1007/3-540-36579-6_11) scales Java-like language
extensions without duplicating the base compiler. [Spoofax](https://doi.org/10.1145/1869459.1869497) combines declarative syntax,
analysis, transformation, code generation, and deployable editor services in a
language workbench. [Nanopass](https://doi.org/10.1017/S0956796805005605) describes a compiler as typed transformations
between many small, related languages. [MAGIK](https://www.usenix.org/conference/dsl-97/incorporating-application-semantics-and-control-compilation) dynamically loads application-specific
semantics and transformations with direct access to compiler IR. [LMS](https://www.cs.purdue.edu/homes/rompf/papers/rompf-scala16.pdf) composes
staged DSL stacks through horizontal IR extensions and vertical transformations
that may introduce new intermediate languages. These systems make language
features and compilation behavior modular. Joggle takes their lesson into a
different lifecycle: the module also owns target choices and artifact actions,
and remains an installed dependency while those cross-role bindings evolve.

Recent work gives module semantics directly to IR. [Modules for Datalog IR](https://doi.org/10.1145/3689484.3690737)
provides statically typed, separately compiled modules, explicit required and
provided relations, partial linking, and bundles. This is the nearest formal
module comparison. Its modules organize Datalog relations and are linked before
ordinary compiler passes; Joggle modules additionally execute typed analyses
and transactional edits over live IR and construct external artifacts. The
comparison motivates formalizing Joggle's resolver and upgrade semantics rather
than treating package management as incidental tooling.

[xDSL](https://arxiv.org/abs/2311.07422) takes a complementary sidekick approach: it recreates MLIR-compatible SSA
infrastructure in Python and exchanges textual IR and declarative definitions
with the production ecosystem. The [MLIR Transform dialect](https://doi.org/10.1145/3696443.3708922) exposes precise,
composable compiler transformations, pre/postconditions, and search integration
as transform IR without requiring a custom pass for each policy. Both are close
comparisons for research accessibility and controllable compilation. Joggle's
question is whether one installed typed module can span vocabulary, analysis,
mutation, target choice, and artifact closure without introducing a new host
extension category for each role. A matched study must test whether that broader
boundary remains simpler and equally diagnosable, rather than assuming it from
the syntax.

[TVM's PackedFunc and runtime Module](https://tvm.apache.org/docs/arch/runtime.html) are the strongest counterexample to a uniform-call
claim: TVM deliberately uses the same type-erased callable for compiler passes,
frontend callbacks, compiled functions, device modules, and RPC. Joggle chooses
a different contract. Calls are structurally typed before execution, module
source may define both portable semantics and compiler actions, mutations are
transactional, and package upgrade checks bindings in reverse dependents. The
paper must evaluate the resulting safety and lifecycle benefits; merely using
one function syntax is insufficient.

### Multi-level composition and deployment

TVM's IR evolution does not show that successive abstractions should be
collapsed. TE made tensor computation schedulable; TensorIR made blocks and
schedule effects observable; Relay supplied functional program semantics; and
Relax lets graph functions, TensorIR functions, and external calls coexist with
symbolic shapes and partial lowering. MLIR addresses the broader infrastructure
problem through reusable dialects, interfaces, conversions, and passes.
ONNX-MLIR applies this design to ONNX-to-native compilation and exposes
accelerators through build integration, dialect/pass registration, conversion
hooks, and runtime components. IREE couples an MLIR compiler to a low-overhead
AOT runtime spanning server and edge targets.

Joggle does not contest the value of specialized invariants or staged lowering.
It studies a narrower lifecycle: an extension whose source meaning,
representation, target policy, and artifact contract are still changing
together. The comparison is therefore a controlled revision experiment, not an
IR-count or source-line contest. If MLIR/Relax preserve the same working paths
with comparable coordination cost, Joggle's central claim is unsupported.

### Controllable tensor and kernel construction

Lift, RISE, and Elevate make transformation strategy explicit in functional
languages. Halide and Tiramisu separate algorithms from schedules. TensorIR,
Triton, Hidet, and TileLang expose different combinations of blocks, tiles,
threads, data movement, and mapping. Exo goes further by externalizing target
instructions, specialized memories, configuration state, and trusted schedule
rewrites into user code. TACO, SparseTIR, DaCe, and HeteroCL make sparse formats,
data movement, custom types, or heterogeneous implementation choices explicit.

These systems provide richer scheduling and kernel construction than Joggle.
Joggle instead asks whether the kernel boundary can remain connected to model
recognition, portable fallback, policy substitution, validation, and artifact
closure while those pieces evolve. Kernel quality must still be evaluated
against these systems or their generated libraries on matched tasks.

### Automatic optimization and backend construction

Ansor searches tensor programs; Roller constrains shapes to hardware-friendly
tiles; Welder jointly schedules operators through a tile graph; Ladder couples
custom datatypes with storage, access, conversion, and scheduling. TASO, Tensat,
Glenside, Mirage, and Axon enlarge the transformation space through generated
substitutions, equality saturation, access-pattern rewriting, or multi-level
superoptimization. ACT generates instruction selection and scratchpad allocation
from tensor-ISA semantics, while ATLAAS lifts RTL behavior toward those
specifications.

Joggle contributes none of these search or synthesis algorithms. Its claimed
role is a checked control plane through which a handwritten rule, bounded
search, synthesizer, or generated policy can propose alternatives without
changing the surrounding execution and artifact path. The chooser-substitution
experiment tests this distinction directly.

### Hardware--representation co-design and edge inference

VTA, PULP-NN, DORY, MCUNet, vMCU, Ladder, and recent sparse-MCU systems show
that datatype, encoding, memory organization, kernels, ISA support, compiler
integration, and model choice can be one systems result. TensorFlow Lite Micro,
LiteRT, ncnn, MNN, ExecuTorch, and IREE establish stronger baselines for runtime
breadth, tuned kernels, memory planning, and deployable artifacts. FlashInfer
shows the same vertical pressure in modern attention: cache representation,
kernel templates, JIT specialization, and runtime scheduling interact.

Joggle uses edge inference as a demanding evaluation domain, not as a claim of
edge-only applicability. Mechanism generality is evaluated by whether portable
and specialized policies share the same extension path; implementation maturity
is separately evaluated by model coverage, correctness, latency, memory,
compile time, and artifact size.

### Generated policies and compiler reliability

LLM-aided compilation, TritorX, and other agentic optimizers generate mappings
or kernels but depend on compilers, linters, profilers, and correctness oracles
to admit results. NNSmith, HirGen, compiler bug studies, and metamorphic testing
show that broad operator support and complex transformation stacks create a
large semantic failure surface. Joggle's transactional edits and explicit
frontiers are relevant only if they reject invalid proposals early, preserve
working paths, and produce reproducible diagnostics; these properties are
measured rather than inferred from API design.

## 7. Discussion and limitations

Progressive IR is not a universal replacement for multi-level IR. A
specialized representation is preferable when its invariants enable analyses
or transformations that cannot be expressed economically over Joggle's public
structure. Dynamic shapes, stateful model execution, device concurrency, and
distributed placement are not yet mature. The current C target is transparent
but lacks the tuned kernels and vector machinery of production systems.

Joggle instead targets the phase in which a model or hardware mechanism is
still changing. Its success criterion is that researchers can package general
compiler logic and specialized machine decisions together, revise them without
central cases, and measure the resulting artifact. Broader deployment support
and competitive performance remain empirical obligations, not consequences of
the architecture.

## 8. Conclusion

Inference hardware research crosses model semantics, tensor computation,
storage, scheduling, target instructions, and artifacts. Joggle keeps these
decisions connected through progressive IR and a uniform typed module
boundary. This design admits portable and specialized policies, as well as
handwritten and generated optimizers, over the same verified program state. The
remaining challenge is equally explicit: target modules must convert that
freedom into competitive complete artifacts. The final evaluation measures
both sides of that claim.
