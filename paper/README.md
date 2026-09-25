# Joggle: A Progressive IR for Malleable Compilation

## Abstract

As AI workloads evolve, compilers must accommodate new operators, numeric
formats, and hardware targets. A single feature can span semantic definitions,
graph transformations, and code generation, yet its implementation often crosses
separate interfaces and ownership boundaries. Local edits can also trigger
broad recompilation. We present Joggle, a compiler infrastructure that makes
feature composition, ownership, and reuse explicit within a progressive
intermediate representation (IR).
Typed compiler functions unify semantics, analysis, transformation, conversion,
and emission through one language, call model, and value model. Graph-level mods organize these functions across stages;
recorded dependencies then direct reactive execution toward affected work.
In two cross-stage packages, integration uses one source file rather than
three in the matched MLIR and xDSL implementations. Across three models,
prepared-body reuse accelerates executable-ready updates by
$1.46$--$2.49\times$ over complete rebuilds. Generated executables achieve a
$2.03\times$ geometric-mean speedup over default TVM on eight jointly correct
models. Together, these results show how explicit extension boundaries connect
compiler programmability to the cost of accommodating change.

## 1. Introduction

The evolution of AI workloads makes compiler development a continuing part of
deployment. New operators, numeric formats, and accelerators require changes
from semantic definitions to generated code. Extensible IRs and tensor
programming systems provide the building blocks for these changes
[@lattner2021mlir; @feng2022tensorir]. The remaining challenge is to compose
those blocks into features that developers can extend, maintain, and evaluate
without repeatedly coordinating the entire compiler.

Consider adding a fused convolution. Its definition, legality analysis, graph
rewrite, and target implementation must agree on one semantic contract. Yet
these roles may use different interfaces and belong to separate registries,
build targets, and backend libraries. Even after a local modification, a
pipeline can repeat work unrelated to that change. The development cost thus
depends on three boundaries: how capabilities compose, where a feature is
owned, and what an edit invalidates.

Our central observation is that the structure of a compiler extension differs
from the structure of the program it transforms. A feature can span several IR
levels, while adjacent passes can inspect disjoint graph regions. Consequently,
program containment alone cannot determine feature ownership or the work an
edit invalidates. A compiler needs explicit representations of these relationships
alongside its program graph.

We present Joggle, a compiler infrastructure that makes these relationships
explicit. Typed *compiler functions* express behavior, graph-level *mods* own
cross-stage features, and recorded observations and effects direct updates.
Functions progressively refine a typed graph while semantic operations and
lower-level helpers coexist. Each successful mutating run publishes a verified
state over the same entity model. This *progressive intermediate
representation* preserves a feature's ownership as its subject program changes
form.

This organization also provides a concrete setting for agent-assisted compiler
development. Repository-editing benchmarks and agents emphasize executable
feedback [@jimenez2024swebench; @yang2024sweagent], while KernelBench measures
generated kernels [@ouyang2025kernelbench]. Here, an extension can involve
several compiler roles, making their composition part of the task itself.

Figure 1 summarizes the three mechanisms developed in this paper.

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

*Figure 1: Three compiler-development problems and Joggle's corresponding
mechanisms: shared compiler functions, graph-level ownership, and dependency-directed updates.*

**Unified compiler functions.** Five compiler roles share typed calls over
graph handles and owned values. Common resolution and composition rules let
one role invoke another, while effect contracts distinguish inspection,
mutation, and artifact return.

**Graph-level feature ownership.** A mod groups cross-stage functions and
native bindings behind one public interface and declared dependency closure.
Loading, visibility, and publication follow this boundary independently of
containment within the subject program.

**Reactive execution.** The evaluator validates recorded observations
before each stage, accounting for effects published upstream. Entity generations
and revisions identify stale observations; transactional publication keeps
reusable records consistent with the verified graph. Across imported revisions,
content-based specialization keys identify reusable prepared bodies.

The evaluation separates extension behavior, package changes, and update
latency from generated-code performance. Native package integration touches
one file per extension, compared with three in the matched MLIR and xDSL
implementations. Prepared-body reuse accelerates executable-ready updates by
$1.46$--$2.49\times$ over complete rebuilds on three models. On eight models
compiled correctly by all four systems, the generated executables achieve a
$2.03\times$ geometric-mean speedup over default TVM. Together, these
experiments examine both the cost of changing a compiler and the artifacts
it produces.

## 2. Motivation

### 2.1 Cross-Stage Extensions

Figure 2 follows a model from structure to device. Operator definitions specify
semantics; graph passes change connectivity; system passes choose order and
storage; backends produce target implementations. These extension points
describe different responsibilities within one compilation workflow.

<!-- FIGURE 2 PLAN — Single-column figure. Preserve the supplied overview
artwork and its existing layout. It shows model structure and formats above
offline operator-, graph-, and system-level compilation; an online runtime and
target devices below; model, pass, operator/type, and hardware customization on
the left; and design, optimization, and deployment roles on the right. Do not
regenerate or alter the figure. -->

*Figure 2: A compiler extension can span semantic definition, optimization,
runtime support, and target deployment.*

For example, fusing convolution, bias addition, and activation requires more
than a replacement kernel. An analysis checks types, shapes, and intermediate
uses; a rewrite replaces the legal subgraph; conversion and emission realize
the fused operation. Changing its layout or numeric format can affect several
of these implementations while leaving unrelated features unchanged.

Existing systems expose substantial control over this workflow: MLIR supports
multi-level lowering [@lattner2021mlir], Halide separates algorithms and
schedules [@ragankelley2012halide], and Relax composes graph, tensor, and library
abstractions [@lai2025relax]. The issue is therefore not whether a compiler can
express an optimization, but how a complete feature crosses its interfaces.

### 2.2 Boundaries That Spread Change

*Fragmented mechanisms.* A fusion rule must invoke its legality check and pass
the matched graph to conversion. If these roles use different call and value
models, adapters become part of the feature. IRDL makes definitions declarative
[@fehr2022irdl], and the Transform dialect makes transformation control
programmable [@lucke2025transform]. Composing a complete extension also requires
agreement at the boundaries between such mechanisms.

*Scattered ownership.* Regions, blocks, and functions organize the program
being compiled. They do not determine who owns the fusion rule, its analysis,
or its emitter. When registration and deployment place these parts in separate
packages, one feature acquires several integration boundaries. A useful
ownership unit must follow the capability across stages.

*Coarse updates.* Pipeline order is similarly insufficient to identify affected
work. Editing one operator need not change analyses of a disjoint branch, yet
invalidating a stage suffix reruns both. Dynamic dependencies recover this
distinction in self-adjusting computation [@acar2009selfadjusting] and
interactive development pipelines [@konat2018pie]. For a mutable compiler graph,
the dependencies must also account for replaced entities and published edits.

### 2.3 A Composable Update Model

These observations lead to three design requirements. First, compiler roles
need a shared typed call interface so that analysis, transformation, and
emission compose directly. Second, a cross-stage feature needs an owner with
explicit visibility and dependencies, independent of subject-program
containment. Third, execution needs observed dependencies and effects so that
updates follow changed state rather than stage order.

These requirements meet at publication. Functions compose within an ownership
boundary, but their results remain reusable only while the observed graph state
is current. Graph changes and dependency records must therefore be published together. Section 3 develops compiler
functions, mods, and transactional evaluation around that invariant.

## 3. Design

### 3.1 System Model

Programs and compiler extensions share one typed graph model.
A *subject mod* holds the program being inspected or transformed; an
*extension mod* supplies the compiler functions acting on it. These are roles
within an invocation. Both use the same entity model, type system, and loader.

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

This representation is progressive because each successful compiler
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

<!-- FIGURE 3 PROMPT — Built-in image generation. Final prompt and correction:
Use case: scientific-educational. Create a replacement for Joggle Figure 3, a dense but impeccably organized single-column EuroSys SYSTEM ARCHITECTURE diagram. Image 1 is STYLE reference (new code-rich Figure 4). Image 2 is the OLD architecture to replace, use its concepts, NOT its layout. Portrait near 1250x1500, white background, thin charcoal panel rules, pale blue/teal/lavender fills, small DejaVu Sans Mono code, crisp sharp accurate text. Four aligned horizontal bands, very little wasted space. No large overall title, no fake measurements, no decorative art. Explain organization clearly, not a random collection of internals.

Band (a) "Extensions and subject"
Two evenly balanced compartments separated by a vertical thin rule.
LEFT "Extension mod". Code at top EXACTLY:
mod extension
use base
use ir
Below code, small import arrows from extension folder glyph to base and ir nodes. Two small signature strips labelled "entry signatures":
fn transform(m: Mod) -> bool
fn analyze(m: Mod) -> dict
RIGHT "Subject mod". Code:
mod fixture
use tensor
Below a clearly nested container Mod > Fn > Blk, containing small horizontal graph x,w -> qconv -> bias -> requantize -> relu -> y; b input joins bias. Use tiny node labels and compact shapes. Indicate an Op rectangle, Val circles, and an Attr tag on qconv. Arrows and nesting precise. Show Mod/Fn/Blk labels on containing borders, not giant texts. Tiny tag text "layout: NHWC".

Band (b) "One invocation boundary"
Three small aligned inlet boxes "query (read)" "run (mutate)" "emit (read)" lead into a common "resolve -> evaluate" central strip. Side inlet "source | intrinsic | native" points into evaluate.
Below, two little exact source operations with arrows to next band:
"ir.args(op)" tagged READ, and "ir.replace(m, old, result)" tagged WRITE.
Along bottom of band tiny result labels "Attr" under query, "verified graph" under run, "text / bytes" under emit. No claim these three APIs have identical effects.

Band (c) "Execution state"
Two compartments:
LEFT "Evaluator caches". Three stacked compact rows:
"decoded body" with key "(store, fn, generation, revision)"
"query" with record "(K, result, D)"
"stage" with record "(K, D, W)"
Tiny key underneath "K: environment, store, function, arguments".
RIGHT "Typed graph store". Four small vertical slots labelled Fn, Blk, Op, Val. Connect Op -> Val -> Op along bottom, small reverse arc "users". Show handle "h = (store, slot, generation)" and revision tags "graph | structure | function". No arbitrary numeric revisions or fabricated pc opcodes. Thin READ line from store toward observed record D; thin WRITE line toward graph revision.

Band (d) "Mutation transaction"
A clean left-to-right flow within one band:
"snapshot" -> "execute" -> diamond "verify" -> "commit"
Under commit compact "(G', D', W')". Success labelled check mark. Failure dashed loop from verify back to snapshot labelled "rollback"; rollback restores input state. Tiny footer "ownership | types | def-use". Graph publication and records side by side, not duplicated giant tables.
Maintain style of Image1, not its code transformation layout. Small labels, dense meaningful symbols, aligned panels, unambiguous edges. No marketing claim. No large blocks of narrative.
Correction: remove output labels beneath the API examples; label the forward Val-to-user-Op edge users, with no backwards users arrow.
-->

*Figure 3: Compilation architecture. Mod dependencies organize extensions; typed calls connect subject graphs, evaluator caches, and checked mutation.*

<!-- FINAL STYLE EDIT (built-in image generation; target system-architecture.png;
Figures 1 and 2 are style-only references, not layout templates):
Use case: style-transfer, dense scientific mechanism figure for a single column.
Input image 1 is the EDIT TARGET: preserve its scientific content, graph edges, exact code, exact labels, and portrait dimensions.
Images 2 and 3 are STYLE REFERENCES ONLY: never copy their multi-column thesis layout, developer figures, or pipeline arrangement.
Restyle target to the reference visual language: thin bright cyan/blue dashed outer grouping boundaries, very pale turquoise/blue/lilac flat fills, white inset tiles, compact bold sans-serif panel headings with teal/blue/lilac accent words, small DejaVu Sans Mono for code, thin black directed connectors, outlined graph/node/file/package symbols. No thick black enclosing boxes, no blue gradient title bars, no shadows, no sketch texture, no red numbered badges. Keep high information density and tight spacing. Small black (a),(b),(c) labels are fine. Use coral only for changed/deleted entities, blue for reads, teal for creation; distinguish reuse with fine hatch. Preserve all math, syntax, array dimensions, state relationships and directionality; do not invent data or mechanisms. Compact, elegant, publication-ready. Figure 3. Preserve four stacked panels: extension and subject, invocation, execution state, transaction. Headings may shorten to (a) Mods and graphs, (b) Typed calls, (c) Runtime state, (d) Checked mutation. All cache key fields and graph APIs must stay exact. Turn frame bands into subtle dashed group boxes, not heavy tables.
-->

These relationships determine the design's structure. Section 3.2
defines typed compiler functions; Section 3.3 gives them a mod-scoped
composition boundary. Sections 3.4 and 3.5 then explain how observed
dependencies and transactional graph storage support reuse.

### 3.2 Compiler Functions

Compiler functions provide the programmable interface to the shared graph.
Operator semantics, analyses, transformations, representation conversion, and
artifact generation use the same definition and invocation rules. Consequently,
an extension can compose these roles through calls rather than adapters between
role-specific interfaces.

A compiler function accepts graph handles and ordinary values:

$$
f : (H_1, \ldots, H_n, A_1, \ldots, A_m) \rightarrow R.
$$

Each $H_i$ is a `Mod`, `Fn`, `Blk`, `Op`, or `Val` handle. Each $A_i$ is a
configuration or structural value, such as an integer, type, list, dictionary,
or `Attr`. The result $R$ may itself be a handle, an ordinary value, structured
data, text, or bytes. Handles retain graph ownership and lifetime; ordinary
values remain independent of graph storage.

Figure 4 makes this interface concrete with a quantized convolution extension.
The input chains convolution, bias, requantization, and ReLU. A local helper
checks layouts, intermediate users, and zero points before creating the fused
call. It copies operation attributes, redirects result users, and erases the
matched chain. The output preserves the function signature and quantization
parameters. Its emitter then reads the fused operation's types and attributes
to produce a kernel. All these roles exchange graph handles and owned values
through the same call interface.

