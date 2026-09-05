# Tensor

`tensor@3.0.0` is a target-independent structural calculus. It is an ordinary
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

## Structural basis

```joggle
fn build<E, S: list<int>>(
  shape: S,
  body: (coord<S>) -> E
) -> tensor<E, S>;

fn at<E, S: list<int>>(
  input: tensor<E, S>,
  position: coord<S>
) -> E;

fn ([])<E, S: list<int>>(
  input: tensor<E, S>,
  position: coord<S>
) -> E {
  return at(input, position);
}

fn fold<A, S: list<int>>(
  initial: A,
  body: (A, coord<S>) -> A,
  shape: S
) -> A;
```

`build` defines an element for every logical coordinate. `at` reads one
element. `fold` performs an ordered accumulation over an explicit logical
shape. Its shape is a compiler-time argument and therefore lives on the
specialized callee value rather than as an Op attribute. No associativity or
reassociation permission is implicit. `input[position]` is a bodyful symbolic
overload defined through `at`; the language core knows nothing about Tensor
indexing.

The bodyful `map` is derived from only `build` and `at`:

```joggle
fn map<E, S: list<int>, R>(
  input: tensor<E, S>,
  body: (E) -> R
) -> tensor<R, S> {
  return build(
    S,
    (position: coord<S>) => body(at(input, position))
  );
}
```

Typed lambdas are nested `Fn` values. Their free tensor and callback values are
explicit capture edges, so materialization, verification, inlining, and later
dependence analysis all inspect one representation.

## What is deliberately absent

Tensor does not declare Conv, Relu, GEMM, pooling, concatenation, reshape,
softmax, model constants, quantization, layouts, kernels, buffers, devices, or
emitters. Domain Mods define those conveniences by composing the calculus.
ONNX symbols belong to `onnx`; scalar leaves belong to `arith`.

Current tests prove body inspection and generic inlining for `map`, express a
dot product as `fold + at + scalar operators`, and materialize coordinate
construction, coordinate projection, and tensor indexing as ordinary Calls.
Execution, symbolic shapes, views, dependence-checked fusion, storage planning,
and scheduling remain future gates rather than implied capabilities.
