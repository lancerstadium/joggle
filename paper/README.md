# Joggle

## Abstract

Extending a heterogeneous compiler is a cross-cutting task. One feature can
require a semantic definition, an analysis, a graph transformation, a
conversion, and an artifact generator, yet established infrastructures expose
these roles through different extension and invalidation mechanisms. Joggle
makes compiler behavior part of the program model. Programs and compiler
extensions share one typed graph, one function language, and one value model;
graph-level *mods* define ownership and dependency boundaries across stages;
and an evaluator records graph observations and effects while publishing
updates transactionally. The resulting intermediate representation is
progressive: typed compiler functions refine verified mods without requiring a
fixed sequence of public IR classes. Joggle combines stable graph handles,
hierarchical revisions, dependency indices, and cached execution plans to
re-execute only stages whose recorded inputs may have changed. We evaluate the
design through held-out compiler-extension tasks, matched cross-system feature
patches, controlled edits over a pinned full-model corpus, and operator- and
model-level artifact measurements. The evidence tests whether extensions are
predictable to generate, whether changes remain within their ownership
boundary, and whether update cost follows the affected graph rather than the
complete pipeline.

## 1. Introduction

Compilers for heterogeneous systems coordinate decisions at several scales.
They define operator semantics, transform graphs, schedule loops and memory,
convert representations, and generate artifacts for processors and
accelerators. LLVM established the value of a shared typed representation for
analysis and transformation [@lattner2004llvm]; MLIR generalized this approach
to multiple abstraction levels [@lattner2021mlir]; Halide separated algorithms
from schedules [@ragankelley2012halide]; and TVM combined graph- and
operator-level optimization for diverse targets [@chen2018tvm]. These systems
make programs increasingly malleable. Extending the compiler that manipulates
them, however, still requires coordinating mechanisms with different syntax,
ownership, and execution rules.

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
source trees, registries, and IR layers rather than the feature's dependency
boundary, so its change surface grows with the compiler. Third, staged drivers
usually track whole pass results, so a small edit can repeat conversions and
analyses whose observations remain valid. These are programmability,
organization, and update-efficiency problems, respectively.

Figure 1 states the paper's argument as three vertical chains. Each column
connects a development problem to one Joggle mechanism and one measurable
outcome. This 3×3 mapping also aligns the evaluation with the design:
small-model synthesis perplexity measures the extension surface, change
footprint measures ownership, and update latency measures reuse after an edit.

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

We call the representation *progressive* because compilation refines one
verified graph rather than requiring every extension to cross a fixed sequence
of public IR types. A mod may retain semantic operations, introduce lower-level
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
   This boundary complements rather than replaces hierarchical program IR.
3. **Dependency-directed updates.** Revisions, dependency indices, and cached
   execution plans restrict responsive re-execution to affected graph regions
   and stages under transactional publication.

The evaluation assigns one evidence stream to each contribution. Held-out
extensions measure small-model uncertainty and executable task success. Matched
feature patches measure repository footprint. Controlled edits over complete
models measure latency and executed work. Artifact quality and mechanism costs
complete the evidence chain without conflating compiler responsiveness with
generated-code quality.

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

<!-- FIGURE 2 PROMPT — Preserve the supplied overview artwork and its existing
layout. It shows model structure and formats above offline operator-, graph-,
and system-level compilation; an online runtime and target devices below; model,
pass, operator/type, and hardware customization on the left; and design,
optimization, and deployment roles on the right. Do not regenerate or alter
the figure. -->

*Figure 2: A compiler extension can span semantic definition, optimization,
runtime support, and target deployment.*

As a running example, consider introducing saturating arithmetic for a
low-precision model. The feature begins with a parametric type and arithmetic
semantics. It then needs a capability test, a transformation that selects the
new operation, a storage policy, and one or more target implementations. The
change is conceptually one feature, but its pieces occupy several stages in
Figure 2. Later revisions—such as admitting another width or changing target
selection—must preserve the same cross-stage agreement.