<!-- FIGURE 4 PROMPT — Built-in image generation; source-checked against
artifact/extensions/vert-fused-op/reference.jog and
.cache/artifact/paper-code-fusion-unit.json. Figure asset:
paper/figures/figure-03-operator-extension.png.
Use case: scientific-educational. Redesign the attached Joggle compiler figure into a NEW dense portrait single-column EuroSys mechanism diagram with REAL code built into the panels. Image 1 is palette reference only: preserve pale blue, teal, lavender, thin charcoal strokes, white background. Replace the layout entirely; no giant role-table. Produce one crisp high-resolution portrait image, about 1500x1800, tight crop, no outer title, no figure number, no decorative whitespace. Small DejaVu Sans Mono code with excellent accurate glyphs, restrained blue keywords, teal types, coral highlights only on changed lines. Dense yet aligned; three vertically stacked panels with small right-hand graph strips, connected by arrows. ALL text below must be copied faithfully; don't invent API calls, omit a return, change a number, or abbreviate identifiers. Short prose only.

Top header rail contains "mod extension" "use base" "use ir", followed by tiny signature "fn transform(m: Mod) -> bool". Small legend: blue = read, teal = create, coral = replace / erase.
Panel (a) "Input subject". Left 78% width is this COMPLETE subject function (line wrapping permitted, not missing code):
fn subject(
  x: tensor<i8, [1,1,1,2]>,
  w: tensor<i8, [1,1,2,1]>,
  b: tensor<i32, [1]>
) -> tensor<i8, [1,1,1,1]> {
  [layout: "NHWC", kernel_layout: "HWIO"]
  let conv = qconv(x, w)
  let biased = bias(conv, b)
  [acc_scale: 0.25, out_scale: 0.5, output_zero: 0]
  let quantized = requantize(biased)
  [output_zero: 0]
  let output = relu(quantized)
  return output
}
Right-hand narrow strip has a vertical dataflow of four small nodes qconv -> bias -> requantize -> relu, with input x,w to qconv, b to bias, output y below relu. Dashed coral enclosure around four nodes, label "single-use". Clearly show b joins bias, not conv.

Panel (b) "Checked graph edit". Small signature "local fn fuse(m: Mod, relu: Op) -> bool". Three compact guard chips: "NHWC / HWIO", "one user", "same zero point". Below show the following REAL replacement excerpt, marked "replacement excerpt" (not a complete standalone function):
let old = ir.outs(relu)[0]
let inputs = ir.args(conv) + [ir.args(bias)[1]]
let result = ir.call(m, relu,
  ir.find(m, "fused_qconv_relu"), inputs, ir.type(old))
Between this code and next lines insert a narrow process box "copy attributes: conv + requantize" with a small metadata tag symbol. Then actual replacement lines:
assert(ir.replace(m, old, result), "replace-failed")
for op in [relu, quant, bias, conv] {
  assert(ir.erase(m, op), "erase-failed")
}
Blue thin leaders from old to relu output; teal from result to a new fused node; coral short leaders for erased old nodes. Do NOT claim the excerpt includes the entire helper. A tiny right annotation "verify -> commit" is sufficient.

Panel (c) "Output subject". Left 78% complete function, same input and output types:
fn subject(
  x: tensor<i8, [1,1,1,2]>,
  w: tensor<i8, [1,1,2,1]>,
  b: tensor<i32, [1]>
) -> tensor<i8, [1,1,1,1]> {
  [acc_scale: 0.25, kernel_layout: "HWIO",
   layout: "NHWC", out_scale: 0.5, output_zero: 0]
  let fused16: tensor<i8, [1,1,1,1]> =
    fused_qconv_relu(x, w, b)
  return fused16
}
Right narrow graph: x,w,b -> fused_qconv_relu -> y. Tiny document icon below labelled "kernel".
At very bottom one small verification rail:
"x=[1,-2]  w=[2,-3]  b=1  |  acc=9  |  RNE(4.5)=4  |  y=[4]"
Tiny footnote "Subject functions complete; operator declarations supplied by fixture."
No fake performance numbers. No Rust code. No ellipses replacing actual subject statements. No huge labels, no icons unrelated to code. The effect should be a carefully typeset dense academic code-and-graph transformation figure, not a presentation poster.
-->

*Figure 4: Quantized convolution fusion. Complete subject functions surround the replacement excerpt. Fixture declarations are omitted; Appendix C.7 gives the full transformation.*

<!-- FINAL STYLE EDIT (built-in image generation; target figure-03-operator-extension.png;
Figures 1 and 2 are style-only references, not layout templates):
Use case: style-transfer, dense scientific mechanism figure for a single column.
Input image 1 is the EDIT TARGET: preserve its scientific content, graph edges, exact code, exact labels, and portrait dimensions.
Images 2 and 3 are STYLE REFERENCES ONLY: never copy their multi-column thesis layout, developer figures, or pipeline arrangement.
Restyle target to the reference visual language: thin bright cyan/blue dashed outer grouping boundaries, very pale turquoise/blue/lilac flat fills, white inset tiles, compact bold sans-serif panel headings with teal/blue/lilac accent words, small DejaVu Sans Mono for code, thin black directed connectors, outlined graph/node/file/package symbols. No thick black enclosing boxes, no blue gradient title bars, no shadows, no sketch texture, no red numbered badges. Keep high information density and tight spacing. Small black (a),(b),(c) labels are fine. Use coral only for changed/deleted entities, blue for reads, teal for creation; distinguish reuse with fine hatch. Preserve all math, syntax, array dimensions, state relationships and directionality; do not invent data or mechanisms. Compact, elegant, publication-ready. Figure 4. Preserve complete input and output subject code verbatim, all checked-edit code including its excerpt label, layout metadata, numerical example and fixture-declaration note. Keep three vertical steps with code left and graphs right. Do NOT replace any code with ellipses or change tensor shapes. Retain single-use guard, zero point, RNE(4.5)=4. Only restyle frames, typography hierarchy, and grouping.
-->

To invoke a compiler function, the environment resolves its qualified name, visible
mods, explicit generic arguments, parameter types, and result context. The
selected implementation may be a source body, an intrinsic, or a native
binding. In each case, the caller uses the same syntax and receives results
through the same evaluator boundary.

Effect contracts then determine how the resolved call accesses graph state.
A query evaluates against a read-only mod and returns an `Attr`; a run applies
mutating functions within a transaction. An artifact call reads a prepared
graph and returns text or bytes. Thus, `query`, `run`, and `emit` share
resolution and evaluation while enforcing their respective publication rules.
Read-only execution rejects mutation, and a run publishes a verified graph.

Thus, the fusion example uses function composition directly: `transform` calls `fuse` through the same
typed interface used for graph inspection. Mods supply the namespace, visibility,
and dependency boundaries around these functions.

### 3.3 Mods

Where compiler functions define behavior, mods define who owns and exposes it.
A mod groups the functions of a feature with their graph and dependencies. We
model this boundary as

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

Within the validated dependency closure, visibility determines which
functions a dependent mod can call. Public functions cross this boundary,
whereas a `local fn` remains inside its owner. Source fragments can therefore
split a large implementation without creating new lifecycle boundaries.
A separate mod establishes an independent public interface, dependency set,
and installation lifecycle.

Native code follows the same boundary. A body-less declaration names the typed
contract, and a native library owned by that mod supplies its implementation.
The binding is scoped to the owning mod and becomes available through the
declared dependency graph.

The same dependency closure also governs installation and upgrade.
Installation stages a candidate, loads and verifies its dependencies, and
publishes it after validation succeeds. Upgrade additionally checks affected
reverse dependencies. If either operation fails, the installed graph remains
unchanged. The mod is therefore the unit of both composition and evolution.

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

Mods establish composition boundaries; recorded observations establish reuse
boundaries within them. A local graph edit need not invalidate every compiler
result. Instead, reuse depends on the state a call observed, rather than on the
subject mod's whole revision alone.
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

Mutating pipelines require one further step: selection must see the effects
of earlier stages in the same run. Each stage retains its call key, observed
inputs, and changed-function summary. Before executing a stage, the scheduler
checks its key and observations against the current graph. A changed observation
selects that stage; otherwise, its previous record is retained.

This check occurs inside the execution loop, not in a separate preselection
pass. Consequently, a producer that acquires a new write can invalidate its
consumer immediately, even if its previous write set was empty. An unchanged
consumer remains reusable. The report distinguishes observations stale at entry
from those invalidated by an earlier stage, termed upstream misses. Each selected
stage executes its complete function body, so stage boundaries determine the
amount of work repeated. A new subject store starts a cold schedule.

```text
Algorithm 1: Reactive stage execution
Input: environment E, graph G, stages s[1..n], records R[1..n]
Output: success; G and R updated only on success

T <- BeginVerified(E, G)
if T = failure: return false
R' <- R
for i = 1..n:
    if Current(E, G, R[i]): continue
    (ok, D, W) <- EvalVerified(E, s[i], G)
    if not ok:
        Rollback(T)
        return false
    R'[i] <- (Key(E, s[i]), D, W)
Commit(T)
R <- R'
return true
```

`BeginVerified` starts the graph transaction and verifies its input;
`EvalVerified` captures observations at the end of the stage and verifies its output.
Here, the call key identifies the environment, function, and arguments; the
record combines that key with the observed inputs and changed-function summary.

Successful execution publishes graph changes and new records together.
Failure restores mutations and revisions and discards tentative records.
Crucially, later stages do not refresh earlier or reused observations. A later
write to an earlier stage's input therefore remains detectable on the next run;
one invocation follows the given stage order rather than iterating to a fixed point.

Figure 5 connects an API edit to stage execution. Changing convolution
layout invalidates the first stage's recorded read. If that stage updates the
ReLU, the next stage detects the changed operation and executes. The stage
observing the disjoint reduction retains its record. This example separates
actual changes from potential reachability: a selected producer need not
invalidate every consumer.


<!-- FIGURE 5 PROMPT — Built-in image edit, synchronized with sequential validation:
Edit this existing scientific figure, preserving portrait aspect, clean pale blue/teal/lavender/coral palette, aligned three compact panels, small monospace labels, white background and dense single-column layout. This is a precise mechanism correction, not decoration.
(a) Keep graph f Conv->Add->ReLU and disjoint g Reduce, exact edit ir.set(m, conv, "layout", "NHWC"), read ir.meta(conv, "layout"), D1 Conv.layout=NCHW, D2 ReLU,D3 Reduce. Keep unchanged.
(b) Change heading to "Validate and execute in order". Replace table columns with "stage | observation now | action"; rows "s1 | Conv changed | execute", "s2 | ReLU changed by s1 | execute", "s3 | Reduce unchanged | reuse". To right draw ordered s1 -> check D2 -> s2, and hatched s3 reuse. Tiny label over s1 arrow "actual edit". Replace bottom formula ribbon with exact text "check reads -> execute -> verify -> record" and tiny note "selection sees earlier writes". No prior-write-scope formula, no dirty union, no preselection.
(c) Change heading to "Commit or restore". Keep G0 layout=NHWC and rollback to that state. Transaction contains s1 -> verify -> check D2 -> s2 -> verify -> commit arrow to G' and records. Compact small boxes not huge diamond. Show hatched s3 bypass. Published table columns "stage | observed state", rows "s1 | after s1", "s2 | after s2", "s3 | unchanged record". Tiny footer "later edits remain detectable". Absolutely remove phrase observations rebased. No global rebase. Main transaction semantics: failure at either verify restores input graph G0 and retains old records. Include short "failure: restore G0, keep records" below dashed rollback arrow. Avoid putting replay or fixed point claims. No fabricated performance numbers. All text legible with minimal padding, same image dimensions and visual weight as original.
-->

*Figure 5: Reactive updates. Ordered checks expose upstream writes and retain unchanged records. Failure restores the input graph.*

<!-- FINAL STYLE EDIT (built-in image generation; target reactive-update.png;
Figures 1 and 2 are style-only references, not layout templates):
Use case: style-transfer, dense scientific mechanism figure for a single column.
Input image 1 is the EDIT TARGET: preserve its scientific content, graph edges, exact code, exact labels, and portrait dimensions.
Images 2 and 3 are STYLE REFERENCES ONLY: never copy their multi-column thesis layout, developer figures, or pipeline arrangement.
Restyle target to the reference visual language: thin bright cyan/blue dashed outer grouping boundaries, very pale turquoise/blue/lilac flat fills, white inset tiles, compact bold sans-serif panel headings with teal/blue/lilac accent words, small DejaVu Sans Mono for code, thin black directed connectors, outlined graph/node/file/package symbols. No thick black enclosing boxes, no blue gradient title bars, no shadows, no sketch texture, no red numbered badges. Keep high information density and tight spacing. Small black (a),(b),(c) labels are fine. Use coral only for changed/deleted entities, blue for reads, teal for creation; distinguish reuse with fine hatch. Preserve all math, syntax, array dimensions, state relationships and directionality; do not invent data or mechanisms. Compact, elegant, publication-ready. Figure 5. Preserve the corrected sequential mechanism exactly: check observations against current graph, execute and verify each selected stage, then retain per-stage records. DO NOT introduce prior-write-scope preselection or global record rebasing. Keep actual edit s1 -> check D2 -> s2; s3 reuse. Failure restores G0 with NHWC. Exact code ir.set(m, conv, "layout", "NHWC") and ir.meta(conv, "layout"). Keep three vertical panels, shorten headings to (a) Edited graph, (b) Ordered checks, (c) Commit or restore. No other semantic changes.
-->

