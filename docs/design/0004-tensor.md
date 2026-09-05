# Design 0004: Tensor semantics

Status: accepted; implementation in progress

## Decision

Joggle represents target-independent tensor computation with a small
installable calculus, not an operation catalog. Compiler core gains no tensor
declaration kind, graph container, attribute dictionary, or lowering hook.

`tensor@2` owns only:

```joggle
type tensor(element: type, shape: list<int>);
type coord(shape: list<int>);

fn build<E, S: list<int>>(body: (coord<S>) -> E) -> tensor<E, S>;
fn at<E, S: list<int>>(input: tensor<E, S>, position: coord<S>) -> E;
fn fold<A, S: list<int>>(
  initial: A,
  body: (A, coord<S>) -> A,
  shape: S
) -> A;
```

`fold` takes its logical domain explicitly because `S` cannot be inferred from
an accumulator result. The shape is a normal compiler-time fn argument stored
on the specialized callee `Val`; no Domain or Axis object is introduced.

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
- [x] Define `build`, `at`, and ordered `fold` as the minimal basis.
- [x] Express generic `map` with an inspectable body.
- [x] Express and verify a dot-product body using only the basis and scalar
  overloads.
- [ ] Add symbolic logical extents without a second shape AST.
- [ ] Define view/index-map composition.
- [ ] Express GEMM and convolution as bodyful library fns.
- [ ] Implement dependence-checked `build`/`at` fusion.
- [ ] Differentially validate transformed real-model computation.
