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
completion, mod ownership to patch footprint, and affected graph scope to
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
`PPL ↓` and `pass@k ↑`. Organization: one feature scattered across hierarchical
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
   execution plans restrict re-execution to affected graph regions and stages
   under transactional publication.

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

As a running example, consider introducing saturating arithmetic for a
low-precision model. The feature begins with a parametric type and arithmetic
semantics. It then needs a capability test, a transformation that selects the
new operation, a storage policy, and one or more target implementations. The
change is conceptually one feature, but its pieces occupy several stages in
Figure 2. Later revisions—such as admitting another width or changing target
selection—must preserve the same cross-stage agreement.

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

Figure 3 follows a common operator extension. A convolution, bias addition, and
activation form a fusible subgraph. One mod owns the fused operator's semantic
contract together with typed functions for legality analysis, fusion, target
conversion, and artifact generation. The functions exchange graph handles and
owned values through the interface above; each role retains its own effect
contract.

<!-- FIGURE 3 PROMPT — Dense single-column operator example, 0.92 ACM column
width, with three tightly aligned horizontal bands and no source-code listing.
Band 1, INPUT GRAPH: small dataflow graph `x,w → Conv → BiasAdd → ReLU → y`;
show tensor types under edges and a dashed rounded enclosure around the three
fusible operations. Band 2, ONE MOD / ONE CALL MODEL: five compact colored
function chips `define`, `legal`, `fuse`, `convert`, `emit`, all connected to
the same typed handles `Mod Fn Op Val`; show the transformed path
`ConvBiasReLU → target.conv_relu → artifact`. Band 3, REACTIVE RECORD: a local
edit `Δ y.type` points to a four-row ledger with columns stage, observed D,
effects W, decision; `legal` is DIRECT, `fuse` is UPSTREAM, an unrelated
analysis and `emit` are REUSE. Add a small transaction bracket spanning
execute→verify→commit. Use white background, thin charcoal strokes,
blue/teal/lavender role colors, coral only for the edit and selected stages,
short labels, operator/dataflow glyphs, no decorative people, no numbered
circles, no red numeric annotations, no imitation of the workflow figure's
layout. -->

*Figure 3: One operator extension uses a shared call model across roles and
records the graph state that governs re-execution.*

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

The ledger in Figure 3 makes this selection concrete. A type edit directly
invalidates the legality stage that observed it. The fusion stage follows when
its recorded input overlaps the earlier effect, while stages with disjoint
observations retain their previous results.

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

| Mechanism | Subjects, controls, and measurements |
| --- | --- |
| Unified language | 24 Joggle/MLIR/xDSL tasks; pass@$k$ and log-PPL |
| Mods | 12 reused tasks; files, lines, zones, and fan-out |
| Reactive execution | 15 models, generated graphs, four policies; latency, work, and reuse |
| Artifact path | operator and accepted-model suites; latency, memory, and size |

*Table 1: Evaluation matrix. All systems use pinned revisions and a shared
semantic oracle.*

**Subjects and controls.** MLIR provides a mature multi-level compiler
baseline; xDSL provides a Python-native SSA framework. Each task follows the
system's documented extension path from a pinned revision. The model corpus
contains 15 SHA-256-pinned ONNX models spanning classification, detection,
quantized networks, and vision transformers. Results retain every compatibility
outcome; aggregates over model execution use the accepted intersection.
Generated graphs at $10^3$--$10^6$ operations vary total size, affected scope,
fan-out, and stage count independently.

**Correctness and measurement.** Compiler-extension tasks use build-and-test
oracles; graph transformations use verification and canonical structural
digests; artifacts use reference tensors with dtype-specific tolerances. A
failed oracle remains visible in coverage results but never contributes a
latency or speedup. Latency cases run ten warm-ups followed by 100 measurements
in a seeded random order. We report medians and 95th percentiles per subject;
confidence intervals resample the independent unit---task, graph, operator, or
model---rather than repeated timings. Ratios are formed within a subject before
geometric aggregation. Every raw row records revisions, build flags, host and
CPU policy, seed, cache state, and correctness outcome.

### 4.2 Extension Predictability and Completion

The extension suite contains 24 held-out tasks, four in each of six families:
type or operation definition, analysis, rewrite, conversion, artifact
generation, and a vertical feature combining these roles. Every task has one
semantic specification, a system-specific harness, and an idiomatic reference
solution that passes the common oracle. A separate demonstration bank uses
disjoint features, so no definition, rewrite, converter, or emitter from an
evaluation feature enters its prompt. Formatting is normalized; generated code
and harness boilerplate are not scored.