Fine-grained observations reduce re-execution but add validation and capture
work. Each invocation validates the input graph, checks stage observations in
order, and verifies the result of every executed stage. Verification reuses a
cached result when the graph and environment are unchanged.
Reuse is beneficial when the saved stage work exceeds tracking and validation costs.
Broad graph APIs remain correct but record broad dependencies; narrow APIs can
preserve results across unrelated edits. External mutable state must enter
through typed arguments or an environment change before reuse. Native bindings
exchange scalar or byte attributes and do not receive graph entities, so
graph-dependent work remains in tracked compiler functions. These rules define
the state over which the evaluator can validate reuse.

**Reuse across imports.** Reimporting a model creates a new subject store.
To carry reusable work across this boundary, the compilation driver retains
prepared specializations under a fixed environment and pipeline. Keys combine
function identity, content, and visible callee families with parameter/result
types, generic types, and bound static values. The driver imports signatures before
preparation, so matching does not revisit cached bodies. After selection, it
batch-clones referenced bodies, reconnects their calls, and removes temporary
declarations. It captures the next cache before scalarization and model-wide
memory planning; later phases therefore operate on the new model rather than
stale allocation decisions. Observations thus select stages in a retained
graph, whereas specialization keys transfer prepared bodies across imported
revisions. The graph runtime supports both through typed handles, revisions,
and verified mutation.

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

Plans retain interpreted control flow, failure propagation, and dynamic calls.
Native functions use the same mod-scoped binding and graph API.

At the extension boundary, typed handles represent live graph identity and
`Attr` represents owned, serializable values. Dependency records, execution
plans, transaction journals, and counters remain private.

## 4. Evaluation

### 4.1 Methodology

The evaluation follows a feature from implementation to execution: twelve
native tasks measure extension size and composition; two packages expose
integration and maintenance scope; nine model edit sites measure update cost;
and operator/model execution measures the resulting artifacts.

**Subjects and controls.** Extension and ownership comparisons use Joggle,
MLIR, and xDSL. Each extension follows its system's native API at a pinned
revision. The update comparison uses Joggle, TVM, and ONNX-MLIR on three models
from the 15-model end-to-end corpus. It measures the time to produce a bound
executable after an edit, using the same fixed inputs as the execution study.
End-to-end execution also includes ONNX Runtime.

**Platform.** Compilation and execution measurements use an Apple M4 host
with 24 GiB of memory and macOS 15.7.3. Backends run sequentially with one
execution thread; core placement follows the OS scheduler. For timing studies,
generated C is compiled with Apple Clang 17 at `-O3 -DNDEBUG`.

**Correctness and measurement.** Compiler-extension tasks use build-and-test
oracles; transformations use verification and canonical graph digests;
executable artifacts use reference tensors with dtype-specific tolerances.
Failed cases remain in coverage but do not enter latency ratios. The repeated update
experiment covers three edit sites on each of DenseNet-121, SqueezeNet-1.1,
and TinyYOLOv3, with ten paired repetitions per site. The execution experiment uses ten warm-ups and
100 timed samples per artifact. We form ratios within an edit, model, or
operator before aggregation. The supplement gives per-case measurements,
input/output examples, and compiler configurations.

Figure 6 shows the paired design: shared feature specifications for extension
and ownership, the same edited graph for updates, and identical tensors for
execution. Each path checks correctness before aggregating measurements.

<!-- FIGURE 6 PROMPT — evaluation-workflow.png. Built-in image generation.
Create a NEW original technical figure showing four compiler evaluation protocols. Borrow ONLY this palette: dark blue #0057B8, teal #12B5B0, purple #7928CA, amber #E6AD48. No reference layout is supplied or should be imitated.

ORIGINAL LAYOUT: a compact 2-by-2 set of four experiment vignettes on a single pure white portrait canvas, aspect ratio 4:5. Small neutral black serif panel headings (a), (b), (c), (d). A very thin light-gray vertical divider and a short horizontal divider separate quadrants. NO panel boxes, NO colored panel fills, NO table grid, NO column headings, NO full-width header or footer, NO problem/solution rows, NO repeated rectangular card frames. Each vignette has its OWN scientifically meaningful internal topology with mostly TOP-TO-BOTTOM flow. Precise fine strokes, small black serif/math labels, compact monospace micro-annotations, tiny graph symbols. All forms 2D, flat, fully opaque, white background. No black filled regions, no shadows, no texture, no photographic decoration. Information-dense, almost no empty margins.

TOP LEFT (a) "Native extensions":
Three system names Joggle / MLIR / xDSL converge on one semantic contract, then branch into jog / C++ / Python reference-file symbols. These converge on verify + execute. Show the qconv -> bias -> requant -> relu chain replaced by fused, beside x=[1,-2], w=[2,-3], b=1, acc=9 -> y=4. Finish with a source-lines measuring glyph (schematic equal-height bars without a numeric axis) and “verified references”. Tiny side annotation “12 tasks / 6 roles”. No Agent loop, completion rate, or fabricated size results.

TOP RIGHT (b) "Package footprint":
Two small input symbols at top: an operator graph labelled “low-bit” and a tiny conv->relu graph labelled “fusion”. Under them a small outline package symbol P. From P, visibly fork FOUR independent arrows: one to a tiny plus-marked package labelled “setup”, three to tiny amber-diff packages labelled “Δ1”, “Δ2”, “Δ3”. These are independent changes, not a chain. Each package output connects to a shared oracle diamond with a small checkmark. Below show a very compact four-column tally strip “F  L  Z  R” and directly below “files  lines  zones  publish”. Side annotation “Joggle / MLIR / xDSL”. Include a tiny two-line code diff + / −, no fabricated numeric measurements, no .cpp or .mlir filenames.

BOTTOM LEFT (c) "Executable-ready update":
At top “Joggle / TVM / ONNX-MLIR” in tiny type and a small four-node directed diamond DAG G with one amber node Δ.
Fork the edited graph into TWO VERTICAL TRACKS, left labelled “update”, right “rebuild”. EACH track has a sequence of FIVE tiny rectangular stage glyphs labelled “decode”, “lower”, “emit”, “native”, “bind”. Draw small blue/teal accents along both paths; a small gray prepared-body icon joins ONLY the update track at “lower”, labelled “reuse”. Enclose neither track in a big box. Instead draw slim vertical timing brackets BESIDE the full tracks from decode through bind, labelled Tᵤ and Tᵣ. Each track ends in its own executable symbol. Both converge on an output-check diamond “yᵤ ≈ yᵣ”. Bottom output “Tᵣ / Tᵤ”. Tiny footnote “3 models · 9 sites · 10 pairs”. Do not omit native compile or binding. The reuse icon illustrates Joggle's prepared bodies, not a claim about all backends.

BOTTOM RIGHT (d) "Generated-code execution":
At top a small matrix X, with side note “24 operators / 15 models”. X fans out to FOUR short VERTICAL parallel paths, with narrow backend labels “Joggle”, “ORT”, “TVM”, “ONNX-MLIR” placed horizontally above the paths, do not rotate text. Each path ends in its own small output matrix Y. All four output matrices merge at a diamond “≈”. From the diamond branch downward: checkmark path to a stopwatch icon “latency”; BOTH check and cross paths to four little outlined squares “coverage”. Beneath, tiny micro-annotation “10 warm-ups / 100 samples”. A very small label under the Joggle path “base/opt: operators”. No data charts or made-up numerical results.

The four flow topologies are a checked-reference comparison, an independent-edit FAN-OUT, two paired VERTICAL TIMELINES, and parallel backend EXECUTION PATHS. This is not a capability comparison diagram and must not resemble a 3-column challenge-solution-benefit table. Keep balanced aligned quadrants, small typography, and compact layout. Render only the figure, without caption.
Final correction pass: panel (b) must say low-bit and conv -> relu; branches setup / Δ1 / Δ2 / Δ3 are independent. Panel (c) must name TVM, not MLIR, and end in yu ≈ yr and Tr / Tu. Panel (d) must say 24 operators / 15 models and 10 warm-ups / 100 samples. White opaque background. Do not introduce new systems, populations, or result values.
-->

*Figure 6: Evaluation workflow. Paired inputs and correctness checks connect extension tasks, package edits, executable-ready updates, and execution.*

### 4.2 Cross-Stage Extensions

Twelve tasks cover definition, analysis, rewrite, conversion, emission, and
cross-stage features. Two tasks per family share a semantic contract across
Joggle, MLIR, and xDSL; all 36 reference implementations pass their oracles.

Figure 4 makes composition concrete: fusion replaces the single-use
convolution--bias--requantization--ReLU chain while preserving types and
quantization attributes. Analysis and emission then consume the same graph
handles through ordinary functions in one mod. The unit fixture produces
accumulator 9 and output 4; Appendix C gives complete input/output examples.

**Implementation size.** Figure 7 compares the checked task-specific source files,
counting nonempty physical lines, including imports and comments but excluding
shared drivers and build files. Type definition takes 17 lines versus 52 in
MLIR and 24 in xDSL; quantized-operation definition takes 37, 62, and 38.
Direct typed definitions reduce boilerplate in these tasks. Cross-stage
implementations take 67/90/67 lines for low-bit support and 74/76/70 for
fusion, in the same system order. xDSL is shorter in analysis, rewriting,
conversion, and emission. Source brevity therefore varies by role; the shared
interface combines concise definitions with direct cross-role composition. Appendix C lists source and byte counts;
Section 4.3 includes complete-package integration.

*Figure 7: Native extension size. Nonempty source lines for twelve tasks, including imports and comments. One checked reference implementation per bar.*

An exploratory Qwen3-8B/14B collection yields 42 complete trajectories out of
72 planned conditions, with no full-task completion. Appendix C reports
actual candidates and diagnostics separately from the checked references.

<!-- AGENT DIAGNOSTICS: report observed trajectories, not a new success proxy.
data/agent-diagnostics.csv preserves final outcomes, unchanged edits, protocol
errors, and observed positive/negative fixture counts for the 42 complete runs.
The planned 72-condition success analysis is incomplete; no success-cost bars
are populated from failed or interrupted trajectories. -->

### 4.3 Change Footprint and Ownership

Source size describes an extension's body; ownership concerns its complete
package: implementation, public entry points,
and build or registration declarations. We compare signed low-bit arithmetic
and quantized convolution fusion, each spanning analysis, transformation, and
emission. Separating installation from subsequent edits distinguishes initial
integration cost from recurring maintenance.

Each feature contributes one integration and three independent maintenance
tasks. Low-bit edits change saturation, arithmetic, or nibble order; fusion
edits change rounding, activation bounds, or stride. Every edit starts from
the admitted parent. Oracles check changed and preserved behavior, including
packed-byte padding and shared users; parent controls must fail the changed contract.

For patch $p$, the footprint is

$$
Footprint(p)=(F_p,L_p,Z_p,R_p),
$$

where $F_p$ counts touched package source files, $L_p$ counts added plus
deleted source lines, $Z_p$ counts ownership zones, and $R_p$ counts
changed entry-point and publication source lines under a frozen policy.
Publication declarations contribute to $F_p$ and $L_p$; $R_p$ identifies that
subset. Each native feature package, including a baseline plugin, forms one
ownership zone. Counts exclude tests, fixtures, and shared measurement code.

**Integration and maintenance.** Figure 8 separates initial package setup from
subsequent edits. Native installation uses one source file per Joggle feature
and three per baseline, including publication declarations. The low-bit package
contains 69 lines, compared with 193 in MLIR and 122 in xDSL; the convolution
package contains 77, 178, and 125 lines, respectively. Thus, direct mod publication
reduces the source needed to connect these features to the compiler.

All six maintenance changes touch one file and one ownership zone per system,
without registration edits. Saturation, subtraction, and rounding tie in lines;
nibble order, activation bounds, and stride use fewer lines in the mod
implementations. Counts include patch formatting. Appendix E gives complete
footprints and worked input/output pairs.

![Package integration and maintenance costs.](figures/figure-05-footprint.png)

*Figure 8: Feature integration and maintenance. Columns: integration files, integration lines, maintenance lines. Lines count additions plus deletions. J/M/X: Joggle/MLIR/xDSL.*

### 4.4 Compilation Updates

We next examine the compilation work triggered by a model edit. The measured
endpoint is a bound replacement executable, covering the full path from the
edited graph to runnable code. Each trial replaces one ONNX `Add` with `Sub`
or one `Relu` with `LeakyRelu`, leaving graph edges and model parameters
unchanged. The numerical oracle evaluates the edited model's semantics.

**Update protocol.** Update-to-ready time starts immediately before applying
the edit and includes import, specialization, lowering, emission, native
compilation, and binding. Numerical validation follows this interval and gates
the result:

$$
T_{validated}=T_{ready}+T_{validation}.
$$

Reference outputs are generated outside both intervals; environment setup and
the validated initial build are recorded separately. For system $s$ and edit
$e$, we pair an update with a complete rebuild of the same edited model:

$$
S_{s,e}=\frac{T_{ready,full,s,e}}{T_{ready,update,s,e}}.
$$

Both paths share optimization policy, edits, tensors, and tolerance. Updates
start from a validated original model; rebuilds start in fresh workers.
Native caches remain enabled. TVM retains runtime/process caches;
ONNX-MLIR invokes a fresh compiler from a resident host.

Joggle retains its environment, evaluator plans, and prepared function bodies.
After import, it matches specialization signatures and materializes referenced
cached bodies. Scalarization, model-wide storage planning, emission, native
compilation, and binding remain inside the measured interval. Its matched
rebuild uses the same pipeline with an empty body cache.

**Turnaround and reuse.** Figure 9 reports absolute update latency and paired
rebuild/update speedup, separating cross-system turnaround from reuse within
each system. Across nine edit sites, Joggle reduces executable-ready time on
all three subjects. Median paired speedups are 2.49× for DenseNet-121, 1.46× for
SqueezeNet-1.1, and 1.86× for TinyYOLOv3. Each model contributes three edits
with ten paired repetitions; every successful update matches its rebuild's
output digest.

