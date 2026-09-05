# Arith

`arith@1.1.0` is the scalar-computation boundary used by domain libraries.
It declares ordinary overloadable fns rather than adding numeric Op kinds to
compiler core:

```joggle
fn zero<T>() -> T;
fn zero<T>(like: T) -> T;
fn max<T>(lhs: T, rhs: T) -> T;
fn (+)<T>(lhs: T, rhs: T) -> T;
fn (*)<T>(lhs: T, rhs: T) -> T;
```

The zero-argument overload obtains `T` from its expected result Type, while the
one-argument overload obtains it from a residual exemplar. These generic
declarations make the current tensor-calculus slice independent of one fixed
scalar format. A format Mod may provide more specific overloads.
Algebraic laws such as reassociation are not implied: `reduce` remains ordered
unless a later transformation has enough evidence to change it safely.
