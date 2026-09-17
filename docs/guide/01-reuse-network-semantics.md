# Reuse network semantics

The installed `tensor` and `nn` modules are ordinary source libraries. A model
can stay concise while the referenced implementation remains inspectable:

```jog
module network
use nn

fn residual(x: tensor<f32, [4]>, skip: tensor<f32, [4]>)
    -> tensor<f32, [4]> {
  return nn.relu(x + skip)
}
```

The tensor-specific `+` wins over the generic base overload by structural
specificity. Its body is a linear element loop; `nn.relu` is another loop with
a condition. Loading the functions does not expand them. A project chooses
the level it wants with an ordinary transform:

```jog
module expose
use opt

fn network(m: Mod) -> bool {
  return opt.expand(m, ["operator +", "nn.relu"])
}
```

Running `expose.network` replaces only those two calls by their resolved
bodies. A later invocation can expose `tensor.matmul`, while an experiment
that maps the abstract call directly to a target primitive can leave it
untouched. Body expansion is generic: the core contains no tensor or NN name,
and C++ performs the same edit with `env.resolve(mod, op)` followed by
`env.expand(mod, op, fn)`. The environment-aware edit handles both a resolved
body and an alternative implementation, so dependency visibility and body
expansion commit together.

When one traversal selects several calls, collect the aligned call and body
handles and invoke `ir.expand(m, calls, bodies)` once. The list form preserves
the same per-call report entries but rolls back the whole frontier if any pair
is invalid. The corresponding embedding API accepts two spans.

For a larger model, list the calls a consumer can already implement and let
`opt` expose everything else to that boundary:

```jog
module edge
use opt
use ir
use tensor

fn tensor.matmul<M: int, N: int, K: int>(
  a: tensor<i8, [M, K]>, b: tensor<i8, [K, N]>
) -> tensor<i8, [M, N]>;

fn caps() -> list<Fn> {
  return ir.fns("edge")
}

fn prepare(m: Mod) -> bool {
  return opt.legalize(m, caps(), 16)
}

fn missing(m: Mod) -> list<str> {
  return opt.frontier(m, caps())
}
```

`prepare` is an ordinary transform and `missing` is an ordinary read-only
query. A bodyless declaration is both the semantic name and the accepted type
contract. The example retains rank-two `i8` matrix products with compatible
symbolic dimensions; other `tensor.matmul` calls remain visible or expand.
The same form can describe `nn.conv2d` or a custom function, so Joggle does not
prescribe an abstraction level.

To provide an inspectable implementation, give that same declaration a normal
body and apply the module's functions:

```jog
fn dot<M: int, N: int, K: int>(
  a: tensor<i8, [M, K]>, b: tensor<i8, [K, N]>
) -> tensor<i8, [M, N]>;

fn tensor.matmul<M: int, N: int, K: int>(
  a: tensor<i8, [M, K]>, b: tensor<i8, [K, N]>
) -> tensor<i8, [M, N]> {
  return dot(a, b)
}

fn apply(m: Mod) -> bool {
  return opt.apply(m, ir.fns("edge"))
}
```

The ordinary overload rules choose among generic and shape- or format-specific
implementations. Newly exposed matching calls are applied to a fixed point.
Use `opt.apply(m, impls, limit)` when a recursive specialization needs an
explicit bound; ambiguity or bound exhaustion restores the complete input
module, and declaration order is never a selection policy.

When types alone cannot express a constraint, pass a normal read-only
`fn(Mod, Op, Fn) -> bool` as the third argument. It receives the semantic call
and each type-compatible candidate before specificity is resolved. The edge
example uses this form to select its portable Conv only for constant
NCHW/OIHW/NCHW layout lists. The same mechanism can inspect an open format
attribute, alignment, or a user-defined device feature; it does not assign any
of those concepts to the core.

If a selected implementation has no body, `opt.apply` retargets the call to
that declaration instead of expanding it. The source model still calls its
semantic function; selection transactionally adds the implementation module,
and a later artifact module derives the external ABI from the selected
signature. [`extensions/edge`](../../extensions/edge) exercises tensor and
multiple-result kernels this way. Adding another external implementation does
not change the model, core, or C emitter.