Existing infrastructures offer rich extension points. LLVM exposes analyses
and passes, MLIR adds dialects and conversions, Halide exposes schedules, and
TVM exposes graph and tensor-program optimization [@lattner2004llvm;
@lattner2021mlir; @ragankelley2012halide; @chen2018tvm]. A complete feature
combines several of these roles, while their declarations, composition
boundaries, and invalidation rules remain separate.

### 2.2 Development Friction

*Fragmented metaprogramming.* An operator schema describes admissible programs,
but a pass, converter, and emitter describe actions over those programs. When
each role uses a different registration and invocation model, neither a person
nor a synthesis tool can treat the feature as one typed program. The required
context includes framework conventions that are not present in the local
definition, increasing both implementation effort and the opportunity for an
incomplete extension.

*Diffuse ownership.* Hierarchical IRs organize operations inside regions,
blocks, and functions. That containment is essential for program semantics, but
it does not by itself express which compiler capabilities belong together or
which project depends on them. Capability ownership is often reconstructed from
directories, build targets, registries, and pass ordering. A feature revision
can therefore touch files and subsystems outside its semantic boundary, making
review, replacement, and parallel development harder to control.

*Coarse update boundaries.* A conventional pipeline establishes correctness by
running an ordered sequence of stages over each new input. After a local graph
or policy edit, rerunning the affected suffix is safe but can be unnecessarily
broad: an analysis that did not observe the edited entity is recomputed, and a
later stage can be rerun even when an earlier stage publishes no relevant
effect. Multi-level conversion amplifies this work because each materialized
boundary becomes another unit to rebuild, validate, and traverse.

The three costs reinforce one another. Fragmented interfaces spread a feature;
spread ownership enlarges its change surface; a larger change surface forces
coarser invalidation. Addressing only one layer leaves the other two as limits
on extension velocity.

### 2.3 Design Requirements

The preceding workflow gives three requirements for a malleable compiler.

**R1 — One typed extension surface.** Semantic definitions, analyses,
transformations, converters, and artifact generators must share one language,
call model, and value model. Uniformity must not erase effects: read-only
queries and mutating transformations require different publication rules even
when they use the same syntax.

**R2 — Explicit capability composition.** A feature must have a named owner,
public and private functions, declared dependencies, and an independent
lifecycle. This graph-level package boundary must remain separate from
hierarchical program containment so that a compiler capability can be
installed, inspected, replaced, or removed without editing a global registry.

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

Figure 3 follows this call from definition to publication. A mod supplies the
typed function, the evaluator observes its graph access, and the runtime either
commits a verified update or returns a read-only result.

<!-- FIGURE 3 PROMPT — Compact square Joggle call mechanism for 0.80 of one ACM
column. Use a white background, thin dark strokes, restrained blue/teal/purple/
coral fills, short arrows, tight boxes, and monospace code labels. The top strip
contains `mod sat`, `use ir`, `fn select`, `[stage: select]`, and
`select(Mod) -> bool`, with analyze/transform/convert/emit chips. A compact
Evaluator performs resolve→execute→observe over a Subject graph
Fn→Blk→Op17→Val9 and a journal/verify transaction. Dashed arrows mark reads;
solid coral arrows mark writes and commit. The bottom is a four-column record:
key `E, fn, args`, result `r`, reads `Val9 type + Op17 callee`, and effects
`Op17`, followed by `reuse = same(key) ∧ current(D)`. Eliminate title banners,
large containers, long connectors, and decorative whitespace. -->

*Figure 3: A typed compiler call executes transactionally and records reads and
effects for reuse.*

Joggle calls this representation progressive because each successful compiler
function publishes another verified mod over the same entity model. A stage may
retain semantic operations while introducing lower-level helpers, and a later
stage may replace only the definitions it owns. Progress therefore does not
require a public transition from one IR class to another.

The rest of the design follows the three requirements from Section 2. Section
3.2 realizes R1 with typed compiler functions. Section 3.3 realizes R2 with
mod-scoped composition. Sections 3.4 and 3.5 realize R3 through observed
dependencies and the graph runtime that validates them.