Absolute median update times are 20.336 s, 1.832 s, and 10.013 s for Joggle.
TVM completes DenseNet and SqueezeNet updates in 10.531 s and 1.070 s;
ONNX-MLIR takes 10.713 s, 2.112 s, and 2.778 s on the three models. Their paired
rebuild/update speedups remain between 0.99× and 1.00×. TVM's TinyYOLOv3 path
rejects an unsupported `Loop`. Thus, Joggle achieves the largest paired reuse
gain. TVM is fastest on DenseNet and SqueezeNet by absolute median update
time; ONNX-MLIR is fastest on TinyYOLOv3.

**Cost breakdown.** Prepared-body reuse accounts for most of the update gain.
DenseNet's median Prepare time falls from 30.631 s to 1.307 s, reducing total
lowering from 38.617 s to 8.539 s. Emission remains near 6.5 s and native
compilation near 2.7 s. SqueezeNet and TinyYOLOv3 show the same pattern:
preparation contracts, while emission and native compilation remain stable.
The supplement reports all 27 edit/system combinations and phase medians.

This breakdown identifies where further reuse would matter most.
Reusing prepared bodies removes repeated lowering work, but emission and native
compilation still run for each executable. As preparation shrinks, those stages
account for more of the update time. The result therefore motivates extending
reuse to artifact construction, rather than further optimizing preparation alone.

**Retained-graph scheduling.** To isolate dependency-directed execution, we also run five metadata-propagation stages on 15 retained model graphs, editing three distinct operations per model. Across 450 affected-edit pairs, reactive execution takes 0.148–52.608 ms at the per-site median and reduces stage-processing time by 18.86× geometrically relative to full traversal. The affected regions span 1–668 operations; hence an early edit need not be the most expensive in a branched graph. All 900 pairs, including unrelated edits, produce matching checked outputs; unrelated edits reuse all five stages. These measurements isolate scheduling and graph processing, while Figure 9 includes artifact construction and native compilation.

*Figure 9: Executable-ready updates. Top: median ready time, IQR. Bottom: paired rebuild/update speedup. Ten repetitions per edit; crosses mark failed compilation.*

<!-- UPDATE-RESULTS FIGURE — Single-column vertical bars, two rows by three columns,
3.33×2.25 inches with 5.5 pt labels. Columns: DenseNet-121, SqueezeNet-1.1, TinyYOLOv3. Top:
absolute ready time for three edit sites, grouped by system and policy; log
axis, hatched rebuild and full-color update. Bottom: paired rebuild/update speedup
at the same edit sites; linear axis and parity at one. Teal Joggle, amber TVM,
blue ONNX-MLIR, with the same system hatches as Figures 9–10. Use lighter
hatched rebuild bars and full-color update bars. Tight axis-label padding,
horizontal multiline edit labels, and compact row spacing enlarge the plot
areas without changing the canvas. Four-sided inward ticks, IQR whiskers, explicit × for
unsupported cases. Ten paired repetitions per edit. CSV and provenance:
paper/data/figure-06-update.*; script: paper/render_update.py. -->

### 4.5 End-to-End Performance

The preceding experiments concern compiler development and update cost.
We now evaluate the resulting executables on 24 operator graphs and 15 models.
The operator suite contains four cases in each of six families: elementwise
chains, reductions, matrix multiplication, convolution, quantization, and
fusion. External baselines are ONNX Runtime's single-thread CPU provider,
TVM Relax's default LLVM CPU pipeline without tuning, and ONNX-MLIR at
`-O3` with parallelism and fast math disabled. Joggle uses input-fixed
entry signatures and the same byte-identical tensors. The operator comparison
also includes Joggle without its optimization pack.

Inputs remain resident during timing. Joggle uses a native C call loop with
caller-owned outputs; the baselines use Python-driven calls with their native
output allocation. The measurements therefore compare deployed invocation
paths, including call and allocation costs, rather than isolated kernel bodies.
These costs matter most for short operators. Appendix B gives the per-backend
boundaries. Input loading, compilation, and reference evaluation are outside
the execution interval.

We report median steady-state execution latency from 100 measurements after
ten warm-ups. The performance figure groups all 24 operators into six panels
with shared logarithmic axes. Each latency is divided by the same-case ORT
median: a ratio below one means faster execution, and a ratio above one means
slower execution. Bars extend from parity to the median ratio; whiskers reach
p95 using the same denominator. Thus, whiskers show timing spread, not
confidence intervals. The model figure uses the same encoding. Cases that
fail compilation or numerical validation remain in the coverage denominator
but contribute no latency ratio.

<!-- PERFORMANCE FIGURE PROMPT — Render from measured CSV using
artifact/figures/figure_07_performance.py. Compact single-column figure with
3.33×2.25-inch canvas and 5.5 pt DejaVu Sans Condensed labels. Use compact
horizontal multiline case labels and axis-attached y labels rather than
distant figure-level text.
Six panels, two rows by three columns: elementwise, reduction, matmul,
convolution, quantization, fusion.
Every panel contains four operators and grouped base/optimized/TVM/ONNX-MLIR bars. Shared
logarithmic y axis, one legend, ORT=1 dashed line, median-to-p95 whiskers,
hatched base bars, solid optimized bars, and dotted amber TVM bars. Mark unsupported
TVM cases with ×, not zero-height bars. Report correct coverage in the
caption. Bars start at parity; use compact wrapped labels and shared axes at
the final column width. Enclose every panel in four thin spines with inward
ticks on all sides; share colors, hatching, and the compact legend with Figure 11.
Source CSV: paper/data/figure-07-operators.csv.
Model measurements have a separate companion display. Preserve every case and failed outcome.
No generated pixels or illustrative numbers for data. -->

*Figure 10: Operator execution relative to ORT. Bars: median; whiskers: p95 (100 samples). Crosses: invalid. DW/PW: depthwise/pointwise; MM: matmul; B/R: bias/ReLU.*

On the 22 operators correct in every configuration, optimized Joggle achieves
$1.35\times$ lower geometric mean latency than default TVM. It has lower
median latency on ten cases, including rectangular and square matrix products.
ONNX-MLIR is $1.55\times$ faster than Joggle in aggregate; ORT is faster than
all three compiler configurations. The family panels locate these differences
rather than reducing every operator to one suite-wide ratio.

Joggle and ORT pass all 24 operators; TVM and ONNX-MLIR each pass 22.
TVM cannot import QLinearConv or QLinearMatMul. ONNX-MLIR cannot lower
QLinearConv, and its QLinearMatMul output fails the integer oracle.
Appendix A gives every operator's latency and distinguishes the full and
jointly correct populations in the aggregate rows.

The optimization pack reduces geometric mean operator latency by
$1.89\times$ across all 24 cases. Rectangular and $256\times256$ matrix
products improve by $22.68\times$ and $12.85\times$, respectively; strided
convolution slows by $1.09\times$. The largest execution gains therefore come
from matrix-product lowering, whereas the update gains arise from preparation
reuse.

Figure 11 extends the external comparison to all 15 models. Joggle passes the
numerical oracle on 14 models, including both EfficientNet quantization
variants and both TinyYOLO models; SSD-MobileNet stops during preparation.
ORT passes on 13 models, and TVM and ONNX-MLIR on 11 each. Correctness is checked
against the unoptimized ONNX graph. ORT's optimized executions of MobileNetV2 and
EfficientNet QDQ exceed the tolerance, so these two models have no ORT-normalized
ratio even where another system produces a correct executable.

Eight models pass in all four systems. On this common set, geometric mean
latency relative to ORT is 25.67× for Joggle, 52.11× for TVM, and 23.84× for
ONNX-MLIR. Thus Joggle runs 2.03× faster than default TVM, while ONNX-MLIR runs
1.08× faster than Joggle; ORT has the lowest aggregate latency. The per-family
panels separate this execution comparison from coverage: a failed candidate
is marked ×, and a correct candidate without a valid ORT reference is marked
with a dash. Absolute medians and p95 values for every correct candidate remain
in the accompanying CSV.

<!-- FIGURE 11 DATA — Single-column 3.33×2.25-inch paired bar plot, 5.5 pt labels, two rows by
three columns. Five panels contain all 15 models grouped as dense CNNs,
mobile CNNs, detectors, quantized models, and other models; the sixth gives
the four-system common-set geometric means. Shared logarithmic latency/ORT
axis, parity at one; teal Joggle, dotted amber TVM, and hatched blue ONNX-MLIR
bars start at parity. ORT is the dashed reference line.
Whiskers extend from median to p95; the aggregate has no timing whisker.
Four thin spines, inward major/minor ticks, compact labels, and one legend.
Explicit × for failed candidates and a dash for correct candidates without a
valid ORT denominator, never zero-valued bars. Aggregate only the eight models
correct in all four systems. All panels share Figure 10's width and height.
CSV: paper/data/figure-07-models.csv; per-model summaries:
paper/data/figure-07-models-summary.csv; script:
artifact/figures/figure_07_models.py. -->

*Figure 11: Model execution relative to ORT. Bars: median; whiskers: p95 (100 samples). Crosses: invalid candidate; dash: invalid reference. Panel (f): eight jointly correct models.*

<!-- PERFORMANCE DATA — Separate operator and model displays form one
end-to-end experiment. Each display has a source CSV and plotting script.
Model CSV: paper/data/figure-07-models.csv. Columns:
subject_kind,subject,subject_hash,family,system,system_revision,variant,
supported,reason,iteration,calls_per_sample,latency_ns,max_abs_error,
max_rel_error,input_digest,output_digest,correct,seed. -->

Taken together, the results separate two benefits: prepared-body reuse reduces
compilation work after an edit, while matrix lowering improves generated code. The model comparison tests
these execution gains at network scale. Keeping these
measurements separate links each gain to the mechanism that produces it.

## 5. Related Work

Table 1 compares capabilities at the extension boundary. Shared typed calls
evaluate all five compiler roles through one call and value model.
Cross-stage packages group these roles under a feature
owner. Read-tracked reuse discovers dependencies from executed reads, while
transactional edits restore graph state after failure. Their combination connects
extension composition to incremental execution.

| Dimension | MLIR [@lattner2021mlir] | xDSL [@fehr2025xdsl] | TVM [@chen2018tvm] | Exo 2 [@ikarashi2025exo2] | Transform [@lucke2025transform] | egg [@willsey2021egg] | egglog [@zhang2023egglog] | rustc [@rustcincremental] | Adapton [@hammer2014adapton] | PIE [@konat2018pie] | **Joggle** |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Primary focus | IR design | IR design | ML compile | Scheduling | Scheduling | Eq. sat. | Eq. sat. | Incr. compile | Incr. eval. | Pipelines | **Compilation** |
| Workload | SSA | SSA | Tensors | Kernels | Payload IR | Expressions | Expr./facts | Rust | General | Builds | **Typed graphs** |
| Extension notation | C++/ODS | Python | Py/C++ | Python | IR/C++ | Rust | Datalog | Rust | Host | PIE | **jog** |
| Extension form | Host API | Host API | API/DSL | Schedule | Schedule IR | Rules | Rules/facts | Query | Thunk | Task | **Typed fn** |
| Composition unit | Dialect | Dialect | IRModule | Library | Sequence | Rule set | Rule set | Query | Thunk | Pipeline | **mod** |
| Shared typed role calls | — | — | — | — | — | — | — | — | — | — | **✓** |
| Cross-stage packages | ✓ | ✓ | ✓ | ✓ | — | — | — | — | — | — | **✓** |
| Read-tracked reuse | — | — | — | — | — | — | — | ✓ | ✓ | ✓ | **✓** |
| Transactional IR edits | ✓ | — | — | — | ✓ | — | — | — | — | — | **✓** |
| Rollback scope | Conversion | — | — | — | Alternative | — | — | — | — | — | **Sequence** |

*Table 1: Extension forms, composition, and reuse across eleven systems.
✓: supplied by the compared extension mechanism; —: not supplied by that
mechanism. Cross-stage packages may use host-language libraries; shared role
calls require a common compiler-evaluated call and value model, not merely a
common host language. Eq. sat.: equality saturation; Incr.: incremental.*

### 5.1 Compiler Construction and Composition

LLVM supplies typed SSA, analyses, and passes [@lattner2004llvm]; MLIR adds
extensible dialects and multi-level lowering [@lattner2021mlir]. IRDL makes
operation, type, and attribute constraints declarative [@fehr2022irdl], while
xDSL brings compatible construction into Python [@fehr2025xdsl]. MLIR also
caches analyses at operation anchors, with invalidation governed by
preservation declarations [@mlirpass].

Staging takes a complementary approach. LMS uses types to stage code
[@rompf2010lms], and AnyDSL specializes higher-order programs by partial
evaluation [@leissa2018anydsl]. Delite shares parallel patterns, optimizations,
and code generators across embedded DSLs [@sujeeth2014delite]; Forge generates
DSL implementations from declarative specifications [@sujeeth2013forge].
The complementary concern here is how these capabilities compose as a feature:
we combine typed calls over mutable graphs with mod-scoped ownership and
observed execution dependencies.

### 5.2 Tensor Optimization and Deployment

Halide separates algorithms from schedules [@ragankelley2012halide], and
TVM combines graph and tensor optimization [@chen2018tvm]. Within this setting,
Ansor searches tensor programs [@zheng2020ansor], and MetaSchedule composes stochastic transformations
[@shao2022metaschedule]. TensorIR exposes computation blocks and scheduling
primitives [@feng2022tensorir], while Relax connects graph, tensor, and external
library abstractions for dynamic workloads [@lai2025relax].

Other systems expose different units of optimization. Lift uses functional
data-parallel patterns [@steuwer2017lift]; TACO compiles dense and sparse tensor
algebra [@kjolstad2017taco]; Tensor Comprehensions combines mathematical
definitions with polyhedral compilation and autotuning [@vasilache2018tc];
Triton exposes tiled kernels [@tillet2019triton]. At graph level, TASO generates
verified substitutions [@jia2019taso], Mirage searches across kernel,
thread-block, and thread levels [@wu2025mirage], and PluS packages expert
optimizations as graph schedules [@wu2025plus].