An external declaration may be generic even though emitted C is not. Each
actual call must contain concrete, representable argument and result types.
The C module erases tensor extents to pointer types, emits equal concrete
prototypes once, and rejects a shared C symbol if two call sites imply
different scalar or pointer ABIs. The edge matrix adapter passes `M`, `N`, and
`K` as ordinary scalar arguments, so one external kernel declaration safely
covers multiple static shapes rather than requiring one declaration per shape.

The repository's
[`extensions/ikj/module.jog`](../../extensions/ikj/module.jog) turns that mechanism
into an executable kernel customization. Its alternative
`tensor.matmul` body changes the loop order to `i-k-j`; the generic element
type and three dimensions are inferred from the real call. Run the extension
with `-M extensions -M build/modules`, then inspect the generated
`build/ikj.jog` before emitting C. The accompanying C gate checks
`[58, 64, 139, 154]`, so the extension is both readable source and a regression,
not an unexecuted API sketch.

Frontend attributes are structural dictionaries. A bridge can use
`has(attrs, key)`, strict `attrs[key]`, `get(attrs, key, fallback)`, and
`keys(attrs)` directly in `.jog`; no schema accessor class or frontend-specific
core hook is required.

Computation and data annotations stay separate without new object families:

```jog
[place: "edge"]
let [quant: {scale: [0.25], zero_point: [0]}] y = frontend.add(a, b)
```

The first dictionary belongs to the call `Op`; the inline dictionary belongs
to its result `Val`. A module queries both with the same `ir.meta` and edits
both with the same `ir.set`/`ir.unset` functions. Names such as `place` and
`quant` are examples, not built-in policies.

For same-signature calls, the whole frontend relation can stay declarative:

```jog
fn onnx_to_nn(m: Mod) -> bool {
  var changed = ir.use(m, "nn")
  changed = opt.rename(m, [["onnx.Relu", "nn.relu"]]) || changed
  return changed
}
```

Neither importing ONNX nor loading `nn` runs this function. The bridge is
selected explicitly, and transaction-final verification rejects a target
whose signature does not match the transported call.

The textual equivalent loads a module and selects one of its normal functions:

```cpp
env.path("modules");
if (!env.load("opt") || !joggle::run(env, "opt.fold_add_zero", mod))
  return env.print_diags(stderr);
```

`modules/opt/module.jog` is the complete transform. It iterates functions,
`Blk`s and operations through `ir`, replaces the result of `x + 0`, and erases
the dead call. No C++ registration is required for that transform.

To edit the compiler function itself, keep its definitions in a separate `Mod`
from the model. The two values use the same API; the distinction is their role
in this workflow, not a new compiler class:

```cpp
joggle::Mod compiler; // parsed from a module using opt
joggle::Mod model;    // parsed from the model source
auto fold = compiler.clone(env, env.find_fn("opt.fold_add_zero"), "my_fold");
if (!fold || !compiler.verify(env)) return false;
// Edit fold's body through compiler's ordinary Val/Op/Blk editing API.
// Verify compiler again after those edits.
joggle::Attr report;
if (!joggle::run(env, fold, model, report)) return false;
```

Only the model is passed to the C preparation function. Copying `fold` into
the model instead would make an emitter inspect compiler-only code as though
it were a model function. The handle call keeps that code outside the artifact
while allowing ordinary structural edits and a transactional execution.
For a procedure such as `c.prepare`, `clone` also captures its private helper
functions inside `compiler` and rebinds the derived copy to them. Those helper
bodies can be edited with the same `Op` API before running the derived `Fn` on
the model; the installed `c.prepare` remains unchanged. The copied private
closure may be larger than the public wrapper, so inspect and verify the
compiler module rather than treating derivation as free specialization.

The same editor can copy a structured operation while explicitly rewiring
values captured from its surrounding function:

```jog
let tiled_body = ir.clone(
  m, original_loop, before, [old_index, old_tensor], [tile_index, tile_tensor]
)
assert(ir.live(tiled_body), "loop clone failed")
```