Two frozen open-weight code models in the 1--3B range receive the specification
and a compact API card. Demonstration counts are $0,1,2,$ and $4$; examples
come only from the demonstration bank through one deterministic retrieval rule
and an equal token budget. Each condition draws 50 samples. Sampling
parameters, seeds, stopping rules, and maximum continuation length are fixed
per model.

For reference tokens $x_{1:N}$ and context $c$,

$$
PPL(x\mid c)=\exp\left(-\frac{1}{N}
  \sum_{t=1}^{N}\log p_\theta(x_t\mid x_{<t},c)\right).
$$

Log-perplexity is a secondary measure. We compare semantically corresponding
reference solutions under the same model and tokenizer, and report both the
value at each demonstration count and the paired change from zero to four
demonstrations. Executable completion is primary: a sample
must parse, type-check, compile where required, and pass the oracle. From $n$
samples with $c$ successes, pass@$k$ is [@chen2021codex]

$$
\widehat{pass@k}=1-\frac{\binom{n-c}{k}}{\binom{n}{k}}.
$$

We compute pass@1, pass@5, and pass@10 for each task, macro-average across
tasks, and obtain intervals by resampling tasks within each family. Token counts
and parse, type, build, and oracle failures explain completion gaps without
replacing the executable result. Pairing fixes task semantics; the
demonstration sweep distinguishes prior familiarity from learnability from
local examples.

<!-- FIGURE 4 PLAN — Full-width, three compact panels fed by one CSV and one
plotting script. (a) task-level pass@1 at four demonstrations, grouped by
family; (b) pass@1 response to 0/1/2/4 demonstrations; (c) parse/type/build/
oracle failure composition. Keep the two models in separate rows. Show paired
log-perplexity only as a small secondary inset so syntax length cannot dominate
the headline. CSV: figure-04-extension.csv. Raw columns: model,
model_revision,system,system_revision,task,family,demo_count,seed,sample_index,
target_tokens,context_tokens,nll,parsed,typed,built,passed. -->

### 4.3 Change Footprint and Ownership

The footprint study reuses 12 tasks from the extension suite, two from each
family. Selection is fixed from the semantic specifications before any patch
metric is collected. Each implementation begins from a clean pinned snapshot
and ends after the common oracle passes. A deterministic delta-debugging pass
visits hunks in a fixed order and removes a hunk whenever the oracle continues
to pass. It repeats to a fixed point, yielding an auditable 1-minimal patch
under that hunk partition.

For patch $p$, the footprint is

$$
Footprint(p)=(F_p,L_p,Z_p,R_p),
$$

where $F_p$ counts touched implementation files, $L_p$ counts added plus
deleted implementation lines, $Z_p$ counts ownership zones, and $R_p$ counts
changed build, registry, or pipeline declaration lines under a frozen policy.
Test changes are reported in
parallel. Frozen rules exclude generated, vendored, lock, and formatter-only
changes; a zone is a source package or build target with one public
responsibility.

Every task reports all four coordinates, build and oracle status, dependency
fan-out, and crossed ownership edges; in Joggle, these are declared `use`
edges. Because a baseline may require zero registry or
build edits, absolute paired counts are primary. We summarize the paired
difference with a task-level bootstrap interval and report a ratio only when
both counts are nonzero. Small rewrites and vertical features remain separate.
The artifact includes patches, frozen zone maps, counting scripts, and every
inclusion decision.

<!-- FIGURE 5 PLAN — One-column dense paired-dot plot fed by one CSV and one
plotting script. Rows are the 12 feature changes grouped by family; four narrow
columns show touched source files, changed source lines, ownership zones, and
registry/build edits on aligned log1p axes. Connect systems implementing the
same task and retain true zeros. CSV: figure-05-footprint.csv. Raw columns:
system,system_revision,task,family,patch_hash,source_files,source_added,
source_deleted,test_files,test_added,test_deleted,zones,registrations,fanout,
cross_zone_edges,oracle_passed. -->

### 4.4 Reactive Update Cost

An update case begins with a verified optimized graph, applies one controlled
edit, restores the required compiler result, and checks its semantic digest.
The fixed pipeline performs analysis, canonicalization, target selection or
legalization, memory planning, and artifact preparation. Three edit classes
exercise distinct invalidation paths: no-op isolates scheduling overhead,
operation metadata changes operation state, and value-type changes value state.
Metadata and type edits target either the affected cone or an unrelated
same-function entity. Sites are chosen before timing at early, middle, and late
topological positions among editable entry-block computations. Each case
records the selected operation and its measured affected scope.

Four policies isolate the mechanisms. **Full** reruns every stage;
**Reactive** validates recorded inputs and propagates overlapping effects;
**Whole-mod** replaces entity
observations with one mod revision; and **No-plan-cache** retains reactive
selection but decodes functions again. All policies must produce the same
verified digest.