Deployment introduces further boundaries. Glow separates graph optimization
from address-only lowering [@rotem2019glow], ONNX-MLIR lowers model operations
[@jin2020onnxmlir], and ONNX Runtime partitions graphs among execution providers
[@ortarchitecture]. These systems organize optimization and deployment around program abstractions.
The mod boundary instead groups the compiler functions implementing a feature
across those levels.

### 5.3 Programmable Transformations

Stratego separates rewrite rules from reusable control strategies
[@visser2001stratego], while Nanopass derives checks and traversal support
from explicit source and target languages [@keep2013nanopass]. RISE and
ELEVATE pair computational patterns with composable optimization strategies
[@hagedorn2020elevate]. Exo externalizes accelerator instructions and scheduling
[@ikarashi2022exo]; Exo 2 builds scheduling libraries from actions, inspection,
and cursors [@ikarashi2025exo2]. The Transform dialect represents schedules
as IR with payload handles and effects [@lucke2025transform].

For search-driven transformation, egg combines equality saturation and
e-class analyses [@willsey2021egg]; guided equality saturation narrows search
through intermediate expressions or sketches [@koehler2024guided]; egglog
integrates equality saturation with Datalog and incremental evaluation
[@zhang2023egglog]. Compiler functions extend this programmability to analysis,
conversion, and emission through a shared call boundary with recorded graph reads.

### 5.4 Incremental Execution

Self-adjusting computation and Adapton reuse work through dynamic dependencies
[@acar2009selfadjusting; @hammer2014adapton]. Differential dataflow handles nested
iteration [@mcsherry2013differential]; IncA maintains graph-pattern analyses
[@szabo2016inca].

For build systems, Shake and pluto discover dependencies during execution
[@mitchell2012shake; @erdweg2015pluto], while PIE provides typed, persistent
pipelines [@konat2018pie]. Build Systems à la Carte separates scheduling from
rebuilding [@mokhov2018build]; rustc validates cached queries with red-green
tracking [@rustcincremental]. LLVM ORC materializes symbols on demand [@llvmorc].

For mutable compiler graphs, reuse must additionally follow entity replacement
and verified graph edits. Our evaluator combines typed observations with entity
generations, revisions, and transactional publication; prepared-body signatures
extend reuse across imported revisions.

## 6. Discussion

**Ownership beyond containment.** Mods reduce integration work by
placing cross-stage functions behind one public interface. Once installed,
all three systems support single-file maintenance in the measured tasks.
The distinction is the integration boundary, not the absence of internal
structure: a mod can span source fragments while retaining one public contract.

**Reuse at two boundaries.** Observations preserve results across
unrelated graph edits; specialization signatures transfer prepared bodies
across imports. As preparation shrinks, emission and native compilation occupy
a larger share of turnaround. Reusing those artifacts requires identities
covering bodies, layouts, targets, and dependencies, alongside graph invalidation.

**Consistent publication.** Typed calls connect compiler roles;
transactions keep their graph edits and dependency records consistent.
Ownership determines the available capabilities, while recorded observations
determine whether previous results remain valid. This link between composition
and reuse is the design's organizing principle.

## 7. Conclusion

Joggle combines typed compiler functions, graph-level ownership, and
dependency-directed reuse within a progressive IR. Native extensions use a
common composition interface, and the measured packages require fewer
integration files. Prepared-body reuse accelerates executable-ready updates
by $1.46$--$2.49\times$. These results connect the organization of compiler
capabilities to the cost of evolving them.

## Appendix A. Operator Measurements

The table reports individual operator latencies and matched baseline ratios.
The main evaluation presents aggregate comparisons and their performance implications.

| Operator | ORT (µs) | Base / ORT | Opt / ORT | TVM / ORT | ONNX-MLIR / ORT |
| --- | ---: | ---: | ---: | ---: | ---: |
| conv-depthwise | 41.92 | 3.62 | 4.52 | 0.82 | 3.74 |
| conv-pointwise | 63.83 | 147.99 | 18.03 | 144.27 | 12.65 |
| conv-stem | 178.12 | 4.60 | 9.55 | 2.50 | 5.96 |
| conv-strided | 39.06 | 178.43 | 194.28 | 186.00 | 180.75 |
| ew-affine-1k | 2.80 | 0.12 | 0.09 | 0.33 | 0.21 |
| ew-broadcast-relu | 3.58 | 0.92 | 0.90 | 0.32 | 0.21 |
| ew-chain-64k | 21.55 | 3.47 | 2.71 | 1.32 | 0.55 |
| ew-select-16k | 8.08 | 1.01 | 0.92 | 0.59 | 0.96 |
| fuse-add-relu | 14.11 | 3.93 | 3.89 | 0.85 | 0.52 |
| fuse-conv-bias-relu | 294.42 | 97.82 | 42.30 | 84.67 | 75.86 |
| fuse-matmul-bias-relu | 14.26 | 170.93 | 13.02 | 166.48 | 6.56 |
| fuse-mul-add | 20.34 | 1.26 | 1.27 | 0.83 | 0.47 |
| mm-batched | 10.64 | 70.82 | 72.59 | 69.89 | 6.41 |
| mm-rectangular | 13.23 | 225.36 | 9.94 | 225.47 | 10.36 |
| mm-square-256 | 31.51 | 328.43 | 25.55 | 330.90 | 16.21 |
| mm-square-64 | 4.55 | 20.68 | 1.67 | 20.40 | 1.57 |
| quant-conv | 26.51 | 9.23 | 10.01 | — | — |
| quant-dynamic | 4.82 | 1.49 | 1.49 | 3.15 | 5.24 |
| quant-matmul | 6.15 | 20.66 | 2.39 | — | × |
| quant-qdq-tensor | 3.40 | 0.55 | 0.57 | 0.44 | 0.65 |
| red-l2-last | 15.46 | 1.15 | 1.13 | 1.23 | 1.21 |
| red-max-channel | 59.16 | 10.61 | 10.66 | 3.72 | 2.22 |
| red-mean-spatial | 12.25 | 9.37 | 9.71 | 6.75 | 8.36 |
| red-sum-row | 9.49 | 6.15 | 6.16 | 6.36 | 6.39 |
| Geometric mean, all 24 | — | 9.30 | 4.92 | — | — |
| Geometric mean, common 22 | — | 8.97 | 4.92 | 6.66 | 3.18 |
| Correct operators | 24/24 | 24/24 | 24/24 | 22/24 | 22/24 |

*Table A.1: Operator execution measurements. Joggle revision `cc82ef114093`,
Apple Clang 17.0.0 (`-O3 -DNDEBUG`), ONNX Runtime 1.26.0 CPU with full graph
optimization, TVM revision `c7b458e946bc` (default LLVM), and ONNX-MLIR revision
`4a13c34aa695` (`-O3`, no parallelism or fast math). All use one CPU thread.
Each passing entry uses ten warm-ups and 100 measured samples. Ratios divide
unrounded per-operator medians; values below one indicate lower latency than
ORT. — denotes an unsupported operation; × denotes a failed numerical oracle.
Geometric means use the indicated common population.*

## Appendix B. Model Measurements

Median execution latency is in milliseconds, with 100 measurements after ten
warm-ups. × identifies a candidate without a numerically valid executable.

| Model | ORT p50 | p95 | Joggle p50 | p95 | TVM p50 | p95 | ONNX-MLIR p50 | p95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DenseNet-121 | 18.124 | 20.897 | 707.834 | 717.648 | 1801.893 | 1826.480 | 774.156 | 776.251 |
| EfficientNet-Lite4 int8 | 11.651 | 13.834 | 148.570 | 168.688 | × | × | × | × |
| EfficientNet-Lite4 QDQ | × | × | 133.400 | 135.056 | 707.443 | 718.765 | × | × |
| GoogLeNet | 12.390 | 13.356 | 269.641 | 283.711 | 945.052 | 969.546 | 612.744 | 734.218 |
| MNIST | 0.048 | 0.050 | 0.212 | 0.281 | 0.203 | 0.217 | 0.336 | 0.489 |
| MobileNetV2 | × | × | 97.677 | 105.865 | 181.238 | 182.083 | 23.114 | 23.969 |
| ResNet-18 | 14.358 | 16.206 | 843.252 | 857.060 | 1129.282 | 1156.727 | 908.751 | 910.927 |
| ShuffleNet-v2 | 1.858 | 1.936 | 26.875 | 27.328 | 66.672 | 67.064 | 8.450 | 8.667 |
| SqueezeNet-1.0 QDQ | 2.853 | 3.027 | 56.876 | 57.895 | 164.147 | 165.738 | × | × |
| SqueezeNet-1.1 | 2.263 | 2.437 | 77.975 | 97.759 | 163.041 | 163.570 | 82.522 | 82.958 |
| SSD-MobileNetV1 | 13.056 | 14.254 | × | × | × | × | × | × |
| TinyYOLOv3 | 23.817 | 25.145 | 532.262 | 647.713 | × | × | 1393.870 | 1410.922 |
| TinyYOLOv2 | 19.982 | 21.762 | 502.028 | 510.865 | 2421.420 | 2466.986 | 1825.879 | 1955.957 |
| UltraFace-RFB-320 | 3.414 | 3.778 | 20.823 | 21.128 | × | × | 12.112 | 12.845 |
| XCiT-Tiny | 43.016 | 46.548 | 2957.785 | 3021.850 | 2972.780 | 3007.537 | 318.047 | 321.317 |
| Correct | 13/15 | | 14/15 | | 11/15 | | 11/15 | |

*Table A.2: Per-model median and p95 execution latency in milliseconds;
100 measured samples per valid executable after ten warm-ups.*

| System | Backend or preparation failure | Numerical-oracle failure |
| --- | --- | --- |
| Joggle | SSD-MobileNetV1 | — |
| ORT | — | EfficientNet-Lite4 QDQ; MobileNetV2 |
| TVM | EfficientNet-Lite4 int8; SSD-MobileNetV1; TinyYOLOv3; UltraFace | — |
| ONNX-MLIR | EfficientNet-Lite4 int8; SqueezeNet-1.0 QDQ; SSD-MobileNetV1 | EfficientNet-Lite4 QDQ |

*Table A.3: Excluded model executions and the first terminal oracle phase.*

| System | Optimization | Timed invocation | Output storage |
| --- | --- | --- | --- |
| Joggle | Clang 17; `-O3 -DNDEBUG` | native C harness | caller-allocated buffers |
| ORT 1.26 | CPU provider; full graph optimization | `session.run` | runtime-managed outputs |
| TVM 0.26.dev0 | default Relax/LLVM; no tuning | `invoke_stateful` | VM-managed outputs |
| ONNX-MLIR 0.4.2 | `-O3`; no fast math or parallelism | C ABI `run_main_graph` | allocate new; release previous |

*Table A.4: Execution controls used for Tables A.1 and A.2. Inputs are resident;
one execution thread, ten warm-ups, and 100 timed samples are used throughout.
Joggle times calls in a native C loop; the other systems time invocations from
the Python collector. Numerical checks follow the timed interval.*

## Appendix C. Extension Task Inputs and Outputs

**Native implementation sizes.** The twelve task-specific reference files are measured after oracle admission.
Nonempty physical lines include comments, imports, and embedded source strings;
UTF-8 bytes retain whitespace. Shared drivers, fixtures, and build declarations
are outside this measurement. Each cell describes one checked implementation.
Source sizes are a descriptive addition following inspection of the Agent
collection, rather than a replacement completion score. The CSV records the
source file and its admission report for every implementation.

| Task | Joggle lines | MLIR lines | xDSL lines | Joggle bytes | MLIR bytes | xDSL bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Parametric type | 17 | 52 | 24 | 517 | 2498 | 1011 |
| Quantized operation | 37 | 62 | 38 | 1519 | 2819 | 1983 |
| Broadcast shape | 43 | 33 | 21 | 975 | 1444 | 1059 |
| Numeric range | 56 | 48 | 38 | 1553 | 2108 | 1725 |
| Add-zero rewrite | 38 | 37 | 30 | 1327 | 1679 | 1407 |
| Redundant cast | 47 | 48 | 34 | 1573 | 1931 | 1516 |
| GELU expansion | 38 | 40 | 27 | 1459 | 1655 | 1233 |
| Quantized expansion | 41 | 41 | 26 | 1569 | 1924 | 1403 |
| Graph manifest | 68 | 66 | 33 | 1934 | 3168 | 1740 |
| Kernel wrapper | 12 | 16 | 10 | 538 | 746 | 483 |
| Signed low-bit | 67 | 90 | 67 | 3034 | 3958 | 3230 |
| Quantized fusion | 74 | 76 | 70 | 3476 | 4151 | 3372 |

**Observed agent trajectories.** We summarize the 42 complete records at the
reporting cutoff: 14 Joggle, 13 MLIR, and 15 xDSL trajectories. The remaining
30 conditions comprise 25 not successfully started, four interrupted prefixes,
and one blocked trajectory. These are collection states, not task failures.
No complete trajectory satisfies its full task contract. This descriptive
analysis was added after inspecting the incomplete collection; it does not
replace the planned task-completion endpoint with partial-credit scoring.

Across the complete records, 34 trajectories exhaust the action budget.
The 1,145 responses contain one action-protocol error and 412 unchanged-source
edits. All recorded feedback appears in the subsequent conversation, and no
response ends at its output-length limit. The final candidates satisfy eleven
negative fixtures and no positive fixture. These observations locate the
bottleneck in producing and repairing executable extensions, rather than
decoding the action format.

