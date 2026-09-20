---
title: Mod catalogue
description: Responsibilities and public surfaces of Joggle's bundled packages.
---

# Bundled mod catalogue

This page groups installed packages by responsibility. Use
`joggle mod info NAME -M build/modules` as the authoritative declaration list
for the exact build on disk.

### `base`

`base` is the compile-time standard library. It supplies collection and
attribute operations, structural type constructors, canonical text utilities,
and the generic operator declarations used by overload resolution. It should
not acquire neural-network or target knowledge.

### `ir`

`ir` is the sole program-editing surface for `.jog` modules. Its API exposes:

- collections such as `ir.fns`, `ir.blks`, `ir.ops`, and `ir.vals`;
- symbol and type queries such as `ir.find`, `ir.resolve`, `ir.owned`, and
  `ir.type`;
- stable in-module identities for values and operations through `ir.key`;
- metadata queries that preserve extension-owned keys;
- reverse dependency lookup through `ir.users` and the transitive
  `ir.affected` consumer cone;
- construction and editing through `ir.call`, `ir.constant`, `ir.loop`,
  `ir.branch`, `ir.clone`, `ir.move`, `ir.replace`, and `ir.args`;
- body reuse through `ir.expand` and `ir.fold`;
- policy invocation through `ir.invoke`.

`ir.fns(m)` reflects every function in the concrete program `m`, including
local functions because they are part of that program's editable structure.
When a body imported from another module is expanded, its private helper
closure is resolved in the source module, inlined transactionally, and removed.
This preserves module privacy and lexical overload selection without storing a
second symbol table in the IR or exposing implementation helpers to targets.
`ir.fns("name")` instead enumerates an installed package and therefore returns
only its public functions. The C++ `Mod::fns()` and `Env::fns(name)` forms obey
the same boundary.

`ir.owned(m, f)` tests handle ownership directly. It is constant-time and is
preferred to scanning `ir.fns(m)` when a metaprogram only needs to distinguish
a function defined by `m` from an environment declaration.

`ir.ops(subject, kinds)` returns the stable recursive operation order filtered
by any of `call`, `constant`, `loop`, `branch`, `return`, or `yield`; an empty
kind list returns an empty operation list. Filtering happens while native
handles are collected, so a metaprogram need not interpret one predicate per
irrelevant operation. The unfiltered overload remains the right choice when
operation order across every kind is part of the algorithm.

`ir.calls(m, ops, symbols)` selects live calls by their written callee or
resolved symbol, including the ordinary `base.` spelling equivalence. The
caller still supplies the allowed-symbol policy; the native primitive only
performs resolution and filtering.

`ir.path(m, op)` returns the structural identity of an operation as alternating
operation and child-block indices from its function body. `ir.at(m, fn, path)`
resolves that identity. Paths survive metadata and value edits and canonical
print/reparse when structure is unchanged, allowing recursive affected regions
to be named without relying on process-local handles.

`ir.unused(ordered, removable)` computes a reverse transitive unused closure
inside one block. The caller supplies the stable operation order and the exact
set it permits removal from; the primitive contributes no purity or side-effect
policy. This keeps DCE policy in `.jog` while avoiding interpreted user-walks
for every candidate result.

All mutations are checked against handle ownership and take part in the caller's
transaction. A higher-level module should build on these operations instead of
requiring a new native binding for each transformation.

`ir.affected(values)` follows the maintained def-use index from changed values,
includes nested bodies once their owning control operation is affected, and
returns each live consumer operation once in stable identity order. It does not
run, invalidate, or verify those operations. This separation makes the index a
testable scheduling primitive rather than a hidden incremental compiler claim.

## Semantic modules

### `tensor`

`tensor` owns the `tensor<E, S>` constructor and reusable tensor algebra.
Shapes and element types remain structural values. Indexing, reshape, broadcast,
permutation, concatenation, reduction, and matrix multiplication have ordinary
function signatures; inspectable bodies can be expanded when a target needs
lower-level computation.

This is the common substrate for imported networks and user kernels. It is not
an ONNX or TFLite operator catalogue.

