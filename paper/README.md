# Joggle

## Abstract

A heterogeneous-compiler feature is rarely local. Its semantics, analyses,
graph transformations, conversions, and artifact generation often use
different extension and invalidation mechanisms. Joggle instead represents
programs and compiler behavior on one typed graph substrate. Typed compiler
functions give these roles one language, call model, and value model;
graph-level *mods* define ownership and dependency boundaries; and an evaluator
records graph observations and effects before publishing updates
transactionally. This arrangement forms a progressive intermediate
representation: compiler functions refine verified mods inside a common graph
model. Stable handles, hierarchical revisions,
dependency indices, and cached execution plans then restrict re-execution to
stages whose recorded inputs may have changed. We evaluate this design with
held-out extension tasks, matched cross-system feature patches, controlled
edits over a pinned model corpus, and operator- and model-level artifact
measurements. The experiments connect one programmable surface to executable
completion, mod ownership to patch footprint, and dependency-selected work to
update cost.

## 1. Introduction

Compilers for heterogeneous systems coordinate decisions at several scales.
They define operator semantics, transform graphs, schedule loops and memory,
convert representations, and generate artifacts for processors and
accelerators. LLVM established the value of a shared typed representation for
analysis and transformation [@lattner2004llvm]; MLIR generalized this approach
to multiple abstraction levels [@lattner2021mlir]; Halide separated algorithms
from schedules [@ragankelley2012halide]; and TVM combined graph- and
operator-level optimization for diverse targets [@chen2018tvm]. Together they
make programs increasingly malleable, but the compiler's own extension model
remains divided among mechanisms with different syntax, ownership, and
execution rules.

Consider adding a numeric format or a target-specific operator family. Its
semantic definition may live in an operator registry, legality in an analysis,
selection in a pass, representation changes in conversions, and realization in
an emitter or runtime binding. Each mechanism is individually useful, yet the
extension is the conjunction of all of them. A developer must reconstruct the
cross-layer contract before making a local change. A code-generating agent faces
the same structural burden: a plausible edit can omit a registration path,
modify the wrong representation, or trigger a wider rebuild and rerun than the
change requires.

This fragmentation creates three coupled problems. First, compiler behavior is
expressed through several extension interfaces, so one logical feature cannot
be inspected or generated through one language. Second, ownership follows
source trees, registries, and IR layers instead of the feature's dependency
boundary, so its change surface grows with the compiler. Third, staged drivers
usually track whole pass results, so a small edit can repeat conversions and
analyses whose observations remain valid. These are programmability,
organization, and update-efficiency problems, respectively.

Figure 1 states the paper's argument as three vertical chains. Each column
connects a development problem to one Joggle mechanism and one measurable
outcome. This 3×3 mapping also aligns the evaluation with the design: held-out
extension tasks measure predictability and executable completion, paired
patches measure change footprint, and controlled edits measure update latency
and executed work.

<!-- FIGURE 1 PROMPT — A dense two-column 3×3 systems-paper argument map. The
columns are PROGRAMMABILITY, ORGANIZATION, and UPDATE; the rows are CHALLENGE,
JOGGLE DESIGN, and OUTCOME. Each column reads top to bottom with no cross-column
arrows. Programmability: fragmented Semantics/Analysis/Transform/Convert/Emit
mechanisms → Unified metaprogramming using `fn optimize(m: Mod)`, one language,
one call model, one value model, and `Ty Attr Mod Fn Op Val` → CONVENIENT with
`agent success ↑` and `tokens/tool calls ↓`. Organization: one feature scattered across hierarchical
IR, pass registry, build target, conversion, and backend → Graph-level mod
boundary with `mod quant`, `use tensor`, own/publish/version/change tabs, and an
app→quant→tensor use graph → CONTROLLABLE with `files Δ ↓` and `LoC Δ ↓`.
Update: local edit causing A→B→C→D rerun → Reactive re-execution over an
affected subgraph using revisions, dependency index `D/W`, and cached execution
plan → EFFICIENT with `p50 / p95 ↓` and `visited nodes ↓`. Use compact technical
glyphs, thin dark connectors, white background, restrained blue/teal/lavender,
and coral only for changed state. Use only the named Joggle constructs; omit
step numbers, numbered circles, red numeric labels, gradients, shadows, and
decorative people. -->

*Figure 1: Joggle maps three extension challenges to system mechanisms and
measurable outcomes.*

Joggle addresses them by making compiler behavior part of the program model. A
Joggle program and its compiler extensions use the same typed graph and the
same function language. Analyses, transformations, converters, and artifact
generators are ordinary typed *compiler functions*. A *mod* owns such functions
with its graph, native bindings, and explicit `use` dependencies. An evaluator
executes calls transactionally, observes the graph state they read, and records
the effects they publish.

We call the representation *progressive* because compilation proceeds through
typed refinements of one verified graph. A mod may retain semantic operations,
introduce lower-level
helpers, or replace selected definitions; each published intermediate remains
an inspectable Joggle mod. Progress is therefore chosen by typed functions and
explicit dependencies, not encoded as a mandatory global pipeline.

This design yields three contributions:

1. **Unified metaprogramming.** One language, call model, and value model cover
   semantic definitions, analyses, graph transformations, representation
   conversion, and artifact generation while retaining distinct read and write
   contracts.
2. **Mod-scoped composition.** Graph-level mod packages define ownership,
   publication, dependency, and change boundaries across compilation stages.
   This boundary is orthogonal to hierarchical program IR.
3. **Dependency-directed updates.** Revisions, dependency indices, and cached
   execution plans restrict re-execution to compiler calls and stages whose
   recorded observations overlap published edits, under transactional
   publication.