Representative records expose the distinction. In Joggle interval analysis,
the candidate changes an immutable dictionary declaration to unsupported
`let mut` syntax and repeats the resulting error (C.6). In xDSL graph-manifest
emission, it returns `{"count":0,"values":[]}` instead of the required SSA graph.
In Joggle parametric-type definition, five invalid-parameter fixtures are
correctly rejected, while valid types fail at `base.get`. Thus negative-case
acceptance and process completion are not substitutes for the requested output.

The native reference programs in C.4, C.5, and C.7 supply the checked implementation
examples. Agent candidates are reproduced separately with their actual
diagnostics. Per-trajectory descriptive counts are in
`data/agent-diagnostics.csv`, derived from the primary batch
`main-agents-explicit-20260925-x0f9ieig`; only five task/model groups have complete
records for all three systems at this cutoff.

**Agent protocol.**

| Setting | Value |
| --- | --- |
| Models | Qwen3-8B; Qwen3-14B |
| Sampling | Temperature 0; thinking disabled |
| Action format | JSON object: `inspect`, `edit`, `test`, or `finish` |
| Per-trajectory limits | 30 actions; 32,000 generated tokens |
| Per-request limit | 4,096 generated tokens, capped by the remaining budget |
| Context | Complete history; 131,072-token window |
| Examples | Public fixtures; no task demonstrations |
| Final oracle | Public and held-out fixtures |
| Matched population | 12 tasks × 3 systems × 2 models; one run per condition |
| Condition order | Shuffled once with seed 20260925 |

All systems receive four explicit action templates:
`{"action":"inspect"}`, `{"action":"edit","source":"<complete replacement source file>"}`,
`{"action":"test"}`, and `{"action":"finish"}`. The candidate file is implicit;
tests run all public fixtures. Error feedback names unrecognized fields.
The full matched population uses this explicit protocol; four preliminary
trajectories using an implicit action format are retained separately in the
artifact. Each request includes prior interactions and public-test feedback.
Wall time covers the active action loop, including public tests and request
retries, but excludes interrupted-run downtime and final held-out validation.
Archived transport interruptions resume with the same history and remaining
budget; received answers are never regenerated. Token counts cover received
responses; usage for unanswered requests is unknown.

Table A.5 reproduces the semantic-contract field of each task prompt verbatim,
in quotation marks and italics. The native API card and public fixtures accompany
this field in the agent input. Representative input/output pairs are displayed
separately from the instruction; final scoring also includes held-out cases.

| Family | Task | Task prompt (semantic contract) | Positive input → output | Boundary / negative input → observation |
| --- | --- | --- | --- | --- |
| Definition | `def-parametric-type` | *“Define a native signed fixed-point scalar fx<width,frac>; require 2 <= width <= 32 and 0 <= frac < width. Joggle declares a generic Ty constructor and exports verify(Mod)->bool to validate the imported fixture's parameter types; MLIR exports registerExtension(MLIRContext&) and xDSL exports register(context), installing a parsed, printed, verified extension.fx type. Native function arguments and returns use this type. Preserve both parameters through construction, printing and reparsing. Invalid parameters must fail with invalid-type-parameter and no output IR/JSON. A fixed observer reads round-tripped argument types; candidates do not return answer dictionaries. storage_bits denotes the logical width parameter, not a measured ABI allocation size.”* | `W=8,F=3` → `fx<8,3>`, 8 logical bits | `W=8,F=8` → `invalid-type-parameter`; no output IR |
| Definition | `def-quantized-op` | *“Define qadd on equal-shape i8 tensors with finite positive input/output scales and signed i32 zero points; the result shape equals the operands and its element type is i8.”* | two `4xi8` tensors → `4xi8` | `2x3xi8 + 3x2xi8` → `shape-mismatch` |
| Analysis | `ana-broadcast-shape` | *“Analyze NumPy-style trailing-dimension broadcast compatibility for non-negative extents, padding the shorter shape with leading ones. Each aligned pair is legal when equal or one; when one extent is one, select the other extent, including zero. Reject negative extents.”* | `[2,3,1]`, `[4]` → legal `[2,3,4]` | `[2,3]`, `[4,3]` → conflict axis 1 from end |
| Analysis | `ana-numeric-range` | *“Propagate closed finite real intervals through add, multiply, ReLU, and clamp using endpoint arithmetic; multiplication evaluates all four endpoint products.”* | `mul([-2,3],[-4,5])` → `[-12,15]` | `relu([2,-1])` → `invalid-interval` |
| Rewrite | `rew-add-zero` | *“Replace integer add(x, zero) or add(zero, x) by x when zero is a same-element-type scalar or splat constant and replacement preserves the result shape. For floating add with positive zero, require explicit no_signed_zeros semantics; otherwise preserve the operation. Leave negative-zero constants unchanged. Floating fixtures use finite inputs under round-to-nearest-ties-to-even with unobserved FP exceptions. Remove a matched constant only when it has no remaining uses.”* | f32 `x+0`, `[4]`, NSZ → `x` | f32 `x+0.001` → unchanged |
| Rewrite | `rew-redundant-cast` | *“Remove identity casts and cancel cast(cast(x,A to B),B to A) only for lossless pairs: signed integer widening or finite f32 to f64 widening. Keep narrowing, float-integer, and shape-changing conversions. Types are i8/i16/i32/i64/f32/f64; floating casts use round-to-nearest-ties-to-even. Remove the inner cast only when it has no remaining users. Modify the subject's native SSA graph; cast_A_B calls identify element conversions and tensor result types specify shape.”* | `f32[4]→f32[4]` → eliminated | `i16→i8→i16` → retained |
| Conversion | `con-gelu-expand` | *“Replace native SSA gelu calls with 0.5\*x\*(1+erf(x/sqrt(2))), using declared splat, mul, div, add, and erf targets. splat carries a numeric value attribute. Preserve shape, floating element type, and every output user. Operation ordering and equivalent parenthesizations are unrestricted. Reject other element types with unsupported-element-type and no output IR. Numerical tolerances are rtol=1e-5, atol=1e-6 for f32 and rtol=atol=1e-12 for f64.”* | `gelu`, f32[5] → primitive SSA graph containing `erf`, f32[5] | integer → `unsupported-element-type` |
| Conversion | `con-quant-expand` | *“Convert native i8 tensor qadd calls into two dequantize calls, f32 add, and quantize. qadd carries lhs_scale, rhs_scale, output_scale and zeros=[lhs_zero,rhs_zero,output_zero]; the target dequantize/quantize calls carry scale and zero. Use f32 arithmetic, dequantize(q)=(q-zero)\*scale, and quantize(x)=clip(round_ties_even(x/scale)+zero,-128,127). Preserve tensor shape and all result users. Reject nonpositive scales with invalid-scale and no output IR. Target declarations are provided; no request dictionary is present in the native graph.”* | fixed vector → `[-1,1,-3,127]` | negative scale → `invalid-scale` |
| Emission | `emit-graph-manifest` | *“Read the native typed SSA function subject and emit {schema_version:1, inputs, nodes, outputs}. Inputs are ordered {id,type} records; nodes are calls in block/topological order with {id,op,inputs,results,attrs}. Number nodes n0.. and values v0.., assigning parameter ids first and then call-result ids in result order. Each type is {element,shape}, using -1 for dynamic extents and [] for rank zero. Each result is {id,type}; inputs and outputs reference value ids, preserving repeated operands and return order. attrs is a lexicographically sorted array of [key,value] pairs from user operation attributes, excluding native callee bookkeeping. Preserve scalar and array attribute values. All fixture calls are reachable from returns; repeated emission must be byte-identical. Do not emit source names, parameter nodes, return nodes, declarations, or a request dictionary.”* | `splat→add→relu` → `n0..n2`, `v0..v3` | repeated emission → byte-identical JSON |
| Emission | `emit-kernel-wrapper` | *“Read kernel from request metadata and emit JSON with exactly symbol=task_kernel and source containing a complete C99 translation unit. Export void task_kernel(const float\* input, float\* output, size_t count). Supported kernels are relu (x>0?x:+0, including negative zero mapped to positive zero) and 2\*x+1 (f32 multiply then f32 add, without contraction). Handle arbitrary count, exact in-place operation, and count=0 with null pointers; do not modify nonaliased inputs or access outside count elements. Runtime vectors are not provided to the emitter. The oracle compiles the emitted C with its recorded host compiler and fixed separate driver, then checks exact f32 bits, input preservation, and output guards in separate-buffer and in-place modes.”* | ReLU `[-2,-0,1.5,4]` → `[+0,+0,1.5,4]` | zero count + null pointers → no access |
| Vertical | `vert-int4` | *“Add signed qint4 values in [-8,7], using two's-complement nibbles packed low nibble first. Provide range analysis, lower equal-length elementwise add through i8, saturate each sum to [-8,7], and emit packed bytes. For an odd element count, set the unused high nibble of the final byte to zero. The reported range is the representable qint4 type range, not the observed output range. Reject source literals outside [-8,7] before addition; do not truncate them into nibbles.”* | five values → `[-7,0,-2,7,7]`, bytes `09 7e 07` | literal 8 → `literal-out-of-range` |
| Vertical | `vert-fused-op` | *“Add fused_qconv_relu for NHWC signed-i8 input and HWIO signed-i8 weights, with unit stride and dilation, no padding, and zero input/weight zero points. Compute valid cross-correlation with i32 accumulation and per-output-channel i32 bias; fixtures have no i32 overflow. Requantize each biased accumulator as round_ties_to_even(accumulator \* scales.acc / scales.out) + output_zero, then clamp to [max(-128, output_zero),127] to implement signed-i8 saturation and real-domain ReLU. Scales are finite and positive, and output_zero is in [-128,127]. Match the qconv -> bias -> requantize -> relu chain only when each intermediate result is single-use; preserve a shared chain. Emit one fused_qconv_relu manifest node for a matched chain and execute the fused semantics on runtime inputs.”* | unit kernel → `[4]`, one fused node | shared convolution → original chain preserved |

*Table A.5: Natural-language task inputs, observable outputs, and oracle-facing
edge cases.*

<!-- TABLE A.5 GRAPH PROMPT — Portrait pages. For each task, place its name
and identifier above a full-width quoted, italic semantic-contract prompt;
place the paired input and expected-output DOT diagrams in two columns below.
Keep each prompt attached to its input/output pair across page breaks. Boundary
cases follow the prompt; observed values remain below the diagrams, outside the
quoted instruction. Use the exact contract field in the artifact specification.
Use DejaVu Sans Mono at 8.5 pt at native size, blue inputs, coral matched
operations, teal outputs, and cream constants. Do not enlarge short graphs or
shrink long graphs to fill their cells; fold graph layouts when needed.
All 12 tasks have paired diagrams. Preserve GELU and quantized rounding and
saturation semantics. Keep figures and their annotations separated without
text overlap. Graphs show task-contract expectations. C.4 retains its diagram typography and display sizes.
Files: figures/a5-*-input.dot and figures/a5-*-output.dot. Markdown records
these instructions and the input/output values rather than raster images. -->

Tables A.6–A.7 detail construction and verification; Table A.8 gives the fixture
split for all 12 evaluated tasks. Rejected definition inputs produce no output
IR. Tensor types omit the `tensor<…>` wrapper; bare i8 or f32 denotes a rank-zero
tensor. Axis indices are zero-based.

### C.1. Parametric type definition

| Case | Width | Fraction bits | Expected type | Logical bits | Expected diagnostic |
| --- | ---: | ---: | --- | ---: | --- |
| fx8-3 | 8 | 3 | `fx<8,3>` | 8 | — |
| fx16-7 | 16 | 7 | `fx<16,7>` | 16 | — |
| minimum-integer | 2 | 0 | `fx<2,0>` | 2 | — |
| minimum-fraction | 2 | 1 | `fx<2,1>` | 2 | — |
| maximum-integer | 32 | 0 | `fx<32,0>` | 32 | — |
| maximum-fraction | 32 | 31 | `fx<32,31>` | 32 | — |
| fraction-overflow | 8 | 8 | — | — | invalid-type-parameter |
| width-too-small | 1 | 0 | — | — | invalid-type-parameter |
| width-too-large | 33 | 0 | — | — | invalid-type-parameter |
| negative-width | -2 | 0 | — | — | invalid-type-parameter |
| negative-fraction | 8 | -1 | — | — | invalid-type-parameter |

*Table A.6: `def-parametric-type`: complete inputs and expected outputs.
Width denotes logical bits, not ABI allocation size.*

### C.2. Quantized operation definition

| Case | LHS type | RHS type | Scales (L, R, out) | Zero points (L, R, out) | Expected result / diagnostic |
| --- | --- | --- | --- | --- | --- |
| vector | 4×i8 | 4×i8 | 0.5, 0.25, 0.5 | 0, -3, 0 | 4×i8 |
| matrix | 2×3×i8 | 2×3×i8 | 0.125, 0.125, 0.25 | 2, 2, 1 | 2×3×i8 |
| scalar | i8 | i8 | 1, 1, 0.25 | 0, 0, 0 | i8 |
| empty-axis | 0×3×i8 | 0×3×i8 | 1, 1, 0.5 | 0, 0, 0 | 0×3×i8 |
| signed-zero-point-bounds | 1×2×3×i8 | 1×2×3×i8 | 0.0625, 2, 1 | −2³¹, 2³¹−1, −2³¹ | 1×2×3×i8 |
| shape-mismatch | 2×3×i8 | 3×2×i8 | 1, 1, 0.5 | 0, 0, 0 | shape-mismatch |
| zero-scale | 4×i8 | 4×i8 | 1, 1, 0 | 0, 0, 0 | invalid-scale |
| lhs-zero-scale | 4×i8 | 4×i8 | 0, 1, 1 | 0, 0, 0 | invalid-scale |
| rhs-negative-scale | 4×i8 | 4×i8 | 1, -0.25, 1 | 0, 0, 0 | invalid-scale |
| negative-output-scale | 4×i8 | 4×i8 | 1, 1, -0.5 | 0, 0, 0 | invalid-scale |
| lhs-element | 4×i16 | 4×i8 | 1, 1, 1 | 0, 0, 0 | operand-type-mismatch |
| rhs-element | 4×i8 | 4×i16 | 1, 1, 1 | 0, 0, 0 | operand-type-mismatch |
| rank-mismatch | i8 | 1×i8 | 1, 1, 1 | 0, 0, 0 | shape-mismatch |

