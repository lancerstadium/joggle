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

Figure 1 connects each development problem to a Joggle mechanism and its
evaluation endpoint. Extension tasks measure executable completion and agent
effort; paired patches measure change footprint; controlled edits measure
update latency and executed work. Together, these comparisons link the
extension interface, ownership boundary, and execution model to their effects
on compiler development.

<!-- FIGURE 1 PROMPT — A dense two-column 3×3 systems-paper argument map. The
columns are PROGRAMMABILITY, ORGANIZATION, and UPDATE; the rows are CHALLENGE,
JOGGLE DESIGN, and OUTCOME. Each column reads top to bottom with no cross-column
arrows. Programmability: fragmented Semantics/Analysis/Transform/Convert/Emit
mechanisms → Unified metaprogramming using typed compiler functions, one language,
one call model, one value model, and `Ty Attr Mod Fn Op Val` → CONVENIENT with
`task success` and `tokens · tool calls`. Organization: one feature scattered across hierarchical
IR, pass registry, build target, conversion, and backend → Graph-level mod
boundary with `mod quant`, `use tensor`, own/publish/version/change tabs, and an
app→quant→tensor use graph → CONTROLLABLE with `files · lines` and `ownership
zones`; depict a feature patch as filename bars without invented source code.
Update: local edit at B causing suffix B→C→D rerun → Reactive re-execution over an
affected subgraph using revisions, dependency index `D/W`, and cached execution
plan → EFFICIENT with `p50 · p95`, `visited work`, and `edit-to-artifact`.
Use a changed→execute / unchanged→reuse ledger instead of synthetic curves.
The outcome row names measured endpoints without displaying invented results.
Use compact technical
glyphs, thin dark connectors, white background, restrained blue/teal/lavender,
and coral only for changed state. Use only the named Joggle constructs; omit
source snippets, line numbers, numbered circles, red numeric labels, gradients, shadows, and
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

Figure 3 separates the two graphs that organize compilation. The mod graph
determines which compiler functions are available; the subject graph contains
the program those functions inspect and refine. Typed handles connect the
shared evaluator to the subject store. Execution plans cache function decoding,
whereas dependency records track the graph state observed by a call. These
caches serve different purposes and share a transactional publication boundary.

<!-- FIGURE 3 PROMPT — system-architecture.png. Original compact single-column
square compiler architecture, white background, fine charcoal rules, pale blue
and amber nodes, small serif mathematics and monospace field annotations.
Top left: tensor/fusion/target mod boundaries, use edges from dependents to
tensor; legal/fuse/lower/emit function nodes. Top right: nested M/f/b0 subject
graph x,w→Conv→Add→ReLU→y, b→Add, value circles and users(v) links.
Middle: f:(H,A)→R and parallel query(R), run(RW), emit(R) entrances to one
resolve/evaluate interface; a compact body/intrinsic/native bracket identifies
implementation forms. Bottom: evaluator plan table, register slots and
K/D/W records beside separate F/B/O/V entity-slot arrays, h=(S,i,g), revision
scopes and a separate o0→v0→o1 def-use relation. Plan key includes store,
function, generation and revision. Tiny annotations distinguish query (K,r,D)
from stage (K,D,W) records and decode the store/slot/generation handle fields.
Transaction: G→G′→verify ownership/types/uses, success publishes
graph and dependency records; failure restores G. No invented timing numbers,
large title bands, decorative icons, paragraphs, or sequential query/run/emit.
Use thin dependency arrows and tiny local annotations, not word-heavy cards. -->

*Figure 3: Joggle's two graph structures. Mods delimit compiler capabilities;
typed calls operate on the subject graph. Decoded plans, dependency records,
and versioned entity stores support transactional publication. Entries are schematic.*

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

Figure 4 follows the operator extension introduced in Section 2. The graph
computes $y=\max(\operatorname{Conv}(x,w)+b,0)$. A legality function checks
types, shapes, and intermediate uses; fusion replaces the matched operations
with one semantic operation; conversion selects its target form. These graph
states, $G_0$, $G_1$, and $G_2$, retain the same input/output contract. An
emitter reads $G_2$ and returns the artifact. The extension's mod owns all five
roles, which exchange graph handles and owned values through the same call
interface.