### 3.2 Compiler Functions

Joggle's metaprogramming interface is the typed compiler function. Operator
semantics, analyses, transformations, representation conversion, and artifact
generation use this single programmable surface rather than separate
definition and invocation mechanisms.

A compiler function accepts graph handles and ordinary values:

$$
f : (H_1, \ldots, H_n, A_1, \ldots, A_m) \rightarrow R.
$$

Each $H_i$ is a `Mod`, `Fn`, `Blk`, `Op`, or `Val` handle. Each $A_i$ is a
configuration or structural value, such as an integer, type, list, dictionary,
or `Attr`. The result $R$ may itself be a handle, an ordinary value, structured
data, text, or bytes. Handles retain graph ownership and lifetime; ordinary
values remain independent of graph storage.

Listing 1 shows these roles in the bundled `sat` mod. The same syntax declares
a parametric type, a capability query, native simulation and artifact
functions, and a mutating selection stage.

```jog
mod sat
use ir

// Semantic type and operations.
fn sat<W: int>() -> Ty;
fn +<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
fn add<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;

// A read-only capability query.
[role: "match"]
fn supports(type: Ty) -> bool {
  if name(type) != "sat" || len(args(type)) != 1 {
    return false
  }
  let width = args(type)[0]
  return kind(width) == "int" &&
         int(width) >= 2 && int(width) <= 63
}

// Native simulation and artifact boundaries.
[role: "sim"]
fn sim(width: int, a: int, b: int) -> int;

[role: "emit", format: "sv"]
fn emit(width: int) -> str;

// A graph transformation.
[stage: "select"]
fn select(m: Mod) -> bool {
  var changed = false
  for op in ir.ops(m) {
    if ir.callee(op) == "operator +" {
      let outs = ir.outs(op)
      if len(outs) == 1 &&
         sat.supports(ir.type(outs[0])) {
        changed = ir.rename(m, op, "sat.add") || changed
      }
    }
  }
  return changed
}
```

*Listing 1: The `sat` mod defines semantics, analysis, native boundaries, and
transformation with one function syntax.*

Given a `sat<W>` result, `select` calls `supports`, retargets the addition to
`sat.add`, and reports whether the subject changed. Its signature distinguishes
mutation from the read-only query. Body-less `sim` and `emit` declarations bind
native implementations behind the same typed call boundary.

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

Compiler functions also compose through ordinary calls. In Listing 1,
`select` invokes `sat.supports`; larger policies can call analyses, converters,
selectors, and planners from their declared dependencies. The call graph is
therefore available to type checking, diagnostics, and dependency capture. The
typed call graph also defines composition, replacing a second global pipeline
registry.

This function model unifies how compiler behavior is declared, resolved, and
invoked. Mods then supply the namespace, visibility, and dependency boundaries
that organize these functions.

### 3.3 Mods

A mod is Joggle's unit of ownership and composition. We model it as

$$
\mathcal{M} = (n, U, F_{pub}, F_{local}, G, N),
$$

Here, $n$ is the mod name and $U$ is its declared `use` set. $F_{pub}$ and
$F_{local}$ are its public and local functions, $G$ is its graph, and $N$
contains optional native bindings. The on-disk package is a distribution form
of this object, not its semantic boundary.

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
rules make the mod, rather than a collection of edits to global registries, the
unit that evolves.

The mod graph is orthogonal to the containment structure inside a subject
program. Blocks and operations describe program structure; `use` edges
describe compiler capability. The former may change during transformation
without rewriting the latter. This separation lets one project compose
analysis, transformation, and artifact mods without embedding those ownership
decisions into its program IR.

Static mod dependencies remain deliberately coarse. They state what a
compiler extension may call. During execution, the evaluator records which
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

Mutating pipelines require one further step. A reactive schedule stores, for
each stage $s_i$, its call key $K_i$, observed inputs $D_i$, and previous output
scope $W_i$. The key names the environment, function, and arguments. The output
scope contains changed function identities and flags for structural or
mod-dependency changes. A stage is selected when its key or inputs are stale,
or when an earlier selected stage may change something it observed:

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