The lists are a parallel substitution table, not a schedule description.
Joggle verifies exact types and dominance for entries selected by the copied
subtree; unused entries allow one accumulated table to clone a sequence. A loop
module can therefore build new bounds and carried values, clone ordinary
nested operations into the new structure, and then replace the old loop. The
same primitive applies to conditions and calls and leaves the core unaware of
tiling policy.

The bundled removable `tile` module turns that primitive into one complete
loop transform. A project chooses the loop with ordinary reflection:

```jog
let axes = tile.axes(loop, index)
let form = tile.affine(m, loop, index)
if len(axes) == 2 && axes[0] == 0 && axes[1] == 1 {
  // The index is derived from both loop axes.
}
```

`tile.axes` follows ordinary values through calls and nested blocks. It lets a
policy inspect which loop arguments contribute to an index or condition while
keeping the program in the same IR. It intentionally reports value dependence,
not a complete memory-dependence proof; a transform must still inspect loads,
stores, aliasing, and carried results before changing order.

When `form` is nonempty it is `[offset, coefficient0, coefficient1, ...]`.
For example, an index `row * 100 + column` in a two-axis loop produces
`[0, 100, 1]`. `tile.affine` returns an empty list for `row * column`, an
inexact integer division, an unsupported definition, or arithmetic overflow.
This gives a user pass a proof-oriented building block for layout and loop
legality without introducing an affine expression object.

For tensor accesses, `tile.reads(m, loop, tensor)` and
`tile.writes(m, loop, tensor)` return `list<list<Val>>`. Each inner list is the
actual index vector of one access. Store updates and structured carried values
are followed conservatively, so the query does not expose compiler-created
versions as a second user-facing abstraction.

Policies that need layout cost rather than the original index values can call
`tile.read_forms(m, loop, tensor)` or `tile.write_forms(m, loop, tensor)`.
Each result is the row-major linear form of one access. For a static
`tensor<E, [2, 3]>` indexed by `[row, column]`, the form is `[0, 3, 1]`.
An empty inner list retains the fact that an access exists while stating that
its address is not provably affine; dynamic tensor layouts are never guessed.
The two-argument overloads `tile.read_forms(m, loop)` and
`tile.write_forms(m, loop)` collect every corresponding access in lexical IR
order. They let a policy inspect a newly exposed body without knowing its
parameter names or reconstructing tensor aliases.

A project can then choose a rewrite with ordinary reflection:

```jog
module my_tile
use tile

fn apply(m: Mod) -> bool {
  for op in ir.ops(m) {
    if ir.kind(op) == "loop" {
      return ir.live(tile.split(m, op, 0, 4))
    }
  }
  return false
}
```

`tile.split(m, loop, axis, factor)` replaces the selected iterator by adjacent
outer and inner iterators and adds a tail guard, so bounds need not be
divisible by the factor. The adjacency preserves the original lexicographic
iteration order even when `axis` is not the last iterator; a later checked
`tile.reorder` call is the explicit way to change that order. The shorter
`tile.split(m, loop, factor)` form selects the last iterator. Neither form
selects a loop or factor on the user's behalf.

An experiment can also collapse an adjacent static pair without introducing a
new loop representation:

```jog
if tile.can_merge(m, loop, axis) {
  let merged = tile.merge(m, loop, axis)
  assert(ir.live(merged), "axis merge failed")
}
```

The replacement loop has one linear iterator. Its body reconstructs the two
original coordinates, preserving nonzero lower bounds, lexicographic order,
and all loop-carried values. The shorter form selects the final adjacent pair;
`tile.mergeable(m)` lets policy code enumerate the same legal edits first.

After splitting a state axis, a pass may legally move only its inner axis
across a reduction band and promote the resulting tile:

```jog
let tiled = tile.split(m, loop, state_axis, 4)
let reordered = tile.reorder(m, tiled, [0, 1, 2, 3, 5, 6, 7, 4])
let promoted = tile.scalarize(m, reordered, 4)
assert(ir.live(promoted), "tile promotion failed")
```

If a static state extent is not divisible by four, peel it before applying the
same sequence:

```jog
let parts = tile.peel(m, loop, state_axis, 4)
let prefix = tile.split(m, parts[0], state_axis, 4)
// Reorder and promote prefix; parts[1] remains the scalar tail.
```