<!-- FIGURE 4 PROMPT — Original compact single-column scientific diagram,
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

*Figure 4: A schematic fused-operator extension. One mod owns five compiler
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
invoke the legality analysis in Figure 4, and a conversion can query the fused
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

Figure 5 illustrates the distinction between a direct and an upstream miss.
An external edit changes the convolution's layout metadata. Stage $s_1$ read
that property, so its observation is stale. Stage $s_2$ read the ReLU operation:
that observation remains current at selection time, but $s_1$ previously wrote
to the containing function $f$. This output scope overlaps $s_2$'s input, so
$s_2$ is selected as an upstream miss. Stage $s_3$ observed a reduction in a
disjoint function $g$ and retains its result. Thus, stage selection follows
recorded reads and write scopes, not merely reachability from the edited node.

<!-- FIGURE 5 PROMPT — reactive-update.png. Dense square single-column
mechanism diagram, fine black rules, small math annotations, pale blue graph
nodes, amber selected stages and hatched retained records. Three tight bands:
(a) f contains x,w→Conv→Add→ReLU→y and b→Add; disjoint g contains u→Reduce→v.
External edit layout a→b at Conv; cached D1 records layout=a, D2 observes ReLU,
D3 observes Reduce. (b) Record table s1/Conv.layout/f/stale,
s2/ReLU/f/upstream, s3/Reduce/empty/reuse; selected_i=stale(Ki,Di) OR
overlap(dirty,Di). Scope intersections explain selection. (c) s1→s2 within a
transaction captures fresh D′/W′; verify G′ precedes joint publication;
failure restores G, including the already committed external edit layout=b.
Retained s3 records are rebased. No timings, large headings or prose boxes. -->

*Figure 5: Reactive stage selection. A stale observation selects $s_1$;
overlapping effects select $s_2$; disjoint observations retain $s_3$.
Verification gates joint publication; rollback preserves the committed external edit.*

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
| Efficient | 3 systems; 15 models | update time, reuse |
| End-to-end | 4 systems | correctness, latency |

*Table 1: Evaluation matrix. Every comparison fixes revisions, inputs, and its
correctness oracle before measurement.*

**Subjects and controls.** Extension and ownership comparisons use Joggle,
MLIR, and xDSL. Each extension follows its system's native API at a pinned
revision. The production update protocol instead targets Joggle, TVM, and
ONNX-MLIR: its endpoint is an executable for the edited model, not an analysis
summary. It shares the 15 pinned ONNX subjects and fixed inputs with the
end-to-end comparison. End-to-end execution also includes ONNX Runtime.

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

Figure 6 summarizes the common comparison structure. Extension and ownership
studies share feature specifications; update paths start from the same edited
graph; execution paths consume identical tensors. The oracle precedes metric
aggregation in each branch. Successful cases provide paired measurements,
while unsuccessful cases remain in the coverage denominator.

<!-- FIGURE 6 PROMPT — evaluation-workflow.png. Original dense square
single-column protocol schematic, fine black rules, white background, small
serif math labels and monospace annotations. Independent source glyphs for
specification, graph G, tensor X and revision/hash. Four compact rows:
(a) native function/type specification → Joggle/MLIR/xDSL → budgeted code/oracle
repair loop → pass/tokens; (b) feature patch hunks and package dependency
boundaries → (F,L,Z,R); (c) same edited G forks into update/full compilation,
replacement executables and output-tensor check → Tu and Tu/Tf;
(d) same X forks into Joggle/ORT/TVM/ONNX-MLIR, numerical comparison →
median/p95 and correct/total. Label extension and ownership rows
Joggle/MLIR/xDSL; label update row Joggle/TVM/ONNX-MLIR. Success/failure branches
both retain CSV records. This is a protocol, not numerical results. No
fabricated bar charts, percentages, prose boxes, large headers or gradients. -->

*Figure 6: Four paired comparisons. Shared specifications, edited graphs,
and tensors align inputs; correctness gates measurements while failures
remain in coverage. Update shading denotes executed work.*

### 4.2 Agent Extension Completion