### `nn`

`nn` gives frontend-neutral names and bodies to common inference operations:
convolution, bias and activation, pooling, normalization, linear algebra,
elementwise functions, and softmax. Overloads capture layout, shape, and
optional parameters without creating operation classes.

An `nn` call may remain compact for graph rewrites or be exposed into tensor
and scalar computation. New implementations may be added as overloads or
selected through open metadata; a target consumes the resolved call rather
than a hard-coded neural-network enum.

### `quant` and `math`

`quant` expresses quantized values and conversions with tensor functions.
`math` supplies scalar operations required by exposed neural-network bodies.
Keeping both independent prevents a frontend schema or target emitter from
becoming the semantic definition.

`tensor` also owns the minimal runtime-shape vocabulary. `make`, `view`, and
`dim` consume ordinary values and preserve the existing `tensor<E, S>` type
constructor; `write` is an inspectable dense-slice body. These functions let a
semantic module express scans, filtering, and other variable-result algorithms
without defining a dynamic buffer class. Static-only targets intentionally
reject unresolved runtime allocation until an explicit bounding and storage
preparation policy has run.

`slice` follows the same rule for both constant and runtime controls.  Its
shared body normalizes positive and negative steps, clamps bounds, derives the
logical result extents, allocates from the source-shape capacity, copies by
ordinary coordinate arithmetic, and returns a `view`.  The three-, four-, and
five-input forms are overloads of that body rather than frontend or emitter
cases.  Static shape bookkeeping is marked for normal specialization; dynamic
starts, ends, axes, and steps remain ordinary runtime values.

`broadcast` likewise has one overload for a structural target shape and one
for a runtime shape tensor.  The runtime body checks compatibility, constructs
the logical extents, and performs the same trailing-axis coordinate mapping as
the static body.  A frontend therefore passes a dynamic shape value through;
it does not ask an emitter to implement an `Expand` opcode.

`range` constructs a variable-length tensor from scalar start, stop, and step
values.  One generic arithmetic body handles integer, floating-point,
ascending, descending, and fractional sequences through normal scalar
overload resolution.  The implicit form uses runtime-shaped storage;
`range<C>(...)` states a compile-time capacity for deterministic targets and
checks the runtime count against it.  Capacity is therefore a selected policy
at the semantic boundary, not a hidden machine limit or a backend opcode.

Runtime coordinate linearization is the tensor overload
`offset(shape, coordinates)`.  It complements the structural-shape overload
and lets an ordinary function body address partially dynamic tensors without
introducing a layout operation kind.  The sizes-driven `nn.resize2d` body uses
this mechanism for asymmetric nearest-neighbor and bilinear interpolation;
the interpolation mode is a compile-time string selected and folded before a
static target emits the exposed loops.

## Frontends

A frontend is deliberately split into transport and meaning:

- `onnx.read` and `tflite.read` decode bytes into faithful source-format
  calls and typed constants;
- `onnx.nn.convert` and `tflite.nn.convert` explicitly map those calls to
  shared functions.

Dynamic result extents do not require an opaque runtime operator. For example,
`nn.nonzero` allocates from the statically provable input-element capacity,
computes the selected extent in its ordinary function body, and returns a
logical `tensor.view`. The same body is inspectable and transformable before a
backend sees it.

The split preserves source attributes for inspection and allows a user to run
format-specific checks before conversion. It also keeps multiple frontends
from duplicating canonical tensor and neural-network bodies.

Adding a frontend should require:

1. one codec function returning `.jog` text;
2. schema-local type refinement where the external format requires it;
3. a conversion function built from `ir` edits and shared semantics;
4. conformance tests against authoritative models and reference outputs.

It should not require edits to the core, the C emitter, or another frontend.

## Analyses and transformations

`bounds` and `stat` return ordinary compile-time data. `opt`, `mem`, and
`tile` edit the same function bodies that users inspect.