Figure 4 separates the dependency levels. The static `use` graph defines which
functions may be called; a compact observation ledger records which entities a
particular run read. An edit invalidates the stage that observed it and only
the later stages whose recorded inputs overlap its effects.

<!-- FIGURE 4 PROMPT — Compact square dependency diagram for 0.80 of one ACM
column, using Figure 3's thin strokes and restrained semantic colors. The top
quarter is a tight `use` graph: Project→sat,nn; sat→ir; nn→tensor,math; and
ir,tensor→base. The lower region is a four-column ledger headed stage, D: reads,
W: effects, decision. Its rows are Select | Val9 type | Op17 | DIRECT; Plan |
Op17 | Fn3 | UPSTREAM; Analyze | Fn8 | — | REUSE; Emit | structure | bytes |
REUSE. A small `edit Δ = Val9 type` chip points to DIRECT; a dashed link marks
the observation; a purple arrow connects Select's Op17 effect to Plan's Op17
read and is labeled overlap. Use tight cells and short arrows, with no large
panels or decorative whitespace. -->

*Figure 4: Static `use` edges bound calls; dynamic read/effect records select
work after an edit.*

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
preserve results across unrelated edits. External mutable state must appear in
arguments or the environment, and native callbacks must expose relevant graph
reads through the public API. These rules define the state over which Joggle
can validate reuse.

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
advance the owning function's revision. These counters are freshness tokens
rather than semantic versions. Their granularity lets a function-level
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

### 4.1 Experimental Framework

The evaluation follows the three claims in Figure 1. The first analysis tests
whether a uniform extension surface is predictable to a small code model and
still produces executable programs. The second measures whether feature changes
remain inside their declared ownership boundary. The third measures whether
dependency-directed execution makes update work proportional to the affected
graph. Artifact quality and mechanism overhead bound these results: a shorter
update is useful only when it preserves correctness, generated-code quality,
and acceptable cold-path cost.

**Subjects.** The extension and change-footprint experiments compare Joggle
with MLIR and xDSL. MLIR represents a mature multi-level compiler
infrastructure, while xDSL is a Python-native framework designed for rapid
construction of SSA compilers. Each comparison uses a pinned source revision
and the project's documented extension path. The update experiment uses two
tracks. A controlled track compares Joggle's reactive scheduler with
complete rerun, safe suffix rerun, and one-mechanism-at-a-time ablations inside
the same executable. A cross-system track runs the same analysis and rewrite
semantics over equivalent graph inputs in Joggle, MLIR, and xDSL. The controlled
track identifies the cause of reuse; the cross-system track measures the
end-to-end consequence of each infrastructure's public execution model.

**Models.** The full-model corpus is the repository's pinned set of 16 ONNX
models. It spans classification, detection, machine comprehension, quantized
networks, and vision transformers. Every model passes through a fixed
compatibility gate for each compared system. The corpus record
retains all outcomes; aggregate cross-system results use the common accepted
set. A generated graph family supplements these models at $10^3$, $10^4$,
$10^5$, and $10^6$ operations. It controls total graph size, affected-cone
size, fan-out, and stage count independently.

**Correctness.** Every compiler-function task has a build-and-test oracle.
Every transformation measurement verifies the resulting graph and compares a
canonical structural digest with the expected result. Cross-system outputs use
the same semantic oracle rather than textual equality. Artifact measurements
compare output tensors with a reference implementation under an operator- and
dtype-specific tolerance. A sample enters a latency aggregate only after its
correctness checks pass.

**Timing and statistics.** Systems are compiled in release mode with matched
instrumentation. Cold-process, warm-process, and warm-cache measurements are
reported separately. Each latency case uses ten warm-up iterations followed by
100 timed iterations, with case order randomized within a block. We report the
median, 95th percentile, and a task- or model-level bootstrap 95% confidence
interval. Ratios are aggregated with the geometric mean. The artifact records
the machine, operating system, compiler revision and flags, CPU affinity,
frequency policy, model digest, random seed, and raw per-iteration rows.