The extension protocol specifies 24 tasks, four in each of six families:
type or operation definition, analysis, rewrite, conversion, artifact
generation, and a vertical feature combining these roles. Each task has one
semantic specification and fixed positive and negative fixtures. Admission to
the trajectory matrix requires a system-specific harness and an idiomatic
reference patch that passes the shared oracle.

The agent protocol uses two frozen small instruction models with the same deterministic coding-agent
harness. For each system, the agent receives the semantic specification, a
compact native API card, an isolated workspace, and the same inspect, edit,
build, and test tools. It may take at most 30 actions and emit at most 32k
tokens. Each model--system--task condition runs ten paired seeds with zero or
two disjoint demonstrations. The full design contains 2,880 trajectory conditions.

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

The production update endpoint is a replacement executable for a changed
model. Its timing boundary starts when the edit is applied to a previously
compiled model and ends after the replacement passes numerical validation
against the edited model's reference outputs.
Cold construction of reusable compiler state is measured separately.

For system $s$ and edit $e$, the paired update ratio is

$$
UpdateRatio_{s,e}=\frac{T_{update,s,e}}{T_{full,s,e}}.
$$

The denominator is a complete rebuild of the same edited model under the same
optimization policy. Absolute update latency permits cross-system comparison;
the ratio quantifies reuse within one system. Both measurements are necessary:
a small ratio alone does not establish a shorter development cycle.

The protocol separates model-graph, optimization-policy, and compiler-source
edits. Each case fixes the edit location and semantics, input tensors, and
correctness tolerance. Native caches remain enabled, with retained state and
invalidation recorded for each path. Coverage counts the cases that produce
correct replacement executables. Stage timings explain the total cost rather
than replacing it with an isolated propagation measurement.

<!-- FIGURE 6 PROMPT — Production update results only. Compact aligned panels:
absolute edit-to-executable latency by model and system; paired Update/Full
ratios; breakdown of the same measured update paths. Separate graph, policy,
and compiler-source edits. Shared model order, boxed axes, small inward ticks,
and one legend. Use measured, numerically verified replacement executables;
show unsupported coverage without assigning zero latency. Matched-stage
diagnostic timings are not production update results. -->

### 4.5 End-to-End Performance

The final experiment quantifies the performance and coverage of code emitted
through the programmable infrastructure. One matrix contains 24 fixed operator graphs---four each for
elementwise chains, reductions, matrix multiplication, convolution,
quantization, and fusion---and the same 15 model subjects. We compare Joggle's
required lowering path, the same path plus its frozen optimization pack, and a
pinned single-thread ONNX Runtime CPU reference. The operator comparison also
includes TVM Relax's default LLVM CPU pipeline without tuning and ONNX-MLIR's
LLVM pipeline at `-O3`, with parallelism and fast math disabled.
Each case uses byte-identical inputs; dtype-specific numerical oracles gate its timing results. Joggle fixes
each entry signature from those inputs before either lowering pipeline begins.

The main measure is steady-state execution latency after ten warm-ups and 100
measurements. The performance figure groups the 24 operators into six compact
panels with shared logarithmic axes. Each bar extends from parity to a
per-operator median divided by its ONNX Runtime median; the upper whisker reaches p95 under the
same denominator. The dashed line marks equal latency. Values below one
indicate faster execution. Whiskers describe timing variation. Model results
use the same normalization, and unsuccessful cases contribute to coverage.

<!-- PERFORMANCE FIGURE PROMPT — Render from measured CSV using
artifact/figures/figure_07_performance.py. Compact single-column figure with
six panels, two rows by three columns: elementwise, reduction, matmul,
convolution, quantization, fusion.
Every panel contains four operators and grouped base/optimized/TVM bars. Shared
logarithmic y axis, one legend, ORT=1 dashed line, median-to-p95 whiskers,
hatched base bars, solid optimized bars, and dotted amber TVM bars. Mark unsupported
TVM cases with ×, not zero-height bars. Report correct coverage in the
caption. Bars start at parity; use compact wrapped labels and shared axes at
the final column width. Enclose every panel in four thin spines with inward
ticks on all sides; share colors, hatching, and the compact legend with Figure 8.
Source CSV: paper/data/figure-07-operators.csv.
Model measurements have a separate companion display. Preserve every case and failed outcome.
No generated pixels or illustrative numbers for data. -->

