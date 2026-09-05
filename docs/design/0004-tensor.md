# Design 0004: Tensor algebra

Status: accepted; implementation in progress

## Decision

Joggle uses one small, frontend-neutral Tensor package:

```text
compute(shape, index_fn)   construct a logical tensor
tensor[i, j, ...]          read by logical indices
map(tensor, element_fn)    rank-polymorphic element transform
reduce(tensor, init, fn)   ordered accumulation
```

There is no public coordinate Type. A Known shape determines the number of
`index` inputs in `compute`'s callable Type. Contextual lambda inference then
turns `(i, j) => ...` into the same typed nested Fn that an explicitly annotated
lambda would produce. Captured tensors remain explicit closure edges.

## Rationale

`compute` alone cannot replace the other two combinators without losing useful
structure. Generic elementwise code does not know how many indices a shape has,
so `map` is the rank-polymorphic form. A normal control-flow loop could encode a
reduction, but would erase the reduction boundary and make reassociation,
parallelization, and storage elimination harder to justify. Ordered `reduce`
states only traversal order; a later pass needs additional evidence before
reassociation.

This retains the algorithmic-pattern structure emphasized by
[LIFT](https://lift-project.github.io/publications/2017/steuwer17LiftIR.pdf)
and [RISE & Shine](https://thok.eu/publications/2022/rise.pdf). The indexed
construction surface follows the usability of
[TVM TE](https://tvm.apache.org/docs/deep_dive/tensor_ir/tutorials/tir_creation.html),
but Joggle does not introduce a second TE object model or convert it into a
different built-in Fn kind.

## Dependent callable mechanism

Prelude supplies the ordinary compiler-time function:

```joggle
fn repeat(value: type, count: int) -> list<type>;
```

Tensor declares `compute` with
`callable<@repeat(index, length(S)), [E]>`. Direct Known bindings are solved
before dependent Residual callable Types. This is a general language rule, not
a Tensor special case: any package can let a Known parameter determine a later
lambda Type.

## Neural-network boundary

`nn` owns bodyful frontend-independent algorithms. For example, rank-two
MatMul is nested `compute`, product construction, and ordered `reduce`; Relu is
`map`. ONNX contains no algorithm bodies. Its reader and source schema are
separate so a future TFLite reader can preserve TFLite details while converging
on the same `nn` functions through a normal pass.

## Rejected alternatives

- A public `coord<S>` wrapper adds ceremony to every indexing lambda and hides
  familiar multi-index syntax.
- Per-rank `compute1`, `compute2`, ... overloads create an unbounded declaration
  family.
- A single opaque `kernel` or `generate` operation discards map/reduce laws and
  pushes semantic recovery into analysis.
- Putting Conv or ONNX names in Tensor couples the reusable algebra to one
  workload or exchange format.

## Gates

- [x] Multi-index parsing, formatting, overload resolution, and IR round-trip.
- [x] Contextually inferred lambda parameter Types.
- [x] Known parameters can determine later callable Types.
- [x] Frontend-neutral bodyful MatMul and Relu package.
- [x] ONNX reader/schema separation from NN algorithms.
- [ ] Enforce Tensor subscript arity against static rank.
- [ ] Add symbolic extents without a second shape AST.
- [ ] Derive access summaries and dependence facts from ordinary Fn bodies.
- [ ] Add generic fusion equations only after the new algebra has real workload
      evidence.
- [ ] Lower the same Fn semantics to user-defined implementation vocabulary.
