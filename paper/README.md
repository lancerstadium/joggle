# Joggle

## Abstract

## 1. Introduction

## 2. Motivation and Design Requirements

### 2.1 Compiler Extension Workflow

### 2.2 Development Friction

### 2.3 Design Requirements

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
each value to its users. This structure does not privilege an operator set,
input format, or artifact. Such semantics enter through extension mods.

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

Figure 2 follows this call from definition to publication. A mod supplies the
typed function, the evaluator observes its graph access, and the runtime either
commits a verified update or returns a read-only result.

<!-- FIGURE 2 PROMPT — Compact square Joggle call mechanism for 0.80 of one ACM
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

*Figure 2: A typed compiler call executes transactionally and records reads and
effects for reuse.*

The remainder of this section develops the model in execution order. Section
3.2 defines compiler functions. Section 3.3 explains how mods own and compose
them. Section 3.4 derives incremental execution from observed dependencies.
Section 3.5 presents the runtime mechanisms that preserve these contracts.

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
may be a source body, an intrinsic, or a native binding. This choice does not
change the caller's syntax or the evaluator's result boundary.

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
therefore available to type checking, diagnostics, and dependency capture. It
does not need a second, global pipeline registry.

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
each stage $s_i$, both its observed inputs $D_i$ and its previous output scope
$W_i$. The output scope contains changed function identities and flags for
structural or mod-dependency changes. A stage is selected when its own inputs
are stale or when an earlier selected stage may change something it observed:

$$
Selected_i = \neg Current(D_i) \lor
  \exists j<i:\ Selected_j \land Overlap(W_j,D_i).
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

Figure 3 separates the dependency levels. The static `use` graph defines which
functions may be called; a compact observation ledger records which entities a
particular run read. An edit invalidates the stage that observed it and only
the later stages whose recorded inputs overlap its effects.

<!-- FIGURE 3 PROMPT — Compact square dependency diagram for 0.80 of one ACM
column, using Figure 2's thin strokes and restrained semantic colors. The top
quarter is a tight `use` graph: Project→sat,nn; sat→ir; nn→tensor,math; and
ir,tensor→base. The lower region is a four-column ledger headed stage, D: reads,
W: effects, decision. Its rows are Select | Val9 type | Op17 | DIRECT; Plan |
Op17 | Fn3 | UPSTREAM; Analyze | Fn8 | — | REUSE; Emit | structure | bytes |
REUSE. A small `edit Δ = Val9 type` chip points to DIRECT; a dashed link marks
the observation; a purple arrow connects Select's Op17 effect to Plan's Op17
read and is labeled overlap. Use tight cells and short arrows, with no large
panels or decorative whitespace. -->

*Figure 3: Static `use` edges bound calls; dynamic read/effect records select
work after an edit.*

Fine-grained observations reduce re-execution but add capture and validation
work. For $K$ stages, incremental latency is approximately

$$
\begin{aligned}
T_{select} &= \sum_{i=1}^{K} T_{validate}(D_i) + T_{propagate}, \\
T_{inc} &= T_{select} + T_{commit} \\
        &\quad + \sum_{s_i \in Selected}
          \left(T_{eval}(s_i) + T_{verify}(s_i)\right).
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
advance the owning function's revision. These counters are freshness tokens,
not semantic versions. Their granularity lets a function-level observation
survive an unrelated edit elsewhere in the mod.

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
(G', \mathrm{ok}) & \text{if } Eval(f,G) \Downarrow G' \land Verify(G'),\\
(G, \mathrm{fail}) & \text{otherwise.}
\end{cases}
$$

Thus, locally valid edits cannot expose a globally invalid graph. Successful
verification commits the graph and its revisions; failure restores both.

The evaluator executes checked compiler functions through predecoded plans. A
plan maps parameters, block arguments, and operation results to dense slots;
it also records block targets, operation kinds, operand indices, and call-site
information. Its cache key is

$$
PlanKey = (S, id(fn), generation(fn), revision(fn)).
$$

Changing a function body invalidates its plan, while calls to the same version
reuse decoding and slot layout. Stable call sites additionally cache overload
resolution under the current environment epoch and argument types. Reusable
register windows reduce per-call allocation.

These plans are an internal execution form, not another public IR and not
native machine code. They remove repeated interpretation setup while
preserving the language's control flow, failure propagation, and dynamic
calls. A native implementation may accelerate a specific typed function, but
it enters through the same mod-scoped binding and graph API.

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

### 4.2 Programmability

### 4.3 Change Locality

### 4.4 Update Performance

### 4.5 Artifact Quality

### 4.6 Overheads

## 5. Related Work

### 5.1 Extensible Compiler Infrastructures

### 5.2 Tensor Compilers

### 5.3 Rewriting Systems

### 5.4 Incremental Computation

## 6. Discussion

## 7. Conclusion
