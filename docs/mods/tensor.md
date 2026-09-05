# Tensor

`tensor@5.0.0` is the frontend-neutral logical Tensor Mod. Compiler core has
no Tensor Op class, graph dialect, attribute table, or lowering registry.

## Surface

```joggle
type tensor(element: type, shape: list<int>);

fn compute<E, S: list<int>>(
  shape: S,
  body: (list<index>) -> E
) -> tensor<E, S>;

fn ([])<E, S: list<int>>(
  input: tensor<E, S>, indices: index...
) -> E;

fn ([])<E, S: list<int>>(
  input: tensor<E, S>, indices: list<index>
) -> E;

fn ([])(indices: list<index>, position: int) -> index;
```

`compute` constructs one value for every logical point. Its callback receives
an ordinary `list<index>`, so the same signature supports any rank:

```joggle
return t.compute([3, 2], (at) => input[at[1], at[0]]);
```

The two subscript overloads are one operation symbol. `input[at]` is convenient
for generic-rank elementwise code; `input[row, column]` is convenient after a
point has been projected. Both become ordinary Calls.

## Ordinary loops, not Tensor combinators

Tensor does not declare `map`, `reduce`, `fold`, or `scan`. An extension may
offer such helpers as normal bodyful fns, but they have no special compiler
status. Elementwise algorithms use `compute`; reductions use ordinary loop
carried values:

```joggle
sum: E = zero();
for k: index in range(K) {
  sum = sum + lhs[row, k] * rhs[k, column];
}
return sum;
```

Analyses must recover access and reduction facts from this real Fn structure.
A transformation may reassociate or parallelize only after proving legality.

## Boundaries

Logical shape does not define layout, packing, allocation, address space,
schedule, or device. Those remain future user-defined Types and fns. Tensor
also does not declare Conv, Relu, MatMul, ONNX nodes, or TFLite builtins. The
`nn` Mod defines frontend-independent algorithms; format readers preserve
their source schemas and convert through ordinary passes.