`bounds.infer(m)` maps existing integer and Boolean values to conservative
closed intervals. It follows constants, resolved scalar arithmetic, static
loop ranges, and structured carried values while rejecting overflow instead of
wrapping a proof. `bounds.fold(m)` changes only comparison or logical calls
whose result interval is exactly `[0, 0]` or `[1, 1]`; unknown and mixed
conditions remain untouched. It deliberately leaves control-flow selection
and dead-code cleanup to the existing `opt.fold` and `opt.basic`, so
`bounds.fold opt.fold opt.basic` is an explicit composable pipeline rather
than an analysis with hidden mutation. A resolved `tensor.dim` of a static
tensor contributes one exact integer fact, allowing runtime-shape code to
collapse after specialization. No tensor contents, NN operation, allocation
policy, or target rule is otherwise built into the analysis.

`opt` applies algebra only to the exact resolved functions that own it. Its
built-in identities and cleanup recognize `base` scalar functions, not every
call printed with an operator token. User-defined number formats and operator
overloads therefore retain their own semantics unless a caller explicitly
includes them in a `pure` policy passed to `opt.dce`, `opt.cse`, or `opt.fix`.
`opt.update(m, roots, rule)` is the first explicit affected-cone execution
boundary. It snapshots `ir.affected(roots)` once and invokes an ordinary
`fn(Mod, Op) -> bool` rule for each still-live operation. Operations created by
the rule are deliberately not scheduled in the same wave; a caller chooses
whether to compute another wave. The complete call remains one transactional
`run`, so a failed rule rolls the wave back. This is a scheduler primitive, not
persistent pipeline reuse by itself; embedders can place named stages that use
it inside `ReactiveSchedule`.
Within each block, DCE walks in reverse definition order and propagates
deadness through already-dead pure users. A complete unused SSA chain is
therefore erased as one checked batch instead of forcing one whole-program
fixed-point round per link; cross-block users remain conservative.
`opt.hoist(m, safe)` performs policy-controlled loop-invariant code motion. A
listed call promises both absence of effects and safety when the loop executes
zero times; this is deliberately stronger than the `pure` policy used by DCE
and CSE. The `opt.hoist(m, policy)` overload instead accepts an ordinary
read-only `fn(Mod, Op) -> bool`, so a module may use types, metadata, or target
facts without changing `opt`. The pass moves immutable calls and constants only
when their operands already dominate the loop, visits inner loops first, avoids
binding collisions, and relies on `ir.move` to recheck all def-use dominance
before committing. Unknown calls, assignments, structured control, and
loop-carried values stay in place.

`opt.expose` is the main connection between semantics and a target. It asks a
capability function whether an operation is accepted and expands available
bodies only where needed. `opt.apply` selects compatible ordinary functions:
it expands a body-bearing implementation and transactionally retargets to a
bodyless external implementation. Its policy overload takes an ordinary
`fn(Mod, Op, Fn) -> bool` predicate and filters candidates before overload
selection. Alternatively, a `fn(Mod, Op, list<Fn>) -> list<Fn>` selector sees
the complete compatible set and returns zero or one member, allowing user code
to resolve equally specific implementations. Both policies are read-only;
selector cardinality and membership are validated before an edit. This lets a
module express layout, alignment, cost, or device-feature choices without a
core registry. A configured overload appends a normal `dict` parameter to
either callback. The caller supplies that dictionary to `opt.apply` or
`opt.instantiate`; its keys and units remain module-owned.
`opt.candidates(m, op, impls)` returns that same symbol and type-compatible set
without applying a policy or editing the module, so experiments can inspect
and report their choice space directly. Duplicate handles are removed; two
distinct implementations with equal signatures remain distinct candidates.
`opt.basic` performs target-independent cleanup. `opt.specialize(m, key,
value)` fully expands finite static loops carrying the selected open attribute
and folds list projection and constant branches around dynamic values. The
selection is explicit: the core never treats an attribute as behavior.
`opt.instantiate(m, name, args)` replaces one explicitly named generic root
function with a concrete instance while preserving its original name and
metadata. Type arguments are ordinary `Ty` values in a module and strings such
as `["1"]` at the command line. Existing call sites make the operation reject
rather than silently retarget incompatible calls.