The evaluation assigns one evidence stream to each contribution. Held-out
extensions measure predictability and executable task success. Matched feature
patches measure repository footprint. Controlled edits over complete models
measure latency and executed work. Artifact quality and mechanism costs
complete the evidence chain while reporting compiler responsiveness and
generated-code quality separately.

## 2. Motivation and Design Requirements

### 2.1 Compiler Extension Workflow

Figure 2 summarizes where extension work enters a heterogeneous compilation
stack. Model formats carry structures and semantics into the compiler.
Operator-level transformations specialize individual computations;
graph-level transformations fuse, propagate, or quantize across operations;
system-level transformations plan ordering, storage, and communication. The
runtime then supplies execution and platform support before deployment to a
target. These levels cooperate, but they are commonly exposed through different
APIs and maintained by different developers.

<!-- FIGURE 2 PLAN — Single-column figure. Preserve the supplied overview
artwork and its existing layout. It shows model structure and formats above
offline operator-, graph-, and system-level compilation; an online runtime and
target devices below; model, pass, operator/type, and hardware customization on
the left; and design, optimization, and deployment roles on the right. Do not
regenerate or alter the figure. -->

*Figure 2: A compiler extension can span semantic definition, optimization,
runtime support, and target deployment.*

Consider adding a fused convolution, bias addition, and activation. The
extension defines the fused operator's semantics, checks shapes and uses,
replaces a matching subgraph, selects a target implementation, and emits an
artifact. These responsibilities span several stages in Figure 2. A later
change to the supported layout or numeric type must preserve agreement among
the legality test, transformation, and target implementation. Section 3 uses
this operator extension to make the interfaces and ownership boundary concrete.

Existing infrastructures offer rich extension points for these individual
roles. The difficulty addressed here is their composition: a complete feature
must connect declarations, transformations, target realization, and
invalidation even when those roles have separate interfaces and ownership
boundaries.

### 2.2 Development Friction

*Fragmented metaprogramming.* An operator schema describes admissible programs,
but a pass, converter, and emitter describe actions over those programs. When
each role uses a different registration and invocation model, neither a person
nor a synthesis tool can treat the feature as one typed program. The required
context includes framework conventions that are not present in the local
definition, increasing both implementation effort and the opportunity for an
incomplete extension.

*Diffuse ownership.* Hierarchical IRs organize operations inside regions,
blocks, and functions. That containment expresses program semantics; compiler
capabilities additionally need ownership and dependency boundaries. Capability
ownership is often reconstructed from directories, build targets, registries,
and pass ordering. A feature revision
can therefore touch files and subsystems outside its semantic boundary, making
review, replacement, and parallel development harder to control.

*Coarse update boundaries.* A conventional pipeline establishes correctness by
running an ordered sequence of stages over each new input. After a local graph
or policy edit, rerunning the affected suffix is safe but can be unnecessarily
broad: an analysis that did not observe the edited entity is recomputed, and a
later stage can be rerun even when an earlier stage publishes no relevant
effect. Materialized representation boundaries add further units to rebuild,
validate, and traverse.

The three costs reinforce one another. Fragmented interfaces spread a feature;
spread ownership enlarges its change surface; a larger change surface forces
coarser invalidation. Addressing only one layer leaves the other two as limits
on extension velocity.

### 2.3 Design Requirements

The preceding workflow gives three requirements for a malleable compiler.

**R1 — One typed extension surface.** Semantic definitions, analyses,
transformations, converters, and artifact generators must share one language,
call model, and value model. The surface preserves explicit effects: read-only
queries and mutating transformations use distinct publication rules within the
same syntax.

**R2 — Explicit capability composition.** A feature must have a named owner,
public and private functions, declared dependencies, and an independent
lifecycle. This graph-level package boundary is orthogonal to hierarchical
program containment, making a compiler capability installable, inspectable,
replaceable, and removable through its owner.

**R3 — Change-proportional execution.** Revisions and dependency indices must
validate the state a function actually observed and propagate only effects that
can reach a later observation. Cached execution plans must avoid repeated setup.
Updates remain transactional: records and plans become reusable only with the
verified graph from which they were derived.

These requirements determine Joggle's structure. Compiler functions provide
R1, mods provide R2, and the evaluator and graph runtime provide R3. The next
section develops these mechanisms in the order in which a call uses them.

## 3. Joggle Design

### 3.1 System Model

Joggle represents programs and compiler extensions with the same typed graph
substrate. A *subject mod* contains the program being inspected or changed,
whereas an *extension mod* contributes compiler functions. Both are ordinary
mods: they use the same graph representation, type system, dependency rules,
and loader. The distinction describes their role in one invocation, not two
different runtime objects.

The graph owned by a mod is

$$
G = (F, B, O, V, A),
$$

where $F$, $B$, $O$, and $V$ are functions, blocks, operations, and values, and
$A$ denotes attributes attached to these entities. Blocks order operations,
operations consume and produce values, and maintained def-use edges connect
each value to its users. Operator sets, input formats, and artifacts enter
through extension mods.

An environment $E$ loads the mods named by a program, resolves typed calls,
binds source or native implementations, and owns evaluator state. One
compiler-function call has the form

$$
E \vdash f(M, a) \Downarrow (r, D, W).
$$

Here, $M$ is the subject mod and $a$ contains explicit arguments. The call
returns $r$, records the observed graph state $D$, and publishes graph effects
$W$. Read-only calls have $W=\varnothing$. Mutating calls produce $W$ only
after the resulting graph passes verification.

Joggle calls this representation progressive because each successful compiler
function publishes another verified mod over the same entity model. A stage may
retain semantic operations while introducing lower-level helpers, and a later
stage may replace definitions it owns. Typed refinements express progress
within one graph model, and every published state remains inspectable.

