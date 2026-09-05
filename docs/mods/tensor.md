# Tensor

`tensor@4.0.0` is a target-independent structural calculus. It is an ordinary
source Mod and adds no tensor node, graph owner, operation registry, or
lowering interface to compiler core.

## Types

```joggle
type tensor(element: type, shape: list<int>);
type coord(shape: list<int>);
```

The shape is a logical extent. It does not encode physical layout, packing,
address space, allocation, or placement. `coord<S>` is an index in that same
logical space. The current test verifier accepts static non-negative extents;
symbolic extents remain unfinished.

Coordinates use the Prelude `index` Type. Construction and projection are
ordinary fns:

```joggle
fn coord<S: list<int>>(shape: S, values: index...) -> coord<S>;
fn ([])<S: list<int>>(position: coord<S>, axis: int) -> index;
```

The shape argument specializes the result and the number of residual values
must agree with its rank. The rank check is an unfinished semantic verifier.
The surface expression `position[0]` is the ordinary symbolic call
`[](position, 0)`; `0` is a compiler-time callee binding rather than an Op
attribute.

## Functional basis

```joggle
fn map<E, S: list<int>>(
  shape: S,
  body: (coord<S>) -> E
) -> tensor<E, S>;

fn ([])<E, S: list<int>>(
  input: tensor<E, S>,
  position: coord<S>
) -> E;

fn reduce<E, S: list<int>, A>(
  input: tensor<E, S>,
  initial: A,
  body: (A, E) -> A
) -> A;
```

The domain overload `map(shape, body)` defines an element for every logical
coordinate. `input[position]`
reads one element. `reduce(input, initial, body)` performs a deterministic
ordered accumulation over the Tensor elements. Its logical domain comes from
the Tensor Type and is not repeated at the call site. No associativity or
reassociation permission is implicit. The symbolic overload is the only
Tensor access declaration; there is no parallel named `at` API, and the
language core knows nothing about Tensor indexing.

The Tensor overload of `map` is derived from domain `map` and `[]`:

```joggle
fn map<E, S: list<int>, R>(
  input: tensor<E, S>,
  body: (E) -> R
) -> tensor<R, S> {
  return map(
    S,
    (position: coord<S>) => body(input[position])
  );
}
```

This is one overloaded functional algebra, not a second IR tier. Model authors
map an existing Tensor; library authors may map a logical coordinate domain.
Inlining exposes both as the same nested-Fn representation. `reduce` consumes
that Tensor representation and preserves logical order; parallel reassociation
requires separate evidence. All are ordinary fns in the same `tensor` Mod,
and only `[]` spells element access.

Typed lambdas are nested `Fn` values. Their free tensor and callback values are
explicit capture edges, so materialization, verification, inlining, and later
dependence analysis all inspect one representation.

## What is deliberately absent

Tensor does not declare Conv, Relu, GEMM, pooling, concatenation, reshape,
softmax, model constants, quantization, layouts, kernels, buffers, devices, or
emitters. Domain Mods define those conveniences by composing the calculus.
ONNX symbols belong to `onnx`; scalar leaves belong to `arith`.

Current tests prove body inspection and generic inlining for `map`, express a
dot product as `map + reduce + [] + scalar operators`, and materialize coordinate
construction, coordinate projection, and tensor indexing as ordinary Calls.
Execution, symbolic shapes, views, dependence-checked fusion, storage planning,
and scheduling remain future gates rather than implied capabilities.