*Figure 7: Operator latency relative to ORT (log scale; lower is faster).
Bars run from parity to the median; whiskers reach p95 over 100 samples.
Joggle and ORT pass 24/24 cases; TVM passes 22/24. × marks unsupported
QLinearConv and QLinearMatMul imports.*

| Operator-suite measure | Base | Optimized | TVM | ONNX-MLIR |
| --- | ---: | ---: | ---: | ---: |
| Correct operators | 24/24 | 24/24 | 22/24 | 22/24 |
| Faster than ORT | 4/24 | 4/24 | 7/22 | 7/22 |
| Latency / ORT, all 24 | 9.27× | 5.21× | — | — |
| Latency / ORT, common 22 | 8.95× | 5.23× | 6.66× | 3.18× |

*Table: Operator summary. Geometric means use all 24 or the common 22 operators,
as indicated. Per-operator values appear in Appendix A.*

Across the 22 jointly correct operators, Joggle's optimized path was 1.27×
faster than default TVM; ONNX-MLIR was 1.64× faster than Joggle.
Joggle had lower median latency on ten operators against TVM and eight against
ONNX-MLIR. Its rectangular and square matrix products outperformed TVM, whereas TVM favored several
elementwise and reduction workloads. All three compiler configurations remained
slower than ORT in aggregate.

Joggle and ORT passed all 24 operators, while each external compiler passed 22.
TVM could not import QLinearConv or QLinearMatMul; ONNX-MLIR could not lower
QLinearConv, and its QLinearMatMul output failed the integer oracle.
The common-set aggregate therefore used the same 22 operators across all
configurations, with complete per-operator measurements in Appendix A.

Within Joggle, the optimization pack reduced geometric mean latency by 1.78×
over all 24 operators, with gains concentrated in matrix multiplication.
Rectangular and 256×256 products improved by 21.67× and 12.90×, respectively.
Strided convolution instead slowed by 1.11× relative to the base path.
These results distinguish optimization gains from cross-system kernel performance
and complement the compiler update costs in Section 4.4.

Figure 8 extends the comparison to all 15 models. Both Joggle paths pass the
numerical oracle on the same 11 models; ONNX Runtime passes all 15. SSD-MobileNet
and TinyYOLOv3 stop during preparation, while EfficientNet INT8 and QDQ exceed
the numerical tolerance. These four cases remain visible in the figure
and do not enter latency aggregates.

Across the 11 jointly correct models, the optimization pack reduces latency by
a geometric mean of 2.30× relative to the base path. The corresponding latency
ratios to ONNX Runtime are 48.52× for the base path and 21.05× for the optimized
path. All 11 optimized models remain slower than ONNX Runtime. MobileNetV2
improves from 192.94 to 86.85 ms, whereas XCiT changes from 3081.52 to 2955.53 ms;
the respective ONNX Runtime medians are 6.02 and 36.24 ms. The pack therefore
accelerates MobileNetV2 by 2.22× but XCiT by only 1.04×. The model breakdown
separates these workload-dependent gains from the suite-wide aggregate.

<!-- FIGURE 8 DATA — Single-column 3.35-inch paired bar plot, two rows by
three columns. Five panels contain all 15 models grouped as dense CNNs,
mobile CNNs, detectors, quantized models, and other models; the sixth gives
the paired geometric means. Shared logarithmic latency/ORT axis, parity at
one; hatched gray base bars and solid teal optimized bars start at parity.
Whiskers extend from median to p95; the aggregate has no timing whisker.
Four thin spines, inward major/minor ticks, compact labels, and one legend.
Explicit ×C for preparation failures and ×N for numerical failures, never
zero-valued bars. Aggregate only the 11 jointly correct models.
CSV: paper/data/figure-07-models.csv; per-model summaries:
paper/data/figure-07-models-summary.csv; script:
artifact/figures/figure_07_models.py. -->

*Figure 8: Model execution by family. Bars show median latency / ORT;
whiskers reach p95 over 100 samples. ×C/×N mark preparation/numerical failures.
Panel (f) aggregates the 11 jointly correct models.*