The rest of the design follows the three requirements from Section 2. Section
3.2 realizes R1 with typed compiler functions. Section 3.3 realizes R2 with
mod-scoped composition. Sections 3.4 and 3.5 realize R3 through observed
dependencies and the graph runtime that validates them.

### 3.2 Compiler Functions

Joggle's metaprogramming interface is the typed compiler function. Operator
semantics, analyses, transformations, representation conversion, and artifact
generation use this single programmable surface for definition and invocation.

A compiler function accepts graph handles and ordinary values:

$$
f : (H_1, \ldots, H_n, A_1, \ldots, A_m) \rightarrow R.
$$

Each $H_i$ is a `Mod`, `Fn`, `Blk`, `Op`, or `Val` handle. Each $A_i$ is a
configuration or structural value, such as an integer, type, list, dictionary,
or `Attr`. The result $R$ may itself be a handle, an ordinary value, structured
data, text, or bytes. Handles retain graph ownership and lifetime; ordinary
values remain independent of graph storage.

Figure 3 follows the operator extension introduced in Section 2. The graph
computes $y=\max(\operatorname{Conv}(x,w)+b,0)$. A legality function checks
types, shapes, and intermediate uses; fusion replaces the matched operations
with one semantic operation; conversion selects its target form. These graph
states, $G_0$, $G_1$, and $G_2$, retain the same input/output contract. An
emitter reads $G_2$ and returns the artifact. The extension's mod owns all five
roles, which exchange graph handles and owned values through the same call
interface.

<!-- FIGURE 3 PROMPT — Original compact single-column scientific diagram,
3.35 inches wide and approximately 2.6 inches high. Top: one conv_ext mod
containing Semantics/Analysis/Transform/Convert/Emit columns, with
define/legal/fuse/lower/emit and contract/read/write/write/read beneath them;
use tensor and a common typed-functions/graph-handles/owned-values rail.
Bottom: three aligned graph states. G0: x,w→Conv; b→BiasAdd; Conv→BiasAdd→ReLU→y,
with a single-use match boundary. G1: x,w,b→ConvBiasReLU→y. G2:
x,w,b→target.conv_relu→y. Left arrows: fuse·verify and lower·verify.
An emit(read) arrow connects the G2 graph to an artifact glyph. Semantic
contract: y=max(Conv(x,w)+b,0). These are schematic operator and role names.
Thin charcoal connectors, white background, pale teal/blue/lavender;
coral only for the match boundary. Compact readable labels, no numeric callouts,
fake source code, cache-hit counts, or isolated output-type edits. -->

*Figure 3: A schematic fused-operator extension. One mod owns five compiler
roles. Fusion and conversion publish verified graphs; emission reads the
prepared graph and returns an artifact without changing it.*

Joggle resolves a call from its qualified name, visible mods, explicit generic
arguments, parameter types, and result context. The selected implementation
may be a source body, an intrinsic, or a native binding. Caller syntax and the
evaluator's result boundary remain identical across these implementations.

Uniform calls retain distinct effect contracts. A query evaluates against a
read-only mod and returns an `Attr`. A run applies one or more mutating
functions within a transaction. An artifact call evaluates read-only over a
prepared graph and returns text or bytes. Thus, `query`, `run`, and `emit`
share resolution and evaluation while preserving different publication rules.
Read-only execution rejects mutation, and a run publishes only a verified
graph.

Compiler functions also compose through ordinary calls. A fusion function can
invoke the legality analysis in Figure 3, and a conversion can query the fused
operator's semantic contract. These calls remain visible to type checking,
diagnostics, and dependency capture. The typed call graph therefore defines
composition directly.

This function model unifies how compiler behavior is declared, resolved, and
invoked. Mods then supply the namespace, visibility, and dependency boundaries
that organize these functions.

### 3.3 Mods

A mod is Joggle's unit of ownership and composition. We model it as

$$
\mathcal{M} = (n, U, F_{pub}, F_{local}, G, N).
$$

Here, $n$ is the mod name and $U$ is its declared `use` set. $F_{pub}$ and
$F_{local}$ are its public and local functions, $G$ is its graph, and $N$
contains optional native bindings. The loaded object is the semantic boundary;
its on-disk package is the distribution form.

The `use` declarations form a directed mod graph. Search roots determine where
a named mod can be found, while `use` edges select the dependencies that
participate in loading and resolution. Separating location from dependency
keeps neighboring installations outside the visible function set.

Loading validates the declared dependency closure before evaluation. The
loader rejects missing mods, cycles, declaration-name mismatches, duplicate
public signatures, unreadable fragments, and unresolved native bindings. It
then loads source fragments in deterministic order. For a fixed mod graph and
search-root order, this process yields a stable public function set.

Visibility keeps this public set small. A public function can be resolved by a
dependent mod, whereas a `local fn` remains inside its owner. Source fragments
may split a large implementation without creating new dependency or lifecycle
boundaries. A separate mod is appropriate when a capability needs an
independent public surface, dependency set, owner, or installation lifecycle.

Native code follows the same boundary. A body-less declaration names the typed
contract, and a native library owned by that mod supplies its implementation.
The binding is scoped to the owning mod and becomes available through the
declared dependency graph.

Lifecycle operations preserve the same closure. Installation stages a
candidate, loads and verifies its dependencies, and publishes it only after
validation succeeds. Upgrade additionally checks affected reverse
dependencies. A failed operation leaves the installed graph unchanged. These
rules make the mod the unit that evolves across installation and upgrade.

The mod graph is orthogonal to the containment structure inside a subject
program. Blocks and operations describe program structure; `use` edges
describe compiler capability. The former may change during transformation
without rewriting the latter. This separation lets one project compose
analysis, transformation, and artifact mods without embedding those ownership
decisions into its program IR.

