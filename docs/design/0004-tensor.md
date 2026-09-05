# Design 0004: Tensor semantics

Status: accepted; implementation in progress

## Decision

Joggle represents target-independent tensor computation with a small
installable calculus, not an operation catalog. Compiler core gains no tensor
declaration kind, graph container, attribute dictionary, or lowering hook.

`tensor@3` owns only:

```joggle
type tensor(element: type, shape: list<int>);
type coord(shape: list<int>);

fn coord<S: list<int>>(shape: S, values: index...) -> coord<S>;
fn ([])<S: list<int>>(position: coord<S>, axis: int) -> index;
fn build<E, S: list<int>>(
  shape: S, body: (coord<S>) -> E
) -> tensor<E, S>;
fn at<E, S: list<int>>(input: tensor<E, S>, position: coord<S>) -> E;
fn ([])<E, S: list<int>>(
  input: tensor<E, S>, position: coord<S>
) -> E {
  return at(input, position);
}
fn fold<A, S: list<int>>(
  initial: A,
  body: (A, coord<S>) -> A,
  shape: S
) -> A;
```

`build` and `fold` take their logical domains explicitly. A computed shape
therefore participates in ordinary overload inference before its nested lambda
is materialized, and loop transformations have a visible domain to rewrite.
The shape is a normal compiler-time fn argument stored on the specialized
callee `Val`; no Domain or Axis object is introduced.
`position[axis]` and `input[position]` resolve through the same overload system
as `+`: their IR is an ordinary Call to a fn named `[]`. The parser provides
only the bracket spelling and does not know coordinate or Tensor semantics.

## Derived computation

Higher-level computation is an ordinary bodyful fn. `map` is already defined
through `build` and `at`. A dot product can be written without registering a
Dot or GEMM operation:

```joggle
fn dot(lhs: tensor<f32, [4]>, rhs: tensor<f32, [4]>, initial: f32) -> f32 {
  return fold(
    initial,
    (sum: f32, p: coord<[4]>) => sum + at(lhs, p) * at(rhs, p),
    shape: [4]
  );
}
```

The update lambda is a real nested `Fn`; `lhs` and `rhs` are explicit capture
edges. Scalar operators resolve through ordinary imported overloads. Domain
libraries such as ONNX may expose named Conv or Relu functions, but every
optimizable function must expand into this calculus or remain visibly opaque.

## Transformation consequence

Inlining exposes construction, access, reduction, and scalar calls in the same
caller. Fusion composes a producer `build` body into consumer `at` uses after
checking index dependence and effects. It does not manufacture `conv_relu`,
match a high-level function name, or create a second pattern IR.

Ordered `fold` makes no reassociation promise. Parallel reduction is legal only
after a transformation obtains an explicit algebraic contract or proves the
property for the concrete scalar implementation.

## Boundaries

Logical shape does not imply layout, packing, address space, storage capacity,
allocation, or device placement. Model constants and ONNX operators belong to
the ONNX Mod; scalar leaves belong to Arith. Later format and machine Mods may
introduce ordinary Types and fns without changing tensor or compiler core.

## Gates

- [x] Typed lambdas are nested Fns with explicit captures.
- [x] Define static tensor and coordinate Types.
- [x] Express coordinate construction, coordinate projection, and Tensor
  indexing as ordinary overloaded fns.
- [x] Define `build`, `at`, and ordered `fold` as the minimal basis.
- [x] Express generic `map` with an inspectable body.
- [x] Express and verify a dot-product body using only the basis and scalar
  overloads.
- [ ] Add symbolic logical extents without a second shape AST.
- [ ] Verify that coordinate constructor arity agrees with the static rank.
- [ ] Define view/index-map composition.
- [x] Express rank-two ONNX MatMul as a bodyful library fn.
- [ ] Express convolution and batched/broadcast MatMul as bodyful library fns.
- [ ] Implement dependence-checked `build`/`at` fusion.
- [ ] Differentially validate transformed real-model computation.