<!-- PERFORMANCE DATA — Separate operator and model displays form one
end-to-end experiment. Each display has a source CSV and plotting script.
Model CSV: paper/data/figure-07-models.csv. Columns:
subject_kind,subject,subject_hash,family,system,system_revision,variant,
supported,reason,iteration,calls_per_sample,latency_ns,max_abs_error,
max_rel_error,input_digest,output_digest,correct,seed. -->

Together, generation characterizes the extension surface, patch footprint its
ownership boundary, the cross-system update study its responsiveness, and the
combined operator/model matrix its generated performance.

## 5. Related Work

The related-work table aligns mechanisms with Joggle's three design axes. **Roles**
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

| Operator | ORT (µs) | Base / ORT | Opt / ORT | TVM / ORT | ONNX-MLIR / ORT |
| --- | ---: | ---: | ---: | ---: | ---: |
| conv-depthwise | 41.92 | 3.64 | 4.57 | 0.82 | 3.74 |
| conv-pointwise | 63.83 | 146.92 | 32.66 | 144.27 | 12.65 |
| conv-stem | 178.12 | 4.44 | 9.35 | 2.50 | 5.96 |
| conv-strided | 39.06 | 171.00 | 190.42 | 186.00 | 180.75 |
| ew-affine-1k | 2.80 | 0.08 | 0.08 | 0.33 | 0.21 |
| ew-broadcast-relu | 3.58 | 0.81 | 0.79 | 0.32 | 0.21 |
| ew-chain-64k | 21.55 | 3.40 | 3.92 | 1.32 | 0.55 |
| ew-select-16k | 8.08 | 0.98 | 0.87 | 0.59 | 0.96 |
| fuse-add-relu | 14.11 | 4.89 | 4.53 | 0.85 | 0.52 |
| fuse-conv-bias-relu | 294.42 | 101.45 | 43.96 | 84.67 | 75.86 |
| fuse-matmul-bias-relu | 14.26 | 170.18 | 13.35 | 166.48 | 6.56 |
| fuse-mul-add | 20.34 | 2.32 | 2.44 | 0.83 | 0.47 |
| mm-batched | 10.64 | 71.02 | 71.00 | 69.89 | 6.41 |
| mm-rectangular | 13.23 | 214.80 | 9.91 | 225.47 | 10.36 |
| mm-square-256 | 31.51 | 327.35 | 25.37 | 330.90 | 16.21 |
| mm-square-64 | 4.55 | 20.57 | 1.66 | 20.40 | 1.57 |
| quant-conv | 26.51 | 9.06 | 10.25 | — | — |
| quant-dynamic | 4.82 | 1.48 | 1.52 | 3.15 | 5.24 |
| quant-matmul | 6.15 | 20.78 | 2.40 | — | × |
| quant-qdq-tensor | 3.40 | 0.54 | 0.56 | 0.44 | 0.65 |
| red-l2-last | 15.46 | 1.10 | 1.07 | 1.23 | 1.21 |
| red-max-channel | 59.16 | 10.66 | 10.40 | 3.72 | 2.22 |
| red-mean-spatial | 12.25 | 9.54 | 9.57 | 6.75 | 8.36 |
| red-sum-row | 9.49 | 5.78 | 6.28 | 6.36 | 6.39 |
| Geometric mean, all 24 | — | 9.27 | 5.21 | — | — |
| Geometric mean, common 22 | — | 8.95 | 5.23 | 6.66 | 3.18 |
| Correct operators | 24/24 | 24/24 | 24/24 | 22/24 | 22/24 |

*Table A1: Operator execution measurements. Joggle revision `5a71fe55a3be`,
Apple Clang 17.0.0 (`-O3 -DNDEBUG`), ONNX Runtime 1.26.0 CPU with full graph
optimization, TVM revision `c7b458e946bc` (default LLVM), and ONNX-MLIR revision
`4a13c34aa695` (`-O3`, no parallelism or fast math). All use one CPU thread.
Each passing entry uses ten warm-ups and 100 measured samples. Ratios divide
unrounded per-operator medians; values below one indicate lower latency than
ORT. — denotes an unsupported operation; × denotes a failed numerical oracle.
Geometric means use the indicated common population.*
