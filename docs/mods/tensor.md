# Tensor

`tensor@4.0.0` is the frontend-neutral tensor algebra. It is a normal source
Mod; compiler core has no Tensor Op class, graph dialect, attribute table, or
lowering registry.

## Surface

```joggle
type tensor(element: type, shape: list<int>);

fn compute<E, S: list<int>>(
  shape: S,
  body: callable<@repeat(index, length(S)), [E]>
) -> tensor<E, S>;

fn ([])<E, S: list<int>>(
  input: tensor<E, S>,
  indices: index...
) -> E;

fn map<E, S: list<int>, R>(
  input: tensor<E, S>, body: (E) -> R
) -> tensor<R, S>;

fn reduce<E, S: list<int>, A>(
  input: tensor<E, S>, initial: A, body: (A, E) -> A
) -> A;
```

The variadic subscript still uses infix surface syntax; `input[i, j]` is one
ordinary Call with three value arguments.

`compute` constructs one value per logical index. The known shape determines
the contextual lambda arity, so users write familiar code without a coordinate
wrapper:

```joggle
return t.compute([3, 2], (row, column) => input[column, row]);
```

`map` remains separate because it represents a rank-polymorphic elementwise
operation. A generic function such as `relu<E,S>` cannot spell a fixed list of
index parameters when the rank of `S` is unknown. `reduce` states deterministic
ordered accumulation; no reassociation or parallelism permission is implicit.

## Why four operations

`compute` and `[]` expose index functions needed by layout changes, convolution,
and matrix multiplication. `map` preserves a strong elementwise invariant for
generic fusion. `reduce` preserves a recognizable reduction domain and update
function. Encoding the latter two as arbitrary loops would be expressively
possible but would discard the algebraic facts a transformation needs to prove
legality.

The division follows the functional algorithmic-pattern lesson of
[LIFT](https://lift-project.github.io/publications/2017/steuwer17LiftIR.pdf)
and [RISE & Shine](https://thok.eu/publications/2022/rise.pdf), while the
`compute(shape, lambda)` surface intentionally resembles
[TVM TE](https://tvm.apache.org/docs/deep_dive/tensor_ir/tutorials/tir_creation.html).
Unlike those systems, these names are ordinary package functions in Joggle's
single Fn representation.

## Boundaries

Logical shape does not define layout, packing, allocation, address space,
schedule, or device. Those are later user-defined Types and fns. Tensor also
does not declare Conv, Relu, MatMul, ONNX nodes, or TFLite builtins. The `nn`
package defines frontend-independent algorithms; format readers preserve their
own source schemas and convert to `nn` through ordinary passes.

Current implementation supports static integer shapes, contextual lambda
parameter inference, explicit captures, and variadic multi-index syntax.
Symbolic extents, index-count verification on every subscript, views,
dependence summaries, storage planning, and scheduling remain explicit future
gates.