Every policy starts from the same fully initialized graph. A Full stage walks
the complete function, whereas a Reactive stage invokes the same property
computation through its recorded root and therefore visits the affected cone.
This setup holds the required compiler result constant while separating global
re-execution from dependency-selected work.

The primary measure is edit-to-result latency. We also record selection,
evaluation, and verification time; executed and reused stages; observed
entities; compiler-function operations; plan hits; and miss reasons. For
policy $p$, speedup is $T_{Full}/T_p$ and work reuse is
$1-E_p/E_{Full}$, where $E$ is executed compiler-function operations.

The model corpus supplies realistic graph structures for the within-Joggle
mechanism study. Generated graphs separate $|G|$ from affected scope
$|\Delta G|$: one sweep fixes a one-operation edit while scaling $|G|$;
another fixes $|G|$ while increasing $|\Delta G|$. The cross-system track uses
the same neutral generated DAG, analysis, rewrite, edit, and output digest in
Joggle, MLIR, and xDSL. It therefore compares update policies without making
frontend coverage part of the result. Cold measurements include process
startup, parsing, and setup; warm measurements retain each system's public
graph and pass infrastructure.

<!-- FIGURE 6 PLAN — Main full-width data figure fed by one CSV and one plotting
script. Left: heatmap of Full/Reactive speedup for every one of the 15 models by
edit class and scope; annotate executed/total stages in each cell. Right-top:
ECDF of edit-to-result latency for Full and Reactive. Right-bottom:
selection/evaluation/verification share for p50 and p95, with total time
labels. CSV:
figure-06-model-update.csv. Raw columns: system,system_revision,subject,
subject_hash,total_ops,
affected_ops,fanout,stages,edit_class,edit_scope,edit_site,policy,cache_state,
iteration,wall_ns,select_ns,evaluate_ns,verify_ns,executed_stages,reused_stages,
observed_ops,observed_values,changed_functions,evaluated_ops,plan_compiles,
plan_hits,miss_reason,output_digest,correct,seed. -->

<!-- FIGURE 7 PLAN — Single-column scaling figure fed by one CSV and one plotting
script. Log-scaled x-axis is total operations; y-axis is update latency.
For an affected metadata edit, separate lines show cones of 1/8/64/512
operations and Full. A lower inset plots observed entities. Do not connect
unsupported or missing cases.
CSV: figure-07-scaling.csv, using the Figure 6 columns on generated subjects. -->

### 4.5 Artifact Quality and System Costs

Artifact measurements keep compiler responsiveness separate from generated-code
quality. The operator matrix covers elementwise chains, reductions, matrix
multiplication, convolution, quantize/dequantize paths, and fusion opportunities
across fixed shapes and dtypes; model results use the accepted corpus subset.
We compare unoptimized Joggle, optimized Joggle, and a pinned reference runtime
on one CPU. Operator records contain kernel and call latency, compile time, and
code size; model records contain inference latency, peak memory, and artifact
size. Speedups are computed per subject before geometric aggregation, and fixed
inputs establish numerical equivalence.

<!-- FIGURE 8 PLAN — Single-column operator figure fed by one CSV and one
plotting script. Use log-scale paired points for unoptimized Joggle, optimized
Joggle, and the pinned CPU reference, faceted by operator family. Separate
preparation time from steady-state latency. CSV: figure-08-operators.csv.
Columns: operator,family,shape,dtype,system,system_revision,variant,iteration,
prepare_ns,latency_ns,code_bytes,max_abs_error,correct,seed. -->

<!-- FIGURE 9 PLAN — Full-width model figure fed by one CSV and one plotting
script. Left: per-model latency relative to the pinned CPU reference. Right:
peak memory and artifact size as aligned dot plots. Mark the common supported
set and retain unsupported models as coverage marks rather than dropping them.
CSV: figure-09-models.csv. Columns: model,model_hash,system,system_revision,
variant,supported,reason,iteration,prepare_ns,latency_ns,peak_bytes,
artifact_bytes,max_abs_error,correct,seed. -->

Finally, cold-path accounting separates loading, parsing, verification, plan
construction, and first execution; warm accounting separates selection,
evaluation, verification, and commit. The Whole-mod and No-plan-cache policies
provide the two mechanism ablations: observation precision and persistent
execution plans. Their counters connect latency changes to observed entities,
executed compiler-function operations, and plan compilation.

Together, generation characterizes the extension surface, patch footprint its
ownership boundary, reactive execution its update cost, and artifact and
overhead measurements its end-to-end consequence.

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