The network-wide overloads `opt.instantiate(m, impls)` and
`opt.instantiate(m, module)` instead specialize matching generic bodies at
concrete call sites. Compile-time Boolean, integer, real, string, and recursive
list arguments are bound into private functions; tensor and byte values remain
ordinary parameters. Structurally identical configurations reuse one instance.
The policy overload accepts the same predicate or selector form as
`opt.apply`, including its configured form. No operator name, frontend schema,
configuration key, or target is built into this mechanism.

Modules may query open metadata uniformly with `ir.where`: the same name
filters `list<Fn>`, `list<Op>`, and `list<Val>` and returns the same handle
type. Exact attributes and membership in list-valued attributes are supported.
This keeps implementation discovery, schedule annotations, placement facts,
and value-format tags in ordinary module code rather than separate registries.

`mem.bound` derives a finite `mem.capacity` for a dynamic `tensor.make` when
its runtime shape is assembled by ordinary tensor writes and every extent has
a nonnegative integer upper bound. Static dimensions must agree exactly;
overflow and an unproved extent reject the proof. `mem.plan` revalidates these
facts and assigns reusable slots to bounded dynamic and static local tensors.
Parameters, constants, and returned bindings remain outside the local
workspace, allowing an artifact target to use caller-owned result storage
directly. A `tensor.view`, its backing binding, and its shape are excluded from
slot reuse; this conservative rule also covers chains of views. The shape of
a dynamic `tensor.make` is retained for later extent queries as well.
Uses inside a loop remain
live through the loop's end, including nested loops and carried values, rather
than ending at a single lexical read. This can retain more storage than a
precise alias-aware liveness analysis; it is not such an analysis.
The same pass marks a constructor fill as removable
only when Def-Use structure proves that one loop unconditionally writes the
complete linear or rectangular tensor domain before yielding it. This proof
uses no neural-network operation names; partial, conditional, indirect, and
otherwise unproven writes retain the fill. `tile` provides conservative
structural loop operations.
`mem.separate(op)` is a read-only call-site proof over tensor operands and
results. It recognizes only different planned slots, different immutable
tensor payloads, and slot/payload pairs. It rejects repeated values and every
unclassified storage source. This deliberately small relation can be consumed
by an artifact module without moving ABI policy into `mem`.
`tile.depends(value, source)` follows ordinary Def-Use edges, including values
captured by nested blocks, and reports a conservative value dependence.
`tile.axes(loop, value)` returns the zero-based loop-body axes on which a value
depends. Both queries are read-only and operate on the existing `Val` and `Op`
handles; they do not introduce an access descriptor or schedule object. A value
dependence is not by itself proof that two memory accesses are independent, so
loop rewrites must combine these facts with access and carried-value checks.
`tile.affine(m, loop, value)` adds the stronger fact needed for address
reasoning. It returns `[offset, coefficient0, coefficient1, ...]` for an
integer value that can be proved affine in the loop axes, or an empty list
when the proof is not available. It follows ordinary constants, copies,
same-type casts, addition,
subtraction, constant multiplication, and exact constant division. Nonlinear
products, truncating division, unsupported control flow, and coefficient
arithmetic overflow are rejected rather than approximated. The result is
compile-time data over existing values, not an affine dialect or another IR.
`tile.reads(m, loop, tensor)` and `tile.writes(m, loop, tensor)` return the
existing
index values grouped once per access. They conservatively follow tensor
bindings through stores, branch-carried values, and loop-carried values, so a
policy can combine actual accesses with `tile.axes` without learning internal
value versions. Indexing and range syntax are structural contracts: resolved
user overloads of `[]`, `[]=`, and `..` participate without a registration
table, while affine arithmetic laws remain restricted to `base` functions.
`tile.read_forms` and `tile.write_forms` linearize those indices for static
tensors as `[offset, coefficient0, coefficient1, ...]`. Their two-argument
forms collect all reads or writes in lexical IR order, so a policy can inspect
an unfamiliar body without naming its tensors; the three-argument forms retain
alias-aware selection for one tensor. An empty inner form preserves a known
access whose address or layout is not provably affine.
`tile.canon(m, loop)` uses the same proof to replace an affine index expression
with one deterministic sum of loop coordinates. It changes only index
operands and returns whether it made an edit. The old expression is erased as
one checked batch only when every external use is an indexed access being
rewritten; a value shared with a guard, data operand, range, or unrelated
operation is retained. `tile.canon(m)` applies the same rule across the module.
The transform is idempotent and contains no tensor-rank, operator, frontend, or
artifact case. It therefore removes expanded stride-update chains before C or
another target without turning that cleanup into an emitter peephole.
`tile.reorder(m, loop, order)` rebuilds the same loop with a permutation of its
existing axes. It is deliberately conservative: ranges must be statically
bounded, the carried state must have one equal affine read/write address, the
state address must be provably injective, and the permutation must preserve
relative order within state axes and within reduction axes. This admits stable
interleavings such as moving an unchanged reduction nest across independent
output coordinates while rejecting a changed floating-point reduction order.
The explicit form returns the replacement `Op`; an identity returns the
unchanged source. Factor-one split and unroll operations do the same. Policy
and whole-module forms return whether anything changed. The same convention
applies to explicit `split`, `unroll`, `scalarize`, and `fuse` edits, so a user
can compose structural edits without a wrapper or a rediscovery scan.
`tile.can_reorder` and `tile.reorder_issue` expose the identical read-only
legality decision used by the edit. `tile.reorderable(m, order)` enumerates
every currently legal loop without mutation. The policy overloads
`tile.reorder(m, policy)` and `tile.reorder(m, policy, config)` invoke an
ordinary read-only function for each live loop; returning an empty `list<int>`
skips it and returning a permutation requests the same checked edit.
`tile.state_axes(m, loop)` and `tile.reduction_axes(m, loop)` derive axis roles
from the affine read/write address of the loop's carried state. Unsupported
loops return no roles. The queries do not inspect a callee name, tensor rank,
or operator annotation, so a policy can choose an order without duplicating a
semantic implementation or hard-coding the number of loop axes.
`tile.scalarize(m, loop)` recognizes the output-stationary form in which all
state axes precede all reduction axes. It replaces one tensor-carried nest with
an outer state-coordinate loop, one scalar accumulator load, an inner
reduction loop, and one final tensor store. The pass preserves reduction order
and conditional updates. It also rebuilds every provably affine indexed
operand directly from the loop axes and omits the old address-only subgraph;
non-affine or otherwise observed address values remain unchanged. Its legality
is structural: one carried state, one equal affine indexed load/store, static
ranges, and no other observation of the carried tensor. `tile.scalarize_issue`,
`tile.can_scalarize`, and
`tile.scalarizable` expose the same read-only decision, and
`tile.scalarize(m)` applies every current candidate. None of these APIs names
Conv, an NN module, a tensor rank, or a backend.
The budgeted form `tile.scalarize(m, loop, budget)` also accepts a contiguous
reduction band followed by a static state tile. It materializes at most
`budget` scalar carried values, evaluates the original reduction body once per
tile coordinate, and writes each result after the reduction. The legality
proof checks the same affine access contract plus the complete address range
against the static carried-tensor capacity. A padded split that can alias or
escape that capacity is rejected; the pass never speculates that a surrounding
condition makes an invalid state address harmless. `can_scalarize`,
`scalarize_issue`, `scalarizable`, and the whole-module form accept the same
budget.
`tile.scalar_cost(m, loop, budget)` is the matching read-only structural cost.
It returns zero when that exact scalarization is illegal; otherwise it returns
the lane count multiplied by the recursively nested non-terminator operation
count in the source body. The two-argument form uses a one-lane budget. This is
an intentionally target-neutral duplication proxy for policy code, not an
estimate of emitted bytes or latency. Address rebuilding and later cleanup may
make the final artifact smaller or larger than the proxy suggests.
`tile.split(m, loop, axis, factor)` strip-mines any selected range axis into
adjacent outer and inner axes. Keeping them adjacent preserves lexicographic
iteration order. Statically divisible ranges need no tail branch; dynamic and
padded ranges retain the checked final-tile guard. The
returned `Op` is the replacement loop. The three-argument form selects the
last axis. `split_issue`, `can_split`, and
`splittable` accept the same optional explicit axis, so policy code can inspect
exactly the edit it intends to request. `tile.extents(m, loop)` returns all
static trip counts, or an empty list when any range is dynamic, so a policy can
require exact tiles without reimplementing range recognition.
`tile.merge(m, loop, axis)` performs the inverse structural operation for one
adjacent pair. It replaces the pair by a zero-based linear axis and reconstructs
both original coordinates with quotient and remainder inside the same body.
The mapping preserves the exact lexicographic execution order and every carried
value, so it does not rely on an operator, tensor rank, or independence guess.
When an exclusively address-used affine value is dense across the selected
pair, the same proof rewrites that value directly onto the merged coordinate;
other coordinate uses retain explicit quotient and remainder reconstruction.
For now, both ranges must have static integer bounds, positive trip counts, and
a representable product. `merge_issue`, `can_merge`, and `mergeable` expose the
same decision without mutation; omitting `axis` selects the final adjacent pair.
`tile.peel(m, loop, axis, factor)` is the conservative alternative when a
padded state tile would prevent a later interchange or promotion. It replaces
one static loop by an aligned prefix and an ordinary scalar tail, returned in
that order as `list<Op>`. The edit is legal only when the selected axis and all
of its outer axes are proved state axes, so executing the two loops
sequentially cannot change a reduction order. Exact, too-small, and
factor-one requests return the unchanged loop as a one-item list.
`peel_issue` and `can_peel` expose the same read-only legality decision. The
mechanism adds neither masked accesses nor a target-emitter case; a policy may
compose `split`, `reorder`, and `scalarize` on the returned prefix while leaving
the tail untouched.
`tile.splittable(m, factor)`, `tile.mergeable(m)`, and
`tile.unrollable(m, factor)` expose legal loop sets; `tile.fusible(m)` exposes
the exact pair collection consumed by automatic fusion. Enumeration is
read-only and returns live `Op` handles, or
two-operation lists for fusion, so a user policy can inspect ordinary IR
without reconstructing legality. Fusion recomputes this collection after each
structural round and skips candidates invalidated by an earlier edit in the
same round. The matching `reorder_issue`, `scalarize_issue`, `split_issue`,
`merge_issue`, `unroll_issue`, and `fuse_issue`
functions return the same human-readable reason used by each transform; an
empty string means the requested edit is legal. Policies can therefore count
or report rejected alternatives without attempting a mutation and scraping a
failed transaction.
Fusion compares affine tensor addresses rather than iterator names or operator
names. It accepts equal multi-axis iteration spaces and a dense row-major
producer consumed by one flattened loop. When a private intermediate is
initialized by a tensor fill, scalarized across a reduction, and read
pointwise, the fill/load/store chain is forwarded as scalar state. Repeated
fusion is covered by a print-and-reparse regression that includes nested loop
and branch state. Fixed-width and custom integer casts remain conservative;
only the core lossless `int`/`index` coordinate conversion is transparent to
affine analysis.
Factor one is a legal no-op for split and unroll and is therefore omitted from
their candidate collections.
These modules are intentionally separate: storage and scheduling policy can be
replaced independently and neither changes the core IR.