*Table A.7: `def-quantized-op`: complete inputs and expected outputs, with defaults
expanded. Scales are finite positive f64 values; zero points are signed i32 values.
This task defines the operation; arithmetic lowering is evaluated separately.*

### C.3. Fixture coverage

| Role | Task | Public | Held-out | Observable output |
| --- | --- | ---: | ---: | --- |
| Definition | `def-parametric-type` | 2 | 9 | Round-tripped type |
| Definition | `def-quantized-op` | 2 | 11 | Result type and attributes |
| Analysis | `ana-broadcast-shape` | 2 | 5 | Broadcast shape |
| Analysis | `ana-numeric-range` | 2 | 8 | Closed interval |
| Rewrite | `rew-add-zero` | 2 | 7 | Rewritten graph |
| Rewrite | `rew-redundant-cast` | 2 | 8 | Rewritten graph |
| Conversion | `con-gelu-expand` | 2 | 5 | Expanded graph and values |
| Conversion | `con-quant-expand` | 2 | 8 | Expanded graph and values |
| Emission | `emit-graph-manifest` | 1 | 7 | Manifest bytes |
| Emission | `emit-kernel-wrapper` | 1 | 6 | Compiled C outputs |
| Vertical | `vert-int4` | 2 | 4 | Packed bytes and range |
| Vertical | `vert-fused-op` | 2 | 4 | Fused graph and values |
| Total | 12 tasks | 22 | 82 | |

*Table A.8: Fixture coverage of the matched task population. Counts are distinct
fixtures per task, shared by all systems and models; final validation uses both
sets. Public fixtures are the first positive and, where present, the first
negative case. The two emission tasks have no negative cases. Full contracts
and representative inputs and outputs appear in Table A.5.*

### C.4. Graph transformations and numerical checks

The following examples connect the natural-language contracts to the expected
graph changes and observable outputs. Teal identifies the replacement, coral
the matched operations, and blue the typed inputs. Values and graph forms are
reference expectations from the task contracts.

| Request | Before | After | Observable result and boundary |
| --- | --- | --- | --- |
| Remove floating add-by-zero under `no_signed_zeros`. | `%x:f32[4] → add(%x,+0) → return` | `%x → return` | All users now read `%x`; `+0.001` retains the add. |
| Cancel a lossless signed-integer cast round trip. | `i8[4] → i16[4] → i8[4]` | `i8[4] → return` | `[-128,-1,0,127]` is preserved; `i16→i8→i16` is retained. |
| Expand GELU into floating-point primitives. | `gelu(%x:f32[5])` | `0.5*x*(1+erf(x/sqrt(2)))` | No `gelu` operation remains; `erf` is present; integer inputs are rejected. |
| Expand quantized addition and preserve i8 saturation. | `qadd(a,b)`, scales 0.5, zeros 0 | Two dequantizations → f32 add → RNE quantization | `[-3,0,5,120]+[2,1,-8,120] → [-1,1,-3,127]`; negative scales are rejected. |
| Fuse a single-use quantized convolution chain. | `qconv → bias → requantize → ReLU` | `fused_qconv_relu` | `x=[1,-2]`, `w=[2,-3]`, bias=1, scales 0.25/0.5: accumulator=9, RNE(4.5)=4, output `[4]`; shared convolution results retain the chain. |

<!-- APPENDIX GRAPH TABLE — Portrait pages. Place each request and check in
a full-width row, followed by paired before/after DOT diagrams in two columns.
Preserve the existing C.4 diagram typography and 6.4 cm × 1.65 cm bounding boxes.
DejaVu Sans Mono; thin charcoal edges; pale input blue, matched coral, replacement
teal. Files: task-{zero,cast,quant,fusion}-{before,after}.dot and
agent-gelu-{before,after}.dot. No image-generation text. -->

The analysis and emission cases below complement the graph transformations.
Outputs are the specified oracle expectations; byte strings are hexadecimal.

| Task / case | Input | Intermediate computation | Expected observable output |
| --- | --- | --- | --- |
| Broadcast / empty axis | `[2,0]`, `[1]` | Align `[2,0]` with `[1,1]` | legal; shape `[2,0]` |
| Interval / mixed product | `[-2,3] × [-4,5]` | Endpoint products `[8,-10,-12,15]` | interval `[-12,15]` |
| Interval / add then ReLU | `[-3,2] + [1,4]` | Sum `[-2,6]`, then clamp below at 0 | interval `[0,6]` |
| Manifest / repeated edges | `add(arg,arg)`; return `[twice,arg,twice]` | `arg=v0`, `twice=v1`, node `n0` | inputs `[v0,v0]`; outputs `[v1,v0,v1]` |
| Wrapper / affine | f32 `[-2,0,1.5,4]` | Separate f32 multiply by 2, then add 1 | `[-3,1,4,9]`; same result in-place |
| qint4 / odd count | `[-8,-1,0,3,7] + [1,1,-2,6,7]` | i8 sums `[-7,0,-2,9,14]`; saturate | `[-7,0,-2,7,7]`; bytes `09 7e 07` |
| qint4 / negative tail | `[-8] + [-8]` | Saturate `-16` to `-8`; zero high nibble | `[-8]`; byte `08` |

*Table A.10: Worked analysis and artifact-output checks. Each row retains the
semantic step between the task input and its expected output.*

### C.5. Executable extension and observed output

The GELU reference implementation makes the graph-editing interface concrete.
The following excerpt is the replacement body of `con-gelu-expand`.
The enclosing function first rejects non-floating inputs; its local `splat`
helper creates a typed constant and attaches its `value` attribute.

```jog
let x = ir.args(op)[0]
let type = ir.type(x)
let half = splat(m, op, type, 0.5)
let one = splat(m, op, type, 1.0)
let root = splat(m, op, type, 1.4142135623730951)
let scaled = ir.call(m, op, ir.find(m, "div"), [x, root], type)
let error = ir.call(m, op, ir.find(m, "erf"), [scaled], type)
let sum = ir.call(m, op, ir.find(m, "add"), [one, error], type)
let hx = ir.call(m, op, ir.find(m, "mul"), [half, x], type)
let result = ir.call(m, op, ir.find(m, "mul"), [hx, sum], type)
ir.replace(m, ir.outs(op)[0], result)
ir.erase(m, op)
changed = true
```

For the `vector-f32` fixture, the input subject returns `gelu(x)` with
`x: tensor<f32, [5]>`. The compiler prints the following subject after the
reference transformation; unchanged operation declarations are omitted.

```jog
fn subject(x: tensor<f32, [5]>) -> tensor<f32, [5]> {
  [value: 0.5]
  let gelu_constant10: tensor<f32, [5]> = splat()
  [value: 1.0]
  let gelu_constant11: tensor<f32, [5]> = splat()
  [value: 1.4142135623730951]
  let gelu_constant12: tensor<f32, [5]> = splat()
  return mul(mul(gelu_constant10, x), add(gelu_constant11, erf(div(x, gelu_constant12))))
}
```

The observer records three constants, two multiplies, division, `erf`, and
addition, all with type f32[5]. All seven reference fixtures pass, including
integer rejection; floating results satisfy the specified numerical tolerance.
These are reference outputs, not Agent completions.

### C.6. Recorded Agent code and diagnostic

The `ana-numeric-range` contract asks: *“Propagate closed finite real intervals
through add, multiply, ReLU, and clamp using endpoint arithmetic;
multiplication evaluates all four endpoint products.”* In the recorded
Qwen3-8B/Joggle trajectory, the final generated candidate was:

```jog
mod extension
use base
use ir

fn analyze(m: Mod) -> dict {
  let subject = ir.find(m, "subject")
  let calls = ir.ops(subject, ["call"])
  let op = calls[0]
  let value = ir.outs(op)[0]
  let type = ir.type(value)
  let dimensions = args(type)[1]
  let element_type = args(type)[0]
  let interval = base.interval(element_type, dimensions)
  let mut result: dict = {}
  result["interval"] = interval
  return result
}
```

The compiler returned `module.jog:14:11: error: expected '='` with empty stdout.
The unmodified candidate exhausted 30 actions after 15 edits and 2,189 generated
tokens. `let mut` is invalid; mutable bindings use `var`. Separately, the
reference produces `{"interval": [-12.0, 15.0]}` for `[-2,3] * [-4,5]`.

<!-- Code evidence: artifact/extensions/con-gelu-expand/reference.jog:24-36;
.cache/artifact/paper-code-check-gelu.json and paper-code-check-range.json.
Agent evidence: main-agents-explicit-20260925-x0f9ieig/
03-Qwen3-8B-Joggle-ana-numeric-range/{candidate.jog,final-oracle.json,result.csv}.
Reference execution outputs and Agent-generated candidates are distinct. -->

### C.7. Complete fusion transformation

Figure 4 uses this complete transformation entry point and its local helper
from `vert-fused-op`. The subject supplies operator declarations; the extension
imports graph access through `ir`. Layout, use-count, and zero-point checks
precede mutation. Attribute copying retains the convolution layout and
requantization parameters on the fused call.

```jog
mod extension
use base
use ir

local fn fuse(m: Mod, relu: Op) -> bool {
  if ir.callee(relu) != "relu" { return false }
  let quant = ir.def(ir.args(relu)[0])
  if !ir.live(quant) || ir.callee(quant) != "requantize" { return false }
  let bias = ir.def(ir.args(quant)[0])
  if !ir.live(bias) || ir.callee(bias) != "bias" { return false }
  let conv = ir.def(ir.args(bias)[0])
  if !ir.live(conv) || ir.callee(conv) != "qconv" { return false }
  for op in [conv, bias, quant] {
    if len(ir.users(ir.outs(op)[0])) != 1 { return false }
  }
  if str(ir.meta(conv, "layout")) != "NHWC" || str(ir.meta(conv, "kernel_layout")) != "HWIO" { return false }
  if ir.meta(relu, "output_zero") != ir.meta(quant, "output_zero") { return false }
  let old = ir.outs(relu)[0]
  let inputs = ir.args(conv) + [ir.args(bias)[1]]
  let result = ir.call(m, relu, ir.find(m, "fused_qconv_relu"), inputs, ir.type(old))
  assert(ir.rename(m, result, "fused", ir.key(result)), "binding-failed")
  for op in [conv, quant] {
    let meta = ir.meta(op)
    for key in keys(meta) {
      assert(ir.set(m, ir.def(result), key, base.get(meta, key)), "attribute-write-failed")
    }
  }
  assert(ir.replace(m, old, result), "replace-failed")
  for op in [relu, quant, bias, conv] { assert(ir.erase(m, op), "erase-failed") }
  return true
}

fn transform(m: Mod) -> bool {
  var changed = false
  for op in ir.ops(ir.find(m, "subject"), ["call"]) {
    if fuse(m, op) { changed = true }
  }
  return changed
}
```

For the unit-kernel case, the native oracle executes the emitted kernel with
`x=[1,-2]`, `w=[2,-3]`, and bias 1. The accumulator is
$1\cdot2+(-2)(-3)+1=9$. With accumulator scale 0.25, output scale 0.5,
and zero point 0, ties-to-even rounding gives output 4. The complete reference
passes all six fusion fixtures; a shared intermediate retains the original
chain. The graph transformation and emitted-kernel checks use the same frozen
compiler as the extension study.

<!-- UPDATE DATA BEGIN -->

## Appendix D. Repeated Model Updates

Ten paired repetitions at each of nine edit sites. Times are seconds; brackets give the interquartile range. Speedup is the median of paired rebuild/update ratios. Each pass count covers ten rebuilds and ten updates.

| Model | Edit node | System | Rebuild [Q1, Q3] | Update [Q1, Q3] | Speedup | Pass |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| DenseNet-121 | 5-add | Joggle | 51.029 [49.521, 51.807] | 20.420 [20.109, 20.568] | 2.49× | 20/20 |
|  |  | TVM | 10.413 [10.334, 10.531] | 10.531 [10.469, 10.628] | 0.99× | 20/20 |
|  |  | ONNX-MLIR | 10.651 [10.623, 10.812] | 10.729 [10.700, 10.832] | 1.00× | 20/20 |
|  | 456-relu | Joggle | 51.308 [50.231, 53.704] | 20.511 [20.296, 20.831] | 2.46× | 20/20 |
|  |  | TVM | 10.379 [10.279, 10.537] | 10.531 [10.488, 10.710] | 0.98× | 20/20 |
|  |  | ONNX-MLIR | 10.803 [10.738, 10.848] | 10.703 [10.646, 10.761] | 1.01× | 20/20 |
|  | 907-relu | Joggle | 50.236 [49.568, 51.507] | 20.217 [19.915, 20.802] | 2.48× | 20/20 |
|  |  | TVM | 10.536 [10.440, 10.565] | 10.536 [10.457, 10.643] | 0.99× | 20/20 |
|  |  | ONNX-MLIR | 10.722 [10.688, 10.766] | 10.702 [10.672, 10.745] | 1.00× | 20/20 |
| SqueezeNet-1.1 | 1-relu | Joggle | 2.684 [2.663, 2.705] | 1.829 [1.820, 1.858] | 1.46× | 20/20 |
|  |  | TVM | 1.068 [1.066, 1.083] | 1.073 [1.068, 1.091] | 1.00× | 20/20 |
|  |  | ONNX-MLIR | 2.138 [2.086, 2.216] | 2.102 [2.081, 2.165] | 1.01× | 20/20 |
|  | 34-relu | Joggle | 2.675 [2.648, 2.758] | 1.837 [1.825, 1.873] | 1.45× | 20/20 |
|  |  | TVM | 1.076 [1.074, 1.085] | 1.073 [1.067, 1.092] | 1.00× | 20/20 |
|  |  | ONNX-MLIR | 2.121 [2.107, 2.155] | 2.158 [2.111, 2.179] | 1.00× | 20/20 |
|  | 63-relu | Joggle | 2.679 [2.653, 2.705] | 1.828 [1.823, 1.844] | 1.46× | 20/20 |
|  |  | TVM | 1.076 [1.067, 1.082] | 1.069 [1.066, 1.075] | 1.00× | 20/20 |
|  |  | ONNX-MLIR | 2.095 [2.069, 2.142] | 2.091 [2.084, 2.115] | 1.00× | 20/20 |
| TinyYOLOv3 | 176-add | Joggle | 18.723 [18.525, 18.984] | 10.055 [9.955, 10.149] | 1.86× | 20/20 |
|  |  | TVM | × | × | --- | 0/20 |
|  |  | ONNX-MLIR | 2.737 [2.727, 2.745] | 2.771 [2.720, 2.807] | 0.99× | 20/20 |
|  | 238-add | Joggle | 18.573 [18.280, 18.693] | 10.078 [10.001, 10.170] | 1.83× | 20/20 |
|  |  | TVM | × | × | --- | 0/20 |
|  |  | ONNX-MLIR | 2.749 [2.724, 2.825] | 2.746 [2.726, 2.793] | 1.00× | 20/20 |
|  | 260-add | Joggle | 18.504 [18.270, 18.652] | 9.901 [9.772, 9.982] | 1.87× | 20/20 |
|  |  | TVM | × | × | --- | 0/20 |
|  |  | ONNX-MLIR | 2.752 [2.724, 2.776] | 2.858 [2.776, 2.903] | 0.97× | 20/20 |