Static mod dependencies state what a compiler extension may call. During
execution, the evaluator records which
functions and graph entities a call actually observes. Section 3.4 uses this
second, dynamic dependency graph to avoid re-executing unaffected work.

### 3.4 Incremental Evaluation

A local graph edit need not invalidate every compiler result. Joggle bases
reuse on observed state rather than on the subject mod's whole revision alone.
For a cached read-only call $c$, the evaluator stores

$$
C_c = (K_c, r_c, D_c),
$$

where $K_c$ identifies the environment, store, function, and arguments;
$r_c$ is the returned value; and $D_c$ contains the observations made while
computing it.

Observations follow the graph API used by the function. Reading one operation
records its identity, generation, and payload state. Reading one value also
records type, metadata, and def-use state. Traversing a function records the
relevant function revision or collection membership. Reading all operations,
the mod structure, or the `use` set records progressively broader state. The
evaluator therefore captures the narrowest observation that preserves the
semantics of each graph operation.

Range access records the structural revision together with only the returned
operations. A content edit outside the range therefore preserves the record,
whereas insertion, removal, or reordering invalidates its structural premise.

A cached query is reusable when its call key and every recorded observation
remain current:

$$
Reusable(c) = Same(K_c) \land
              \bigwedge_{d \in D_c} Current(d).
$$

On a hit, the evaluator returns $r_c$. On a miss, it evaluates the function in
read-only mode, captures a fresh $D_c$, and replaces the entry. The execution
report names the first invalid contract, such as an environment, structure,
function, operation, or value change. Generation checks distinguish an edited
entity from a new entity that later reuses the same slot.

Mutating pipelines require one further step. After a successful run, a reactive
schedule stores for each stage $s_i$ its call key $K_i$, observed inputs $D_i$,
and output scope $W_i$. The key names the environment, function, and arguments.
The output scope contains changed function identities and flags for structural
or mod-dependency changes. On the next run, a stage is selected when its key or
inputs are stale, or when an earlier selected stage may change something it
observed:

$$
\begin{aligned}
Selected_i ={}& \neg Current(K_i,D_i) \\
              & \lor \exists j<i:\ Selected_j \land Overlap(W_j,D_i).
\end{aligned}
$$

The second term is an *upstream* miss. The inputs recorded for $s_i$ may still
match at the start of a run, yet an earlier stage selected in the same run can
change them. Propagating recorded output scopes restricts downstream selection
to overlapping regions instead of forcing all later stages to execute.

```text
Algorithm 1: Reactive stage selection and execution

dirty_outputs <- empty
selected      <- empty

for each stage s[i] in schedule order:
    miss <- validate(s[i].key, s[i].inputs)
    if miss is none and overlaps(dirty_outputs, s[i].inputs):
        miss <- upstream
    if miss is not none:
        selected.add(s[i])
        dirty_outputs.add(s[i].outputs)

begin transaction
for each stage s[i] in selected:
    execute s[i]
    capture fresh inputs D[i] and outputs W[i]
verify graph
commit transaction
replace records for selected stages
rebase retained observations to the committed graph
publish all records for the next run
```

Dependency records are published only after all selected stages execute and
the final graph verifies. Failure rolls back graph mutations and revision
state; it also discards the tentative records. Consequently, the next run
cannot reuse dependencies derived from an unpublished graph.

For example, changing an operator's layout metadata invalidates a legality
query that read that metadata. A scheduled transformation that observed the
same property is selected directly. A later stage is selected when its inputs
overlap the transformation's recorded output scope. A query over a disjoint
function retains its result. Selection follows recorded observations and
effects, including their function and structural granularity.

Fine-grained observations reduce re-execution but add capture and validation
work. For $K$ stages, incremental latency is approximately