## Target modules

A useful target module exposes:

```jog
fn accepts(m: Mod, op: Op) -> bool
fn prepare(m: Mod) -> bool
fn source(m: Mod) -> str  // or another artifact function
```

The names are conventions, not interfaces baked into the runtime.
`accepts` defines a testable boundary. `prepare` explicitly exposes or
rewrites unsupported calls. Artifact functions return text or bytes and must
reject programs outside their advertised boundary.

`c` derives scalar spelling, alignment, index type, headers, payload layout,
and external prototypes from a configuration dictionary and resolved
signatures. A bodyless external declaration may remain generic when every
concrete call has a representable ABI. Prototypes are derived from call-site
types, identical erased signatures are emitted once, and incompatible uses of
one external symbol are rejected. This lets an adapter pass inferred shape or
format terms as normal scalar arguments without declaring every model shape.
`vm` emits a deterministic image and reports executed steps. Neither receives
privileged access to the IR.

`c.frontier(m)` returns the distinct resolved calls outside that boundary;
the configured overload evaluates the same question under a custom scalar
ABI. It is the read-only counterpart of `c.prepare`, so tests and research
modules can inspect target coverage without scraping diagnostics.

External payload offsets are indexed once by stable `Val` identity before
function definitions are rendered. Pure ABI, type, naming, and ownership
queries opt into the language's snapshot-aware memo contract. These are
emitter implementation details: they add no persistent analysis object or
target field to the IR, and an IR revision still invalidates every
capability-dependent result.

