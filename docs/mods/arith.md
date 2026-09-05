# Arith

`arith@1.2.0` is the scalar-computation boundary used by domain libraries.
It declares ordinary overloadable fns rather than adding numeric Op kinds to
compiler core:

```joggle
fn zero<T>() -> T;
fn zero<T>(like: T) -> T;
fn literal<T>(value: int) -> T;
fn max<T>(lhs: T, rhs: T) -> T;
fn (+)<T>(lhs: T, rhs: T) -> T;
fn (*)<T>(lhs: T, rhs: T) -> T;
fn (<)<T>(lhs: T, rhs: T) -> i1;
fn (>)<T>(lhs: T, rhs: T) -> i1;
```

The zero-argument overload obtains `T` from its expected result Type, while the
one-argument overload obtains it from a residual exemplar. These generic
declarations make the current tensor-calculus slice independent of one fixed
scalar format. A format Mod may provide more specific overloads.
`literal`, comparison, and addition are also sufficient to materialize an
ordinary typed counted loop. Algebraic laws such as reassociation are not
implied; a loop-carried accumulation remains ordered unless a later
transformation has enough evidence to change it safely.