<!-- TABLE 1 PLAN — Experimental subjects and controls. Use short cells only:
system/revision, host language, extension task count, accepted models, execution
mode, compiler flags, correctness oracle. Put full version and hardware strings
in the artifact, not in dense prose. -->

### 4.2 Convenient: Unified Extensions

This experiment jointly evaluates predictability and executable task
completion; code length is reported as a control. The suite contains 36 paired
tasks in six families: defining a type or operation, writing an analysis,
applying a graph rewrite, converting a
representation, generating an artifact, and completing a vertical feature
that combines these roles. A task has one semantic specification and a
system-specific harness. Reference solutions use the shortest idiomatic path
that passes the same oracle.

The split is by semantic feature. Definitions, analyses, rewrites, and emitters
for one feature remain in the same partition, preventing a model from seeing
one stage of a held-out feature during context construction. Identifiers,
comments, and formatting follow a fixed normalization policy. Generated files
and test harness boilerplate are outside the scored continuation.

We evaluate two open-weight code models in the 1--3B parameter range with
frozen weights. Each prompt contains the task specification and a compact API
card. We then vary the number of training-partition demonstrations from zero to
four, selecting them with one deterministic retrieval rule under the same
token budget for every system. Absolute uncertainty captures the complete
prompting cost; the within-task change across demonstration budgets captures
how quickly the extension surface becomes predictable from local examples.
Sampling parameters, seeds, stopping rules, and maximum continuation length
are fixed per model.

For a reference continuation $x_{1:N}$ and context $c$, token perplexity is

$$
PPL(x\mid c)=\exp\left(-\frac{1}{N}
  \sum_{t=1}^{N}\log p_\theta(x_t\mid x_{<t},c)\right).
$$

We report paired log-perplexity per task and keep values from different
tokenizers separate. Perplexity measures uncertainty over a valid
implementation; pass@$k$ supplies executable validation. A completion must
parse, type-check, compile where required, and pass the task oracle. We report
pass@1, pass@5, and pass@10 from a fixed pool of $n$ samples. If $c$ samples
pass, the estimator is [@chen2021codex]

$$
\widehat{pass@k}=1-\frac{\binom{n-c}{k}}{\binom{n}{k}}.
$$

We also report syntax success, type-check success, context tokens, completion
tokens, and repair-free success. The paired task design separates the extension
surface from differences in task semantics.

<!-- FIGURE 5 PLAN — Full-width, three compact panels. (a) paired task
log-perplexity with one thin line per task; (b) pass@1/5/10 with bootstrap 95%
CI; (c) failure composition: parse/type/oracle. Facet by task family, keep the
two small models separate, and use the same system colors in every panel. CSV
schema: model,system,task,family,demo_count,seed,target_tokens,context_tokens,
nll,parsed,typed,passed,sample_index. -->

### 4.3 Controllable: Mod-Scoped Change

The change-footprint experiment uses the same semantic feature families but
evaluates repository changes.
Twelve tasks cover new capabilities and revisions to existing capabilities;
each task includes its required semantics, analysis, transformation,
conversion, artifact behavior, and tests. Implementations begin from clean,
pinned snapshots and end only when the common oracle passes. A deterministic
minimization pass then removes each changed hunk in turn and retains the removal
whenever the oracle still passes. This produces one locally minimized,
auditable patch per system and task.

For a patch $p$, we define the primary footprint

$$
Footprint(p)=(F_p,L_p,Z_p,R_p),
$$

where $F_p$ is the number of touched implementation files, $L_p$ is added plus
modified implementation lines, $Z_p$ is the number of ownership zones crossed,
and $R_p$ is the number of build, registry, or pipeline declarations changed.
An ownership zone is a source package or build target with a distinct public
responsibility. Test files and test lines are reported in parallel rather than
discarded. Generated files, vendored dependencies, formatter-only changes, and
lock-file churn are excluded by a rule fixed before implementation. Task
specifications, ownership-zone definitions, and counting rules are frozen
before aggregate statistics are computed.