`c.api` returns the exported C surface as ordinary structured data: symbol and
declaration strings plus parameter and result descriptors containing source
types, C scalar spellings and representation classes, shapes, element counts,
byte counts, exact pointer status, and the external-data argument when present.
It calls the same local naming, signature, and payload-dependency functions as
`c.header`; consumers therefore need not scrape C text or reproduce target ABI
rules to allocate buffers. In particular, a scalar is marked as a pointer when
it is one member of a multi-result C interface, but remains a direct value when
it is the function's sole result.

`c.definition(m, name)` returns one function-definition chunk through the same
signature, naming, storage, and statement emitter used by `c.source`. It is a
function-granular artifact boundary for dependency-indexed tools: editing one
function can invalidate its definition without rebuilding unrelated
definitions. `c.definition(m, index)` selects a defined function by its
zero-based position, avoiding name ambiguity for overloads. `c.preamble(m)`
returns the includes and cross-function prototypes for the default no-blob
configuration. Concatenating that preamble with every indexed definition chunk
in order is byte-identical to `c.source(m)`.
`c.declaration(m, name)` returns one definition's declaration exactly as it
appears in that default preamble. `c.definition_binding(m, name)` returns the
same declaration together with the definition head from one shared signature
calculation, so a persistent builder can update both symbol-bearing fragments
without rescanning unrelated bodies.

