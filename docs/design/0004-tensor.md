# Design 0004: Tensor functions

Status: accepted; implementation in progress

## Decision

Tensor is an ordinary Mod with one construction function and one overloaded
access symbol:

```joggle
type tensor(element: type, shape: list<int>);

fn compute<E, S: list<int>>(
  shape: S,
  body: (list<index>) -> E
) -> tensor<E, S>;

fn ([])<E, S: list<int>>(
  input: tensor<E, S>,
  indices: index...
) -> E;

fn ([])<E, S: list<int>>(
  input: tensor<E, S>,
  indices: list<index>
) -> E;

fn ([])(indices: list<index>, position: int) -> index;
```

`map`, `reduce`, `fold`, and `scan` are not privileged Tensor declarations.
They may be normal library helpers, but a user can express their computation
with the same `fn`, `for`, rebinding, and `[]` syntax used everywhere else.

## Logical indices

The callback receives one ordinary `list<index>`. This keeps generic-rank code
well typed without a public `coord<S>` class or a family of `compute1`,
`compute2`, and similar declarations:

```joggle
fn relu<E, S: list<int>>(input: tensor<E, S>) -> tensor<E, S> {
  return compute(S, (at) => max(input[at], zero(input[at])));
}
```

Known-rank code can project the list and use familiar multi-index access:

```joggle
return compute([3, 2], (at) => input[at[1], at[0]]);
```

The index list is a logical point, not a layout, address, allocation, or
machine coordinate. Projection and both Tensor access spellings resolve to
ordinary `[]` Calls.

## Reductions are ordinary control flow

A reduction is a loop-carried value in the existing Fn CFG:

```joggle
sum: E = zero();
for inner: index in range(K) {
  sum = sum + lhs[row, inner] * rhs[inner, column];
}
return sum;
```

This avoids forcing an algorithm author to classify a function as `map` or
`reduce`. It also preserves the exact sequential semantics by default.
Reassociation, parallel execution, tiling, and fusion require a pass to prove
the corresponding operator and dependence properties.

The compiler will derive iteration domains, access relations, loop-carried
values, effects, and reduction candidates from the real Fn. Those summaries
are analysis results, not another Tensor IR and not annotations that every
extension author must maintain.

## Relation to prior systems

LIFT and RISE show the optimization value of retaining algebraic structure.
Joggle adopts that lesson in analysis and transformation rather than requiring
their fixed combinator vocabulary at the public boundary. TVM TE motivates
the concise indexed-construction surface, while TensorIR demonstrates why
dependence facts must be explicit before schedules are legal. Joggle keeps all
of those computations in one Fn/Call/CFG representation.

## Neural-network boundary

`nn` owns frontend-independent algorithms. Rank-two MatMul now uses
`tensor.compute` plus an ordinary scalar accumulation loop; Relu uses indexed
construction. Neither Tensor nor compiler C++ recognizes their names. ONNX
owns no algorithm bodies and conversion from source-schema calls remains an
ordinary pending pass.

## Gates

- [x] Multi-index parsing, formatting, overload resolution, and IR round-trip.
- [x] Contextually inferred lambda parameter Types.
- [x] Generic-rank indexed construction without a coordinate wrapper.
- [x] MatMul accumulation materializes as an ordinary five-block loop CFG.
- [x] Relu materializes as ordinary indexed construction.
- [x] ONNX reader/schema separation from NN algorithms.
- [ ] Enforce Tensor subscript arity against static rank.
- [ ] Add symbolic extents without a second shape AST.
- [ ] Derive access and dependence summaries from ordinary Fn bodies.
- [ ] Recognize reduction candidates without changing their default order.
- [ ] Implement legality-checked fusion against real converted models.
- [ ] Map the same Fn semantics to user-defined implementation vocabulary.