Controllability is evaluated from all four footprint coordinates because line
count and test volume carry different meanings. We show every task and pair the
coordinates with build success and the common semantic oracle. We also report
dependency fan-out: the number of public components whose rebuild or
registration state changes because of the patch. For Joggle, the analysis
records whether the change remains within one mod or crosses declared `use`
edges.

The principal comparison is paired by task: for each coordinate, we compute the
within-task ratio between Joggle and each baseline, then summarize the ratios
with a geometric mean and bootstrap interval. A second view groups tasks by
role to test whether a unified surface helps only small rewrites or also
vertical features. Complete patches, counting scripts, and inclusion decisions
ship with the artifact.

<!-- FIGURE 6 PLAN — One-column dense paired-dot plot. Rows are the 12 feature
changes grouped by role; columns are touched source files, changed source lines,
ownership zones, and registry/build edits. Plot normalized paired ratios, not
paragraphs inside a table. CSV schema: system,task,family,patch,source_files,
source_loc,test_files,test_loc,zones,registrations,fanout,oracle_passed. -->

### 4.4 Efficient: Reactive Updates

The update experiment measures an edit-to-result operation: begin with a
verified optimized graph, apply one controlled edit, restore the required
compiler result, and verify its semantic digest. The Joggle-native pipeline contains analysis,
canonicalization, target selection or legalization, memory planning, and
artifact preparation. The cross-system pipeline uses a common analysis and
local rewrite over graph inputs derived from the same models. Stage order and
rewrite semantics remain fixed across rerun strategies.

The edit matrix covers six invalidation scopes:

1. no graph change, which isolates validation and cache-hit cost;
2. metadata on one value;
3. the type of one value;
4. the callee or operands of one operation;
5. one function-local structural insertion or removal; and
6. a mod dependency or environment change.

Each local edit is instantiated at deterministic positions selected before
timing. Position is blocked by early, middle, and late pipeline relevance so
that one favorable node cannot determine a model's result. Structural edits
preserve verification invariants, and the final oracle checks that every rerun
strategy reaches the same result.

We compare five execution policies. **Full** runs every stage. **Suffix** runs
the conservative suffix beginning at the first stage whose declared input
class may have changed. **Reactive** validates recorded inputs, propagates
overlapping effects, and runs the selected stages. **Whole-mod** replaces
fine-grained observations with one mod revision. **No-plan-cache** retains
reactive selection but decodes compiler functions again. Full and Suffix bound
the reusable work. Whole-mod isolates dependency precision, while No-plan-cache
isolates plan reuse.

The primary latency is wall-clock time from the completed edit to a verified
result. Supporting measurements are selection time, evaluation time,
verification time, executed and reused stages, observed functions,
collections, operations, and values, changed functions, compiler-function
operations executed, plan hits, and peak resident memory. We report both
absolute latency and speedup $T_{Full}/T_p$ for policy $p$. Executed
compiler-function operations $E_p$ define work reuse as $1-E_p/E_{Full}$.
Work counts explain latency and remain comparable when host-language runtimes
differ.

The 16-model corpus tests realistic graph shapes. The generated family then
separates total graph size $|G|$ from the affected region $|\Delta G|$. For a
fixed one-operation edit, it tests whether validation and update latency remain
near the recorded dependency size as $|G|$ grows. A second sweep fixes $|G|$
and increases $|\Delta G|$ to expose the crossover at which complete execution
becomes preferable. Every raw sample includes the miss reason and output
digest, which detects unintended reuse.

The cross-system result is reported in two parts. Cold end-to-end execution
starts a fresh process and includes parsing and setup. Warm edit execution keeps
the graph and pass infrastructure resident and applies the same semantic edit
through each system's public API. Cache state is named for every series. This
separation prevents process startup or persistence policy from being
misattributed to dependency-directed execution.