For persistent artifacts, `c.definition_head`, `c.definition_storage`,
`c.definition_body`, and `c.definition_tail` expose the four ordered pieces of
one named default-configuration definition. Their concatenation is
byte-identical to `c.definition(m, name)`. ABI-policy edits such as `c.noalias`
can therefore rebuild the head without reevaluating storage or statements.
`c.definition_direct`, `c.definition_results`, and
`c.definition_body_chunk` additionally let a persistent builder emit a range
of top-level operations with one frozen result-routing context. Concatenating
all ranges in order is byte-identical to `c.definition_body`. Chunk boundaries
are implementation identities, not part of the artifact semantics: canonical
print/reparse may regroup equivalent IR, so correctness compares the complete
concatenated translation unit. `c.definition_nested_chunk` accepts the owning
control operation's `ir.path`, child-block index, and local operation range. It
computes the correct C indentation through nested loops and branches and
observes that child block rather than the whole module.
`c.definition_nested_partition` follows the same stable path recursively and
returns prefix, target, and suffix pieces whose concatenation equals the
requested top-level body region. A persistent builder can therefore retain
the structurally derived wrapper pieces and refresh only the nested target;
duplicate emitted text is not used as identity. These are semantic splits,
not compatibility aliases.
The emitter uses the native read-only `ir.bound(fn, name)` predicate for
binding-collision checks so a large function is not traversed in interpreted
`.jog` code merely to validate one result name.

Incremental tooling may retain the preamble only when its edit contract proves
that required headers, signatures, call topology, and payload policy are
unchanged. External payload configuration remains owned by the complete
`c.source` overloads; the default preamble API does not guess it.

A tensor with unknown dimensions still uses a flat pointer ABI. Each dynamic
input axis contributes an adjacent `index` extent argument; each dynamic result
axis contributes an adjacent `index*` extent result. No descriptor structure or
second tensor type is introduced. `c.api` reports `_` in the logical shape,
the proved `mem.capacity` separately, and `-1` for size and byte count when no
finite capacity is known. A caller can therefore allocate deterministic output
storage from the capacity while receiving the actual logical extent at run
time.