Joggle phase medians in seconds over 30 runs per model and policy. Prepare is a component of Lower; columns have separately computed medians. Decode includes input specialization; CC is native compilation.

| Model | Policy | Decode | Parse | Lower | Prepare | Emit | CC | Bind |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DenseNet-121 | rebuild | 1.654 | 0.524 | 38.617 | 30.631 | 6.640 | 2.735 | 0.634 |
| DenseNet-121 | update | 1.722 | 0.535 | 8.539 | 1.307 | 6.525 | 2.708 | 0.256 |
| SqueezeNet-1.1 | rebuild | 0.328 | 0.078 | 1.401 | 0.947 | 0.299 | 0.299 | 0.247 |
| SqueezeNet-1.1 | update | 0.328 | 0.078 | 0.583 | 0.169 | 0.284 | 0.298 | 0.248 |
| TinyYOLOv3 | rebuild | 1.884 | 0.570 | 13.462 | 10.834 | 1.822 | 0.474 | 0.255 |
| TinyYOLOv3 | update | 1.944 | 0.572 | 4.885 | 2.357 | 1.787 | 0.471 | 0.257 |

### D.1 Retained-Graph Scheduling

Five metadata-propagation stages run on each retained graph. Standard ONNX shape inference supplies top-level value annotations without changing operators or tensors. Early, middle, and late select three distinct eligible operations in graph order, not three equal-sized dependency cones. Each site has three warm-ups and ten measured repetitions for affected and unrelated edits. Timing includes stage selection, execution, and in-call verification; import, initial preparation, and edit application precede timing. The final whole-mod check and output digest run outside the timed interval. Paired output checks cover derived metadata and the root type. This controlled mechanism study measures no native code generation or inference execution.

Times below are medians in milliseconds. Each site shows its affected operation count (n), full traversal (F), and reactive update (U). Idle is the median across 30 unrelated updates; all five stages are reused. Total operations include graph support operations as well as model operators.

| Model | Total ops | Early n / F / U | Middle n / F / U | Late n / F / U | Idle ms |
|---|---:|---:|---:|---:|---:|
| densenet-12 | 5516 | 668 / 161.797 / 52.608 | 335 / 166.545 / 26.520 | 1 / 160.577 / 0.210 | 1.868 |
| efficientnet-lite4-11-int8 | 3679 | 122 / 104.327 / 9.638 | 116 / 100.127 / 9.519 | 1 / 99.985 / 0.185 | 0.662 |
| efficientnet-lite4-11-qdq | 4318 | 356 / 126.856 / 28.406 | 181 / 127.579 / 15.456 | 1 / 125.720 / 0.189 | 0.994 |
| googlenet-12 | 907 | 143 / 26.133 / 11.952 | 65 / 24.998 / 5.404 | 1 / 25.105 / 0.166 | 0.360 |
| mnist-8 | 69 | 3 / 2.057 / 0.291 | 6 / 2.173 / 0.526 | 1 / 2.162 / 0.148 | 0.024 |
| mobilenetv2-7 | 1655 | 155 / 46.081 / 12.229 | 78 / 46.002 / 6.346 | 1 / 45.421 / 0.176 | 0.450 |
| resnet18-v1-7 | 643 | 69 / 18.082 / 5.801 | 30 / 17.790 / 2.454 | 1 / 17.875 / 0.154 | 0.178 |
| shufflenet-v2-12 | 1843 | 229 / 52.591 / 19.050 | 115 / 52.263 / 9.626 | 1 / 51.507 / 0.167 | 0.615 |
| squeezenet1.0-13-qdq | 1325 | 119 / 37.272 / 10.203 | 60 / 37.433 / 4.974 | 1 / 37.789 / 0.164 | 0.324 |
| squeezenet1.1-7 | 412 | 66 / 11.646 / 5.656 | 33 / 11.495 / 2.758 | 1 / 11.525 / 0.161 | 0.191 |
| ssd-mobilenetv1-12 | 13863 | 277 / 113.313 / 21.488 | 165 / 112.546 / 13.004 | 1 / 114.585 / 0.191 | 0.958 |
| tiny-yolov3-11 | 1644 | 236 / 46.603 / 17.104 | 21 / 47.257 / 1.557 | 1 / 46.565 / 0.156 | 0.130 |
| tinyyolov2-8 | 285 | 33 / 8.063 / 2.880 | 17 / 8.040 / 1.429 | 1 / 8.099 / 0.163 | 0.104 |
| ultraface-rfb-320 | 1586 | 195 / 44.191 / 15.930 | 96 / 44.915 / 7.758 | 1 / 44.107 / 0.175 | 0.521 |
| xcit-tiny-12-p8-224-opset17 | 3298 | 7 / 109.996 / 0.645 | 453 / 108.807 / 35.100 | 1 / 109.593 / 0.192 | 0.046 |

Source: `data/reactive-scheduler.csv` (1,800 rows, 900 matched pairs); preparation provenance: `data/reactive-input-preparation.json`. Reproduce with `paper/measure_scheduler.py` using the artifact environment. The 18.86× geometric mean pools 450 paired affected-edit ratios with equal weight.

Actual edit locations below use `operator@index` in the imported main function; the common `onnx.` prefix is omitted. Locations remain fixed across the ten repetitions.

| Model | Early | Middle | Late |
|---|---|---|---|
| densenet-12 | `Conv@4605` | `Add@5060` | `Conv@5514` |
| efficientnet-lite4-11-int8 | `Transpose@3556` | `QLinearConv@3562` | `Softmax@3677` |
| efficientnet-lite4-11-qdq | `Transpose@3778` | `QuantizeLinear@4047` | `Softmax@4316` |
| googlenet-12 | `Conv@763` | `Conv@834` | `Softmax@905` |
| mnist-8 | `Reshape@56` | `Add@62` | `Add@67` |
| mobilenetv2-7 | `Conv@1499` | `Conv@1576` | `Reshape@1653` |
| resnet18-v1-7 | `Conv@573` | `Conv@607` | `Gemm@641` |
| shufflenet-v2-12 | `Conv@1581` | `Constant@1711` | `Gemm@1841` |
| squeezenet1.0-13-qdq | `QuantizeLinear@1153` | `MaxPool@1238` | `Reshape@1323` |
| squeezenet1.1-7 | `Conv@345` | `Conv@378` | `Reshape@410` |
| ssd-mobilenetv1-12 | `Cast@1857` | `Clip@2027` | `Cast@2217` |
| tiny-yolov3-11 | `Conv@1348` | `Sub@1447` | `Identity@1626` |
| tinyyolov2-8 | `Mul@251` | `LeakyRelu@267` | `Conv@283` |
| ultraface-rfb-320 | `Conv@1343` | `Relu@1452` | `Concat@1584` |
| xcit-tiny-12-p8-224-opset17 | `Identity@1963` | `Constant@2631` | `Gemm@3296` |

<!-- UPDATE DATA END -->

## Appendix E. Native Package Changes

### E.1. Complete Footprints

Each tuple reports touched package files, added plus deleted source lines, and
entry-point/publication lines $(F,L,R)$. Every row has one ownership zone in
each system. Integration starts from an absent package; maintenance starts
from that feature's original admitted package. Cases and runtime probes are
per system. Parent failures count positive fixtures that the original package
does not satisfy under the changed contract; `—` denotes no parent comparison
for initial integration. Counts describe these implementations, including
their source formatting.

| Feature / change | Joggle (F,L,R) | MLIR (F,L,R) | xDSL (F,L,R) | Cases | Runtime probes | Parent failures |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Low-bit / integrate | 1,69,0 | 3,193,100 | 3,122,49 | 6 | 1,029 | — |
| Low-bit / symmetric saturation | 1,2,0 | 1,2,0 | 1,2,0 | 6 | 1,029 | 5 |
| Low-bit / subtraction | 1,8,0 | 1,8,0 | 1,8,0 | 6 | 1,029 | 5 |
| Low-bit / high-nibble-first | 1,2,0 | 1,4,0 | 1,4,0 | 6 | 1,029 | 4 |
| Convolution / integrate | 1,77,0 | 3,178,100 | 3,125,49 | 6 | 165 | — |
| Convolution / ties-away | 1,2,0 | 1,2,0 | 1,2,0 | 10 | 297 | 3 |
| Convolution / ReLU6 | 1,4,0 | 1,7,0 | 1,8,0 | 16 | 462 | 9 |
| Convolution / stride two | 1,7,0 | 1,10,0 | 1,10,0 | 11 | 297 | 4 |

*Table A.15: Complete package footprints. All 24 changed packages pass their
oracles. Parent-failure counts agree across the three systems. The source CSV
retains added and deleted line counts separately.*

### E.2. Native Installation Boundaries

| System | Deployed source | Responsibility | Discovery / execution |
| --- | --- | --- | --- |
| Joggle | `module.jog` | Feature functions and public interface | Native mod installation; named function calls |
| MLIR | `reference.cpp` | Analysis, transformation, emission | Called by registered passes |
| MLIR | `plugin.cpp` | Pass adapters and plugin entry point | `mlirGetPassPluginInfo` |
| MLIR | `CMakeLists.txt` | Shared-library build and installation | Installed pass plugin loaded by `mlir-opt` |
| xDSL | `reference.py` | Analysis, transformation, emission | Called by pass and target adapters |
| xDSL | `plugin.py` | Pass, target, and Universe definitions | Native pass/target discovery |
| xDSL | `pyproject.toml` | Wheel metadata and Universe entry point | Installed wheel discovered by `xdsl-opt` |

*Table A.16: The same file organization is used for both feature packages.
All files of one feature share one ownership zone. No compiler-host source is
modified for installation or maintenance.*

### E.3. Worked Maintenance Inputs and Outputs

The following rows use recorded fixture inputs and first-probe observations.
All three changed implementations produce the shown results. Packed bytes are
hexadecimal; convolution tensors use NHWC input and HWIO weights. Scalar
convolution examples have a unit spatial kernel and zero bias. Their scale
ratio is $r=s_{acc}/s_{out}$ and their output zero point is $z$.

| Change / fixture | Input and new contract | Original package | Changed package |
| --- | --- | --- | --- |
| Symmetric / `negative-tail` | `a=[-8]`, `b=[-8]`; clamp to `[-7,7]` | Value `[-8]`; byte `08` | Value `[-7]`; byte `09` |
| Subtraction / `odd-count` | `a=[-8,-1,0,3,7]`, `b=[1,1,-2,6,7]`; `qint4_sub(a,b)` | Subtraction remains unlowered; emission rejected | `[-8,-2,2,-3,0]`; bytes `e8 d2 00` |
| Nibble order / `odd-count` | Same vectors; retain saturated addition; first lane in high nibble | `[-7,0,-2,7,7]`; bytes `09 7e 07` | Same values; bytes `90 e7 70` |
| Ties-away / `half-tie-1` | `x=1`, `w=1`, `r=0.5`, `z=-4`; round half away from zero | `round_even(0.5)-4 = -4` | `round_away(0.5)-4 = -3` |
| ReLU6 / `cap-12` | `x=12`, `w=1`, `r=2`, `s_out=0.5`, `z=-3`; cap real output at 6 | Quantized output `21` | Quantized cap `6/0.5-3 = 9`; output `9` |
| Stride two / `stride2-0` | Input `X` and weights `W` below; bias `-4`, `r=0.5`, `z=-3` | Writes a `4×4` output; contracted output guard rejects it | `2×2` output `[[-3,-1],[-1,-3]]` |

*Table A.17: Changed semantics and paired parent outcomes. Rejection denotes a
recorded lowering/emission or output-extent failure, not a missing run.*

For `stride2-0`, the batch and channel dimensions are one. The full spatial
input and kernel are:

```text
X = [ -3 -7 -7 -6  0  2 ]     W = [ 1 2 -1 ]
    [  2  0  6 -8 -4  2 ]         [ 3 0 -2 ]
    [ -4  0 -8  3 -4 -5 ]
    [  1 -3  0 -2  2  5 ]     stride = [2,2]
    [  6 -6  4  7 -3 -2 ]     Y = [-3 -1; -1 -3]
```