<!-- FIGURE 7 PLAN — Main full-width data figure. Left: heatmap of Reactive/Full
speedup for every one of the 16 models by edit class; annotate executed/total
stages in each cell. Right-top: ECDF of edit-to-result latency for Full, Suffix,
Reactive. Right-bottom: stacked selection/evaluation/verification time for
p50 and p95. CSV schema: system,model,model_hash,edit_class,edit_site,policy,
cache_state,iteration,wall_ns,select_ns,evaluate_ns,verify_ns,executed_stages,
reused_stages,observed_ops,observed_values,changed_functions,output_digest. -->

<!-- FIGURE 8 PLAN — Single-column scaling figure. Log-scaled x-axis is total
operations; y-axis is update latency. Separate lines for affected cones of
1/8/64/512 operations and Full. A lower inset plots observed entities. Do not
connect unsupported or missing cases. CSV schema: total_ops,affected_ops,
fanout,stages,policy,iteration,wall_ns,observed_entities,executed_stages. -->

### 4.5 Artifact Quality and System Costs

Artifact measurements separate compiler responsiveness from the code it
produces. The operator
suite covers elementwise chains, reductions, matrix multiplication,
convolution, quantize/dequantize paths, and fusion opportunities over a
predeclared matrix of shapes and dtypes. The model suite uses the compatible
subset of the pinned corpus. Joggle's existing artifact mods instantiate the
same typed generation interface used by the design; generated native sources
are compiled with one fixed toolchain and flag set.

For operators, we report steady-state kernel latency, end-to-end call latency,
generated code size, and compile time. For models, we report per-inference
latency, peak memory, and binary or artifact size. Each result includes the
unoptimized Joggle pipeline, the optimized Joggle pipeline, and a pinned
reference runtime on the same CPU. Speedup is computed per operator or model
before geometric aggregation. Fixed random inputs and framework reference
outputs establish numerical equivalence.

The two comparisons serve distinct purposes. The unoptimized comparison
measures the effect of Joggle extensions at operator and model scale. The
reference-runtime comparison locates the generated artifact relative to an
established implementation. Compilation time remains separate from execution
time, and operator microbenchmarks remain separate from complete-model
measurements.

<!-- FIGURE 9 PLAN — Two compact data figures rather than one overloaded plot.
Operator figure: log-scale paired points for unoptimized Joggle, optimized
Joggle, and reference, faceted by operator family. Model figure: per-model
latency ratio plus peak memory, with the common supported set visibly marked.
Operator CSV: operator,shape,dtype,system,variant,iteration,latency_ns,
compile_ns,code_bytes,correct. Model CSV: model,system,variant,iteration,
latency_ns,peak_bytes,artifact_bytes,max_abs_error,correct. -->

The final analysis decomposes dependency-directed update cost. A cold run
measures loading, parsing, initial verification, plan construction, and the
first pipeline execution. A warm run measures dependency capture,
dependency validation, transaction setup, graph verification, and commit. Peak
memory is measured after loading, after the first schedule, and after repeated
edits; bytes per recorded function, operation, and value are derived from the
same runs.

Ablations disable dependency capture, fine-grained observations, persistent
plans, dispatch caching, reusable register windows, and lazy structural
snapshots one at a time. Each ablation runs the same inputs and oracles as the
complete system. A final precision study rewrites the same analysis with a
whole-graph traversal, a function-scoped traversal, and direct entity lookup.
It connects API choice to observed dependency size, validation cost, and later
reuse without changing the analysis result.

Together, these experiments form one evidence chain. Generation tests the
regularity of the extension surface; patch footprint tests whether a complete
feature stays within its declared boundary; reactive execution tests whether
the runtime exploits that boundary after an edit; and artifact and overhead
measurements establish the resulting costs.

## 5. Related Work

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

Joggle addresses a different boundary. Its central unit is not another
hierarchical operation container; it is the typed compiler function together
with the graph-level mod that owns and publishes it. Semantics, analysis,
mutation, conversion, and artifact generation retain their distinct effects
but share one declaration, resolution, and invocation model. The mod graph is
orthogonal to blocks and operations, so capability ownership does not have to
follow program containment.

### 5.2 Tensor Compilers