Non-empty fixed-shape tensors retain their exact minimum element count across
private C call boundaries through standard C11 array parameters. Public
definitions, prototypes, and headers use one consistent pointer spelling,
keeping the interface warning-clean and valid for both C and C++ consumers.
This is a type-preserving ABI fact, not an alignment, layout, or non-aliasing
claim.

`[c: {noalias: true}]` is an explicit contract on one function. The convenience
transform `c.noalias(m)` applies it to exported entries and
`c.noalias(m, false)` removes only that field while preserving other `c`
metadata. Tensor parameters, tensor results, and an external payload receive
qualified pointers in the generated public function definition. Public
declarations remain
unqualified, so the same header remains valid for C++ consumers. `c.api`
reports the Boolean contract. The compiler does not infer disjointness from NN
names or calling convention; violating an explicit entry contract at a call
site is the user's error.

`c.bind(m, fn, name)` assigns one live definition an explicit external C
symbol while preserving the other fields of its `c` contract. Passing an empty
name removes only the binding. A binding edit invalidates the preamble and that
definition's head; it does not require reevaluating storage or statement
fragments.

`c.restrict(m)` is the proof-driven alternative for private definitions. It
indexes all calls once and adds `[c: {restrict: true}]` only when every call to
a private function satisfies `mem.separate`. Functions with an implicit
external-data parameter are excluded, as are entries, returned buffers,
unplanned storage, and any function with one unsafe call. Repeating the pass
revalidates and replaces only this derived field; explicit `noalias` metadata
is preserved. Public declarations remain unchanged.

By default, `c` derives public symbols from qualified function names and keeps
valid source value names. Named pointer results derive from the return value
with a collision-checked `_out` suffix. An external payload argument uses
exactly the name passed to both `c.source` and `c.header` after C identifier
sanitization; `c.data` writes the corresponding bytes. Omitting that argument
embeds constants in the source, so an application should choose one mode
rather than emit an unused payload beside embedded data. A collision with a
source parameter is rejected. The payload pointer is present only on functions
that directly or transitively read external constants; unrelated public and
private functions keep their ordinary ABI. The `joggle_` prefix is reserved
for the native-module ABI; it is not added to generated model values. The
emitter also never invents abbreviated `jog_` or ordinal `v_` names. Unnamed
result buffers, storage slots, temporary values, and payload arrays instead use the
short role-based `out`, `slot_`, `tmp_`, and `data_` stems, while C keywords
receive a trailing underscore. A module can pin an external name with
`[c: {name: "vendor_kernel"}]`. Because Joggle is pre-1.0, source-derived
spelling is not itself a stable ABI promise: applications that require ABI
stability should use an explicit binding and compile the emitted header and
source from the same IR.

`c.prepare` expands unsupported function bodies and specializes loops explicitly
marked as shape bookkeeping. Static-rank traversal in tensor offsets and
broadcasting is therefore removed before emission without teaching the C target
about a tensor operator. It then runs the target-independent cleanup, whose
algebraic substitutions are restricted to immutable expressions. An unused
scalar assignment may be removed only when its defining operation is already
classified as pure; indexed writes remain effects. Immutable scalar bindings
are emitted as C `const` values. Compound updates retain their C operators,
and integral updates by the operator's identity are omitted without editing
the IR. Runtime element loops and data-dependent indexing remain visible.
After body expansion it refreshes inferred value types through `ir.type(m)`;
this commits the same fixed-point inference used by verification and is needed
because an edit may expose new concrete generic calls. It does not create a
typed side table or another IR.

For a function with one return path, an unplanned local tensor that reaches a
pointer result is backed directly by that result buffer. The emitter retains
the local source name as a pointer alias, so the body stays readable without a
second tensor allocation or a final elementwise copy. A planned workspace or
multiple return paths use the conservative copy form.

Target-specific support for a user type belongs in a small companion module.
The `sat.c` and `sat.vm` modules illustrate this rule: `sat` owns the type
semantics, while each companion owns only its representation at that target.