`tile.peel` returns `[loop]` when no tail exists and `[prefix, tail]` when it
performs the edit. It accepts only leading state coordinates proved from the
carried value's affine address. It therefore cannot silently move a reduction
tail into a separate phase.

The final argument is a hard scalar budget, not a hidden schedule choice. The
pass infers the state and reduction bands from affine accesses and refuses a
tile whose full state-address range cannot be proved inside its static tensor.
This is the same mechanism for convolution, matrix multiplication, or a custom
tensor reduction; a policy still chooses the axis, factor, and order.
Before choosing a factor, policy code may query `tile.scalar_cost(m, loop)`.
The query returns the recursive source-body operation count for one legal
scalar lane, or zero when the loop is not a candidate. Multiplying it by a
prospective factor provides a backend-independent duplication budget without
performing a speculative edit. It deliberately does not pretend to predict
generated source bytes or target latency.
An explicit single-loop edit returns its replacement `Op`, so edits compose
without a result wrapper, temporary metadata, or a second module scan. A legal
factor-one split or identity reorder returns the unchanged source handle.
Whole-module and policy overloads instead return `bool` because their result is
whether any candidate changed.

`tile.reorder(m, loop, [0, 1, 4, 5, 6, 2, 3])` changes only the axis order of
the selected loop. Its legality check proves that the carried tensor uses an
injective affine address and that both the per-address reduction order and the
state-axis order remain stable. A pass can therefore move reduction axes across
independent output axes without defining a replacement operator body. See the
[`locality` extension](../../extensions/locality) for a complete source-only policy.
The policy overload accepts a normal function returning `list<int>`:

```jog
fn choose(m: Mod, op: Op) -> list<int> {
  let state = tile.state_axes(m, op)
  let reduction = tile.reduction_axes(m, op)
  if len(state) <= 2 || len(reduction) == 0 {
    return []
  }
  var order: list<int> = []
  for i in 0..len(state) - 2 {
    order += [state[i]]
  }
  order += reduction
  for i in len(state) - 2..len(state) {
    order += [state[i]]
  }
  if tile.can_reorder(m, op, order) {
    return order
  }
  return []
}

fn apply(m: Mod) -> bool {
  return tile.reorder(m, ir.find("my_tile.choose"))
}
```

The driver owns traversal and stale-handle checks, verifies that the policy is
read-only, and sends every nonempty result through `reorder_issue`; user policy
does not duplicate transformation safety.

Policies need not know an operator name or fixed rank.
`tile.state_axes(m, loop)` returns axes that select distinct elements of the
carried state, while `tile.reduction_axes(m, loop)` returns axes that update the
selected element. Both are inferred from the same affine access proof used by
`tile.reorder`.

`tile.fuse(m, producer, consumer)` uses the same explicit-selection rule. It
accepts a conservative case: two one-dimensional loops over the same
range, each carrying one tensor, where the producer stores one element and the
consumer reads that same element at the same iterator. It rejects shifted or
otherwise non-pointwise dependencies instead of recognizing operation names.
If the produced tensor has no other user, its scalar value is forwarded into
the consumer and the private tensor disappears. A live external result remains
materialized, so scheduling does not silently change the function interface.
An unrelated call between the loops is not crossed implicitly.

For a default greedy traversal, call `tile.fuse(m)`. It considers adjacent
loops only, uses the same read-only `tile.can_fuse` predicate, and repeats until
no legal pair remains. This is convenient for inspection and baseline
experiments; a research module remains free to own candidate selection and
profitability while reusing the exact same legality and rewrite functions.

A module can also retain the bundled traversal and supply only profitability:

```jog
module my_policy
use tile

fn small(m: Mod, pair: list<Op>, limit: int) -> bool {
  let producer = pair[0]
  return len(ir.ops(ir.blks(producer)[0])) <= limit
}

fn apply(m: Mod, limit: int) -> bool {
  return tile.fuse(m, ir.find("my_policy.small"), limit)
}
```

`tile` invokes `small` only for structurally legal adjacent pairs and rejects
a policy that mutates the module. Replacing `small` with a target cost model
does not change the traversal, rewrite, core IR, or emitter.