Halide separates an image-processing algorithm from its schedule, allowing
target-specific choices without changing functional meaning
[@ragankelley2012halide]. TVM combines graph-level optimization, tensor
programs, schedule search, and target code generation
[@chen2018tvm]. Multi-level tensor compilers similarly use progressively lower
representations to expose decisions at the operator, loop, memory, and target
levels. Joggle packages these optimization spaces, their analyses, their
conversions, and their artifact boundaries in a common programmable and
organizational substrate.

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
reads and effects connect them to reactive execution. Nanopasses, transform
dialects, and equality saturation can therefore enter through the same typed
boundary without determining the rest of the compiler's organization.

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

<!-- TABLE 2 PLAN — Large related-work matrix with systems as rows and compact
columns: shared typed IR; multi-level IR; programmable transforms; one syntax
across semantics/analysis/transform/convert/artifact; graph-level capability
package; explicit package dependencies; dynamic graph-read tracking;
stage-effect propagation; transactional publication. Cells use check, partial,
dash, or a one-word qualifier. Every nontrivial cell maps to a cited primary
source in the table notes. Candidate rows: LLVM, Nanopass, MLIR, MLIR Transform,
xDSL, Halide, TVM, egg, Adapton/build systems, Joggle. -->

## 6. Discussion

**Hierarchical structure.** Joggle programs contain
functions, blocks, operations, values, and def-use relations. These structures
express program semantics and permit conventional local reasoning. Compiler
functions refine this graph independently of a global ladder of public IR
classes. A pipeline may retain high-level operations beside lower-level helpers
when that is the most useful verified state.

**A mod is a capability boundary.** Directories split files, and hierarchical
IR splits programs; neither necessarily identifies the owner of a compiler
feature. A mod binds a public typed surface to dependencies, source fragments,
native bindings, and lifecycle operations. Small implementations may remain in
owners, dependency closures, or release cycles.

**Uniform calls preserve effects.** Uniform syntax coexists with explicit
effect contracts. Query execution is read-only, runs publish a verified graph,
and artifact calls return owned values. Native implementations use the same
signatures and ownership boundary. Declarations, resolution, calls, and
composition are uniform; effect checks remain explicit.

**Incrementality follows observable state.** A compiler function gains reuse by
reading the narrowest state needed for its result. Whole-graph traversal
correctly creates a broad dependency; direct lookup creates a narrow one. State
outside the graph enters through typed arguments or the environment. These
rules make the reuse boundary inspectable: miss reasons and observation counts
show why a stage ran.

**Atomic publication.** Dependency records are useful
only for the graph that produced them. Joggle therefore commits mutations,
revisions, and new records after final verification, or restores the previous
state and discards tentative records. This property is stronger than caching a
pass result beside a mutable graph: it prevents a later run from reusing an
observation of an unpublished intermediate.

**Plans remain interpreted.** Predecoded plans cache checked
interpreter work: dense value slots, control-flow targets, operand indices, and
call-site information. They remove repeated setup while preserving interpreted
execution. Native acceleration remains a typed mod binding, and native plan
compilation is an independent design point.

The design admits three direct extensions. Dependency records can be persisted
with serialized mods to reuse work across processes. Independent selected
stages can be scheduled in parallel once their effect contracts establish
noninterference. Finally, effect scopes can move below functions to named graph
regions when workloads justify the additional capture and validation cost.
Each extension preserves the same publication rule: reusable state belongs to
a verified graph version.

## 7. Conclusion

Joggle treats compiler extension as typed computation over a shared graph.
Compiler functions provide one surface for semantics, analysis,
transformation, conversion, and artifact generation. Mods make capabilities
explicit units of ownership and composition alongside hierarchical program IR.
Revisions, observed dependencies, effect propagation,
transactions, and cached execution plans make repeated compilation
change-proportional. This organization connects the way an extension is
written, the boundary in which it evolves, and the work required after it
changes. The evaluation tests these claims separately through generation
success, patch footprint, reactive update latency, artifact performance, and
system overhead.