$$
\begin{aligned}
T_{select} &= \sum_{i=1}^{K} T_{validate}(K_i,D_i) + T_{propagate}, \\
T_{inc} &= T_{select}
        + \sum_{s_i \in Selected} T_{eval}(s_i) \\
        &\quad + T_{verify}(G') + T_{commit}.
\end{aligned}
$$

Reuse is beneficial when this cost is lower than executing the omitted stages.
Broad graph APIs remain correct but record broad dependencies; narrow APIs can
preserve results across unrelated edits. External mutable state must enter
through typed arguments or an environment change before reuse. Native bindings
exchange scalar or byte attributes and do not receive graph entities, so
graph-dependent work remains in tracked compiler functions. These rules define
the state over which Joggle can validate reuse.

Incremental evaluation therefore depends on more than a result cache. It
requires stable entity identity, revisions that reflect every published edit,
and transactions that align graph state with dependency state. The graph
runtime provides these mechanisms.

### 3.5 Graph Runtime

Each mod owns a store with separate slot arrays for functions, blocks,
operations, and values. Public graph entities are small handles

$$
h = (S, i, g),
$$

where $S$ identifies the store, $i$ selects a slot, and $g$ is that slot's
generation. A lookup succeeds only when the store matches and the slot is live
at generation $g$. Erasing an entity advances its generation, so a stale handle
cannot refer to a later entity that reuses the same slot.

The store maintains whole-mod, structural, and function revisions. Any
observable edit advances the whole revision. Membership, order, and topology
changes also advance the structural revision, while function-local changes
advance the owning function's revision. These counters serve as freshness
tokens. Their granularity lets a function-level
observation survive an unrelated edit elsewhere in the mod.

Graph relations are updated with the store. In particular, each value records
its users. Replacing a value can therefore walk the affected uses, update both
def-use lists, and touch the owning functions without scanning every operation.
Repeated affected-cone walks use epoch marks, avoiding a graph-sized bitmap
clear before each sparse traversal.

Mutations execute inside a transaction. Metadata edits journal the previous
leaf value. Structural edits acquire a store snapshot lazily, when the first
such edit occurs. After evaluation, the verifier checks ownership, liveness,
dominance, calls, block arguments, return types, and structured control flow.
The publication rule is

$$
Publish(f,G) =
\begin{cases}
(G', \mathrm{ok}) & \text{if } f:G \Downarrow G' \land Verify(G'),\\
(G, \mathrm{fail}) & \text{otherwise.}
\end{cases}
$$

Verification therefore prevents a locally valid edit from exposing a globally
invalid graph. Success commits the graph and its revisions; failure restores
both.

The evaluator uses predecoded plans for checked compiler functions. A plan maps
parameters, block arguments, and operation results to dense slots;
it also records block targets, operation kinds, operand indices, and call-site
information. Its cache key is

$$
PlanKey = (S, id(fn), generation(fn), revision(fn)).
$$

Changing a function body invalidates its plan, while calls to the same version
reuse decoding and slot layout. Stable call sites additionally cache overload
resolution under the current environment epoch and argument types. Reusable
register windows reduce per-call allocation.

These plans are an internal interpreted execution form. They remove repeated
decoding and slot-allocation setup while preserving the language's control
flow, failure propagation, and dynamic calls. A native implementation may
accelerate a specific typed function through the same mod-scoped binding and
graph API.

At the extension boundary, typed handles represent live graph identity and
`Attr` represents owned, serializable values. Dependency records, execution
plans, transaction journals, and counters remain private. This boundary keeps
runtime machinery out of the extension API while allowing reports and profiles
to evolve as structured data.

Together, stable handles, hierarchical revisions, transactional mutation, and
versioned execution plans make the higher-level design operational. Compiler
functions can manipulate live graph entities, mods can evolve independently,
and the evaluator can reuse work while preserving graph and dependency
publication boundaries.

## 4. Evaluation

### 4.1 Methodology

The evaluation maps each mechanism in Figure 1 to an independently observable
effect. Table 1 summarizes the subjects, controls, and primary measurements.
Only outputs that pass the relevant correctness oracle enter an aggregate.

| Property | Comparison | Primary evidence |
| --- | --- | --- |
| Convenient | 3 systems; 24 tasks | completion, tokens |
| Controllable | 3 systems; 12 patches | files, lines, zones |
| Efficient | 3 systems; 15 models | latency, visited work |
| End-to-end | 2 Joggle paths + ORT | correctness, latency |

*Table 1: Evaluation matrix. Every comparison fixes revisions, inputs, and its
correctness oracle before measurement.*

**Subjects and controls.** The first three studies compare Joggle with MLIR, a
mature multi-level compiler infrastructure, and xDSL, a Python-native SSA
framework. Each extension follows the system's documented path from a pinned
revision. The update study imports one typed topology from each of 15
SHA-256-pinned ONNX models and applies the same edit and five-stage compiler
task in every system. Its primary ratios are formed against that system's own
full rerun and measure the fraction of rebuild cost retained after an edit.
Absolute latency reports the combined cost of the runtime and selected work. End-to-end
execution compares byte-identical inputs and numerical outputs with a pinned,
single-thread ONNX Runtime CPU reference.

**Correctness and measurement.** Compiler-extension tasks use build-and-test
oracles; graph transformations use verification and canonical structural
digests; artifacts use reference tensors with dtype-specific tolerances. A
failed oracle remains visible in coverage results but never contributes a
latency or speedup. Warm-path latency cases run ten warm-ups followed by 100
measurements in a seeded random order. We report medians and 95th percentiles per subject;
confidence intervals resample the independent unit---task, model, or
operator---rather than repeated timings. Ratios are formed within a subject before
geometric aggregation. CSV rows record subject identity, seed, and correctness;
hash-bound run records pin revisions, build flags, host and CPU policy, and
cache configuration.

### 4.2 Agent Extension Completion

The extension suite contains 24 held-out tasks, four in each of six families:
type or operation definition, analysis, rewrite, conversion, artifact
generation, and a vertical feature combining these roles. Every task has one
semantic specification, fixed positive and negative fixtures, a
system-specific harness, and an idiomatic passing reference patch.

Two frozen small code models drive the same deterministic coding-agent
harness. For each system, the agent receives the semantic specification, a
compact native API card, an isolated workspace, and the same inspect, edit,
build, and test tools. It may take at most 30 actions and emit at most 32k
tokens. Each model--system--task condition runs ten paired seeds with zero or
two disjoint demonstrations, yielding 2,880 complete trajectories.

The primary endpoint is executable success within budget: the final workspace
must parse, type-check, build, and pass the semantic oracle without manual
repair. We macro-average success over tasks and resample tasks within each
family. For successful trajectories, secondary measures are completion tokens,
tool calls, edit attempts, and wall time. Failed trajectories retain their
first terminal phase---parse, type, build, semantic oracle, or budget. As a
supplementary interface-predictability diagnostic, we also report the
perplexity of each passing reference solution.

<!-- FIGURE 4 PLAN — Full-width, three compact panels fed by one CSV and one
plotting script. (a) task-macro agent success at two demonstrations, grouped by
family; (b) paired success change from zero to two demonstrations; (c) tokens
and tool calls among successful trajectories. Keep the two models in separate
compact rows. Failure composition and reference-solution log-perplexity belong
in supplementary figures. CSV: figure-04-extension.csv. Raw columns: model,
model_revision,system,system_revision,task,family,demo_count,demo_ids,run,seed,
budget_actions,budget_tokens,wall_ms,prompt_tokens,completion_tokens,tool_calls,
edit_attempts,files_touched,parsed,typed,built,passed,stop_reason,
task_spec_sha256,api_card_sha256,trajectory_sha256,patch_sha256,reference_nll,
reference_tokens. -->

### 4.3 Change Footprint and Ownership

The footprint study reuses 12 tasks from the extension suite, two from each
family. Selection is fixed before patch metrics are collected. Each of the 36
implementations begins from a clean pinned snapshot, passes the common oracle,
and is reduced to a hunk-level fixed point.

For patch $p$, the footprint is

$$
Footprint(p)=(F_p,L_p,Z_p,R_p),
$$

where $F_p$ counts touched implementation files, $L_p$ counts added plus
deleted implementation lines, $Z_p$ counts ownership zones, and $R_p$ counts
changed build, registry, or pipeline declaration lines under a frozen policy.
Test changes are reported in parallel. A zone is a source package or build
target with one public responsibility.

Every task reports all four coordinates, build and oracle status, dependency
fan-out, and crossed ownership edges; in Joggle, these are declared `use`
edges. Because a baseline may require zero registry or
build edits, absolute paired counts are primary. We summarize the paired
difference with a task-level bootstrap interval and report a ratio only when
both counts are nonzero. Small rewrites and vertical features remain separate.

<!-- FIGURE 5 PLAN — One-column dense paired-dot plot fed by one CSV and one
plotting script. Rows are the 12 feature changes grouped by family; four narrow
columns show touched source files, changed source lines, ownership zones, and
registry/build edits in a single-column 2-by-2 layout. Symmetric-log axes retain
true zero while labeling raw counts. Connect systems implementing the
same task and retain true zeros. CSV: figure-05-footprint.csv. Raw columns:
system,system_revision,task,family,patch_hash,source_files,source_added,
source_deleted,test_files,test_added,test_deleted,zones,registrations,fanout,
cross_zone_edges,oracle_passed. -->

### 4.4 Reactive Update Cost

The update experiment measures the interval from a committed edit to a verified
artifact. Each pinned ONNX subject is decoded once into a typed graph that
preserves operation kinds, values, types, and def--use edges. A matched adapter
for Joggle, MLIR, and xDSL then performs analysis, canonicalization, target
selection, storage planning, and deterministic artifact construction. These
are executable stages rather than graph annotations: every stage consumes its
predecessor's result, and the final artifact is checked against a canonical
digest and structural verifier.

A case changes an operation attribute or result type at a preselected early,
middle, or late site. For each edit, the harness also selects a control entity
outside the affected cone. Every system first executes the complete five-stage
path. After an edit, Joggle selects calls through its revision and dependency
index. MLIR and xDSL execute their native complete pass paths because their
public execution models do not retain dependencies at this granularity. Each
numerator is paired with an independent complete rerun from the same edited
input. The primary endpoint normalizes the edit path to that system's complete
rerun,

$$
UpdateRatio_s = \frac{T_{update,s}}{T_{full,s}},
\qquad
WorkRatio_s = \frac{V_{update,s}}{V_{full,s}},
$$

where $V$ counts graph entities visited by the five stages. Ratios are formed
within each edit case before aggregation. Attribute and type edits, and
affected and unrelated scopes, retain separate summaries so inexpensive
controls cannot obscure the cost of an affected update. Absolute
edit-to-artifact latency remains visible.

A second panel calibrates these update ratios against Joggle's production
lowering path. For the same models it reports the time spent in decoding,
fixed-shape entry specialization with inference and conversion, `c.prepare`,
scalar lowering, storage planning and placement, and C emission, together with
graph size, emitted bytes, and artifact correctness. Entry types come from the
same pinned workloads used by the end-to-end experiment. These full-path
measurements report the collector's process and materialization costs alongside
compiler work. Each generated artifact passes a C compilation check;
the end-to-end experiment checks its numerical behavior. The complete-rebuild
time is summed within each run before computing its median. Matched update
results are checked against their corresponding independent full reruns.

<!-- FIGURE 6 PLAN — Full-width, dense three-panel result. (a) Fifteen model
rows show per-system UpdateRatio for affected attribute edits; every system's
complete rerun is 1. Points are medians and segments show the interquartile
range across paired cases. (b) Aligned WorkRatio rows retain true zero and
ratios above one. (c) A numeric heatmap shows Joggle's production stages in
seconds; the total cell is the median of within-run sums. Type edits and
unrelated controls use separate appendix plots from the same script and CSV.
CSV: figure-06-update.csv. Raw columns:
path,system,system_revision,subject,subject_hash,total_ops,affected_ops,
edit_class,edit_scope,edit_site,policy,stage,iteration,wall_ns,visited_ops,
executed_stages,total_stages,artifact_bytes,output_digest,correct,seed. -->

### 4.5 End-to-End Performance

The final experiment quantifies the performance and coverage of code emitted
through the programmable infrastructure. One matrix contains 24 fixed operator graphs---four each for
elementwise chains, reductions, matrix multiplication, convolution,
quantization, and fusion---and the same 15 model subjects. We compare Joggle's
required lowering path, the same path plus its frozen optimization pack, and a
pinned single-thread ONNX Runtime CPU reference. All variants consume
byte-identical inputs and pass dtype-specific numerical oracles. Joggle fixes
each entry signature from those inputs before either lowering pipeline begins.

The main measure is steady-state execution latency after ten warm-ups and 100
measurements. Figure 7 shows every operator and model individually. A point
marks its median latency divided by the ONNX Runtime median; a line extends
to its 95th-percentile latency under the same denominator. The line describes
timing variation, not a confidence interval. Values below one indicate faster
execution. Unsuccessful pairs retain their row and contribute to the coverage
table. Operator and model results share one figure and one CSV, with up to
11,700 timed rows.

For the pinned operator configuration in Appendix A, all 24 operators pass the
numerical oracle in both Joggle paths. The optimization pack reduces geometric
mean latency by 1.81× relative to the base path. Relative to ONNX Runtime, the
base and optimized latency ratios are 9.08× and 5.02×, respectively; the
optimized path is faster on three operators. The effect varies across
operators: the 256×256 matrix multiply improves by 12.83× over the base path,
whereas strided convolution regresses from 146.82× to 164.31× the reference
latency. These per-operator differences locate the remaining generated-code
costs and distinguish the effect of an optimization pack from compiler update
responsiveness.

<!-- FIGURE 7 PLAN — One full-width performance figure fed by one CSV and one
plotting script. Left: all 24 operators grouped by six families. Right top:
all 15 models. Each subject has base/optimized median points and median-to-p95
segments, normalized by its ONNX Runtime median. Both panels share limits;
no subject is compressed into a family mean. Right bottom: correct-coverage
counts for both Joggle variants and ONNX Runtime. Failed cases use × in a
non-data margin; missing measurements use ?. CSV: figure-07-performance.csv. Columns:
subject_kind,subject,subject_hash,family,system,system_revision,variant,
supported,reason,iteration,calls_per_sample,latency_ns,max_abs_error,
max_rel_error,input_digest,output_digest,correct,seed. -->

Together, generation characterizes the extension surface, patch footprint its
ownership boundary, the cross-system update study its responsiveness, and the
combined operator/model matrix its generated performance.

## 5. Related Work

Table 2 aligns related mechanisms with Joggle's three design axes. **Roles**
covers the five compiler roles through one programmable surface. **Owner** and
**Deps** capture capability organization.

The final four columns cover reactive updates.

Symbols: ✓ first-class; △ role-specific or coarser; × absent.

| System | Roles | Calls | Owner | Deps | Reads | Effects | Reactive | Atomic |
| --- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| LLVM [@lattner2004llvm] | × | △ | △ | △ | △ | △ | × | × |
| Nanopass [@keep2013nanopass] | × | ✓ | △ | × | × | △ | × | × |
| MLIR [@lattner2021mlir; @mlirpass] | △ | △ | ✓ | △ | △ | △ | × | × |
| MLIR Transform [@lucke2025transform] | × | ✓ | ✓ | △ | △ | ✓ | × | × |
| xDSL [@fehr2025xdsl] | △ | △ | ✓ | △ | × | × | × | × |
| Halide [@ragankelley2012halide] | × | ✓ | ✓ | △ | × | × | × | × |
| TVM [@chen2018tvm] | △ | △ | △ | △ | × | × | × | × |
| egg [@willsey2021egg] | × | ✓ | △ | × | △ | ✓ | △ | × |
| Adapton [@hammer2014adapton] | × | ✓ | △ | ✓ | ✓ | ✓ | ✓ | △ |
| Build systems [@mokhov2018build] | × | △ | ✓ | ✓ | △ | ✓ | ✓ | △ |
| **Joggle** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

*Table 2: Coverage of unified extension, ownership, and reactive-update
mechanisms. Columns follow the three design axes in Figure 1.*

### 5.1 Extensible Compiler Infrastructures

LLVM established a reusable typed SSA infrastructure with shared analyses and
passes [@lattner2004llvm]. MLIR extends this model with dialects, nested
operations, declarative operation definitions, conversions, and pass pipelines
across abstraction levels [@lattner2021mlir]. Its pass manager caches analyses
at operation anchors and uses explicitly preserved analyses to control
invalidation [@mlirpass]. xDSL retains compatibility with MLIR's SSA structure
while moving compiler construction and rapid prototyping into Python
[@fehr2025xdsl]. These systems demonstrate the value of common IR
infrastructure and extensible operation sets.

Joggle centers the typed compiler function together with the graph-level mod
that owns and publishes it. Semantics, analysis,
mutation, conversion, and artifact generation retain their distinct effects
but share one declaration, resolution, and invocation model. The mod graph is
orthogonal to blocks and operations, so capability ownership follows declared
dependencies independently of program containment.

### 5.2 Tensor Compilers

Halide separates an image-processing algorithm from its schedule, allowing
target-specific choices without changing functional meaning
[@ragankelley2012halide]. TVM combines graph-level optimization, tensor
programs, schedule search, and target code generation
[@chen2018tvm]. Multi-level tensor compilers similarly use progressively lower
representations to expose decisions at the operator, loop, memory, and target
levels. Joggle supplies the programmable and organizational substrate for
declaring, composing, converting, and executing these algorithms.

This distinction separates two performance dimensions. Tensor compilers
primarily optimize generated code. Joggle also reduces the compiler
work repeated after a transformation, policy, or input changes. Section 4
measures generated artifacts and edit-to-result latency independently.

### 5.3 Programmable Transformations

The Nanopass framework derives representation checks and traversal support from
explicit source and target languages, encouraging many small passes
[@keep2013nanopass]. MLIR's Transform dialect represents fine-grained
transformation schedules as IR, maps handles into a separate payload IR, and
uses effect interfaces to constrain transform execution
[@lucke2025transform]. Rewrite systems make local rules concise and composable;
`egg` combines equality saturation, rebuilding, and e-class analyses to manage
many equivalent programs efficiently [@willsey2021egg]. These systems make
transformation intent programmable at complementary granularities.

Joggle extends the programmable unit beyond transformation control. A compiler
function may define semantics, query types, traverse control flow, invoke a
native solver, rewrite a graph, convert a representation, or return an
artifact. Typed calls compose these roles; mods own and publish them; observed
reads and effects connect them to reactive execution. This boundary complements
specialized transformation languages and rewrite engines by organizing the
compiler work that surrounds them.

### 5.4 Incremental Computation

Self-adjusting systems record dependencies and repair demanded computation
after an input changes. Adapton formalizes this model with a demanded
computation graph [@hammer2014adapton]. Build
systems apply related ideas to tasks and artifacts; Build Systems à la Carte
separates dependency discovery, scheduling, and rebuilding policies
[@mokhov2018build]. Compiler pass managers also cache analyses and preserve
them across transformations when their contracts allow it [@mlirpass].

Joggle specializes dependency tracking to mutable compiler graphs. A cached
call records typed graph observations, while a mutating stage also records the
scope it changed. Revisions and generations validate identity and state;
upstream effect overlap selects later stages; and a transaction publishes the
graph and fresh dependency records together. This combination connects
fine-grained graph invalidation with an ordered, effectful compiler pipeline.

## 6. Discussion

**Progressive structure.** Joggle programs retain functions, blocks,
operations, values, and def-use relations. These structures express program
semantics and support conventional local reasoning. Progression changes how
stages relate: compiler functions refine a verified graph without requiring a
global ladder of public IR classes. High-level operations and lower-level
helpers may therefore coexist when a stage needs both.

**A mod is a capability boundary.** Directories split files, and hierarchical
IR splits programs; neither necessarily identifies the owner of a compiler
feature. A mod binds a public typed surface to dependencies, source fragments,
native bindings, and lifecycle operations. Small implementations may remain in
one source file. Separate mods become useful when capabilities require distinct
owners, dependency closures, or release cycles.

**Uniform calls preserve effects.** Uniform syntax coexists with explicit
effect contracts. Query execution is read-only, runs publish a verified graph,
and artifact calls return owned values. Native implementations use the same
signatures and ownership boundary. Declarations, resolution, calls, and
composition are uniform; effect checks remain explicit.

**Precision has a cost.** A compiler function gains reuse by reading the
narrowest state needed for its result. Whole-graph traversal correctly creates
a broad dependency; range and single-entity access create narrow ones. Narrow
records add capture and validation work, so they help only when avoided
execution is more expensive. Miss reasons, observation counts, and executed
stage counts expose this trade-off directly.

**Publication bounds reuse.** Dependency records are valid only for the graph
that produced them. Joggle therefore commits mutations, revisions, and new
records after final verification, or restores the previous state and discards
tentative records. State outside the graph must enter through typed arguments
or an environment revision. A later run can then reuse only observations tied
to a published graph and environment.

**Interpreted plans.** Predecoded plans cache checked
interpreter work: dense value slots, control-flow targets, operand indices, and
call-site information. They remove repeated setup while preserving interpreted
execution. Typed mod bindings provide native acceleration for selected
functions through the same call boundary.

## 7. Conclusion

Joggle treats compiler extension as typed computation over a shared graph.
Compiler functions provide one surface for semantics, analysis,
transformation, conversion, and artifact generation. Mods make capabilities
explicit units of ownership and composition alongside hierarchical program IR.
Revisions, observed dependencies, effect propagation, transactions, and cached
execution plans make repeated compilation change-proportional. Together, these
mechanisms connect three properties that compiler infrastructures usually
expose separately: a predictable way to write an extension, a controlled
boundary in which it evolves, and selective work after it changes. The
evaluation tests these properties through executable extension completion,
paired patch footprint, and reactive update cost while measuring generated
artifacts independently.

## Appendix A. Operator Measurements

| Operator | ORT (µs) | Base / ORT | Opt / ORT |
| --- | ---: | ---: | ---: |
| conv-depthwise | 39.95 | 3.81 | 4.66 |
| conv-pointwise | 60.54 | 152.08 | 19.34 |
| conv-stem | 178.33 | 4.28 | 8.58 |
| conv-strided | 43.33 | 146.82 | 164.31 |
| ew-affine-1k | 2.87 | 0.09 | 0.08 |
| ew-broadcast-relu | 3.53 | 0.79 | 0.79 |
| ew-chain-64k | 21.87 | 4.42 | 3.16 |
| ew-select-16k | 8.13 | 1.27 | 1.24 |
| fuse-add-relu | 13.10 | 2.71 | 4.98 |
| fuse-conv-bias-relu | 307.00 | 86.18 | 41.68 |
| fuse-matmul-bias-relu | 14.52 | 156.55 | 13.00 |
| fuse-mul-add | 20.28 | 2.26 | 2.30 |
| mm-batched | 11.25 | 65.84 | 66.53 |
| mm-rectangular | 13.26 | 207.14 | 9.88 |
| mm-square-256 | 30.98 | 332.02 | 25.87 |
| mm-square-64 | 4.40 | 20.77 | 1.72 |
| quant-conv | 26.27 | 9.10 | 10.02 |
| quant-dynamic | 4.86 | 1.40 | 1.46 |
| quant-matmul | 6.26 | 20.12 | 2.36 |
| quant-qdq-tensor | 3.33 | 0.58 | 0.55 |
| red-l2-last | 15.23 | 1.11 | 1.09 |
| red-max-channel | 58.95 | 9.89 | 10.12 |
| red-mean-spatial | 11.99 | 9.27 | 9.51 |
| red-sum-row | 9.53 | 5.60 | 5.60 |
| Geometric mean ratio | — | 9.08 | 5.02 |
| Correct operators | 24/24 | 24/24 | 24/24 |

*Table A1: Operator execution measurements. Joggle revision `83aa8d4fc72d`,
Apple Clang 17.0.0 (`-O3 -DNDEBUG`), and ONNX Runtime 1.26.0 CPU with one thread
and full graph optimization. Each entry uses ten warm-ups and 100 measured
samples. Ratios divide unrounded per-operator medians; values below one
indicate lower latency than ORT. All 72 subject/variant pairs pass the
numerical oracle. The geometric mean covers all 24 operators.*
