# Tensor compilation pipeline

This page specifies the implemented tensor path. It is intentionally narrow:
one ordinary semantic function is fused and expanded to loops without an NN
operator table or a memory-effect protocol.

## Forms

All forms use the same `Fn`, `Blk`, `Op`, `Val`, and `Type` objects.

### Semantic form

A semantic Fn may call bodyful library functions:

```joggle
fn model(
  lhs: t.tensor<f32, [2, 4]>,
  rhs: t.tensor<f32, [4, 3]>
) -> t.tensor<f32, [2, 3]> {
  product = n.matmul(lhs, rhs);
  return n.relu(product);
}
```

The call DAG is the graph. There is no separate graph container.

### Fused tensor form

`@tensor.fuse(model)` produces one output construction:

```joggle
return t.tensor(
  [2, 3],
  (row, column) => {
    sum: f32 = a.zero();
    product = t.reduce(
      4,
      sum,
      (next, inner) =>
        next + lhs[row, inner] * rhs[inner, column]
    );
    return a.max(product, a.zero(product));
  }
);
```

The exact canonical printer may choose SSA names, but the invariant is exact:

- the Fn has one returned `tensor.tensor` construction;
- intermediate `tensor.map` results have disappeared;
- reductions remain pure scalar expressions inside the construction body;
- reads from input tensors remain `tensor.[]` calls;
- no `empty`, `set`, storage, target, or effect operation is present.

The pass derives this composition from the bodies of `nn.matmul` and
`nn.relu`; it never checks either name.

### Loop form

`@tensor.loops(fused)` makes iteration explicit:

```joggle
out: t.tensor<f32, [2, 3]> = t.empty([2, 3]);
for row, column in 2, 3 {
  sum: f32 = a.zero();
  for inner in 4 {
    sum = sum + lhs[row, inner] * rhs[inner, column];
  }
  out = t.set(out, a.max(sum, a.zero(sum)), row, column);
}
return out;
```

Structured source loops materialize as ordinary CFG blocks and block
arguments. `tensor.set` has functional value semantics: the result is the next
version of `out`. It does not claim a physical allocation or in-place store.

The loop-form invariant is:

- no `tensor.tensor`, `tensor.map`, or `tensor.reduce` call remains;
- one `tensor.empty` seeds each produced tensor;
- every output update is a `tensor.set` on a loop-carried value;
- input access remains `tensor.[]`;
- loops, reductions, and tensor versions satisfy normal CFG/SSA verification.

## Fusion algorithm

The implemented first stage is deterministic:

1. Expand every reachable bodyful, single-block call until only the tensor
   basis and opaque leaves remain.
2. Reject control flow, effects, non-tensor results, and tensor values with
   multiple consumers.
3. Start from each output coordinate and recursively request its scalar value.
4. For `tensor.tensor`, invoke its coordinate body.
5. For `tensor.map`, request the producer at the same coordinate and invoke
   the map body on the scalar result.
6. For `tensor.[]`, compose the requested coordinates with the indexed
   producer.
7. Preserve scalar operations and `tensor.reduce` in the new body.
8. Publish a new verified Fn containing one output construction.

This is access composition, not textual operator grouping. A bodyful custom fn
automatically participates if it normalizes to the tensor basis.

## Current safety boundary

The implementation refuses rather than guesses when it encounters:

- a shared tensor producer;
- residual control flow;
- an effect-typed value;
- a bodyless tensor producer inside the requested fusion region;
- a dynamic result shape;
- more than one returned tensor.

These are current implementation limits, not new IR classes. Diagnostics state
which contract was not met.

## Planned fusion planner

General graphs require a planning step before access composition. The planner
will be an internal analysis over `Fn` use-def; it will not be a public Graph
object. Its decisions are:

- fuse single-use injective chains;
- fuse elementwise epilogues into reductions and dense computations;
- use post-dominance to handle reconvergent diamonds;
- materialize shared producers unless recomputation is explicitly cheaper;
- stop at effects, opaque bodies, control-flow boundaries, or target calls.

The default score is deterministic and symbolic:

```text
saved intermediate bytes
- duplicated scalar work
- code-size growth
- live-input pressure
```

A target package may supply a different ordinary cost fn. It does not register
fusion traits on every operator.

## Target boundary

Loop form is the last target-independent state currently implemented. A future
target package may:

1. choose physical layouts and custom element formats;
2. prove which tensor versions may alias storage;
3. tile, reorder, vectorize, or replace loop regions;
4. select target primitives;
5. emit bytes or a target source Mod.

No target package is part of the tensor API, and SystemVerilog or RISC-V text
would be an emitter result rather than an IR owner.
