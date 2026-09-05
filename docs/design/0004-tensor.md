# Design 0004: Tensor semantics

Status: accepted; implementation in progress

## Decision

Joggle represents target-independent tensor computation with a small
installable calculus, not an operation catalog. Compiler core gains no tensor
declaration kind, graph container, attribute dictionary, or lowering hook.

`tensor@4` owns only:

```joggle
type tensor(element: type, shape: list<int>);
type coord(shape: list<int>);

fn coord<S: list<int>>(shape: S, values: index...) -> coord<S>;
fn ([])<S: list<int>>(position: coord<S>, axis: int) -> index;
fn map<E, S: list<int>>(
  shape: S, body: (coord<S>) -> E
) -> tensor<E, S>;
fn ([])<E, S: list<int>>(
  input: tensor<E, S>, position: coord<S>
) -> E;
fn reduce<E, S: list<int>, A>(
  input: tensor<E, S>,
  initial: A,
  body: (A, E) -> A
) -> A;
fn map<E, S: list<int>, R>(
  input: tensor<E, S>, body: (E) -> R
) -> tensor<R, S> {
  return map(S, (position: coord<S>) => body(input[position]));
}
```

Domain `map` takes its logical domain first. A computed shape therefore
participates in ordinary overload inference before its nested lambda is
materialized, and loop transformations have a visible domain to rewrite. The
shape is a normal compiler-time fn argument stored on the specialized callee
`Val`; no Domain or Axis object is introduced. `reduce` consumes a Tensor, so
the reduction domain is carried once by its Type rather than repeated at the
call site.
`position[axis]` and `input[position]` resolve through the same overload system
as `+`: their IR is an ordinary Call to a fn named `[]`. The parser provides
only the bracket spelling and does not know coordinate or Tensor semantics.

## Algebra boundary

The basis is a rank-polymorphic pull-array algebra:

```text
map(S, f)[p]       = f(p)
map(map(S, f), g)  = map(S, p => g(f(p)))
```

This adopts Lift/RISE's essential choice—functional patterns remain visible
and optimization is equational rewriting—without fixing OpenCL-specific
patterns in compiler core. One overloaded `map` covers both logical-domain
generation and collection mapping; there is no separate `build` vocabulary.
`reduce` deliberately means deterministic ordered accumulation. Its name does
not silently assert associativity or license reassociation.

Patterns such as zip, reindex, window, split/join, sequential/parallel map, or
machine instructions are ordinary fns supplied by Tensor or later Mods. They
are admitted only when they enable a real bodyful workload and generic
equations; they do not become C++ Op kinds. This keeps the extension plane open
while avoiding a prematurely fixed pattern catalog.

Primary precedents are [Lift's functional data-parallel
IR](https://michel-steuwer.github.io/files/publications/2017/CGO-2017.pdf), its
[rewrite-rule algebra](https://lift-project.github.io/publications/2015/steuwer15generating.pdf),
and [RISE/Shine's language-oriented functional-to-imperative
design](https://arxiv.org/abs/2201.03611).

## Derived computation

Higher-level computation is an ordinary bodyful fn. Tensor `map` is already
defined through domain `map` and `[]`. A dot product can be written without
registering a Dot or GEMM operation:

```joggle
fn dot(lhs: tensor<f32, [4]>, rhs: tensor<f32, [4]>, initial: f32) -> f32 {
  products: tensor<f32, [4]> = map(
    [4],
    (p: coord<[4]>) => lhs[p] * rhs[p]
  );
  return reduce(
    products,
    initial,
    (sum: f32, value: f32) -> f32 => sum + value
  );
}
```

Both lambdas are real nested `Fn`s; `lhs` and `rhs` are explicit capture edges
of the producer. Scalar operators resolve through ordinary imported overloads.
Domain libraries such as ONNX may expose named Conv or Relu functions, but
every optimizable function must expand into this calculus or remain visibly
opaque.

## Transformation consequence

Inlining exposes construction, access, reduction, and scalar calls in the same
caller. Fusion composes a producer domain-`map` body into consumer `[]` uses after
checking index dependence and effects. It does not manufacture `conv_relu`,
match a high-level function name, or create a second pattern IR.

Ordered `reduce` makes no reassociation promise. Parallel reduction is legal only
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
- [x] Define overloaded `map`, overloaded `[]`, and ordered `reduce` as the
  minimal basis.
- [x] Express generic `map` with an inspectable body.
- [x] Express and verify a dot-product body using only the basis and scalar
  overloads.
- [ ] Add symbolic logical extents without a second shape AST.
- [ ] Verify that coordinate constructor arity agrees with the static rank.
- [ ] Define view/index-map composition.
- [x] Express rank-two ONNX MatMul as a bodyful library fn.
- [ ] Express convolution and batched/broadcast MatMul as bodyful library fns.
- [ ] Implement dependence-checked domain-`map`/`[]` fusion.
- [ ] Differentially validate transformed real-model computation.
