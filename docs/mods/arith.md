# Arith

`arith@2` is the scalar-computation boundary used by domain libraries.
It declares ordinary overloadable fns rather than adding numeric Op kinds to
compiler core:

```joggle
pub fn zero<T>() -> T;
pub fn zero<T>(like: T) -> T;
pub fn literal<T>(value: int) -> T;
pub fn max<T>(lhs: T, rhs: T) -> T;
pub fn min<T>(lhs: T, rhs: T) -> T;
pub fn select<T>(condition: i1, consequent: T, alternative: T) -> T;
pub fn (+)<T>(lhs: T, rhs: T) -> T;
pub fn (-)<T>(lhs: T, rhs: T) -> T;
pub fn (*)<T>(lhs: T, rhs: T) -> T;
pub fn (//)<T>(lhs: T, rhs: T) -> T;
pub fn (<)<T>(lhs: T, rhs: T) -> i1;
pub fn (>)<T>(lhs: T, rhs: T) -> i1;
pub fn (==)<T>(lhs: T, rhs: T) -> i1;
pub fn (&&)(lhs: i1, rhs: i1) -> i1;
```

The zero-argument overload obtains `T` from its expected result Type, while the
one-argument overload obtains it from a residual exemplar. These generic
declarations make the current tensor-calculus slice independent of one fixed
scalar format. A format Mod may provide more specific overloads.
The additional integer-style operations and `select` are still declarations,
not built-in instruction semantics. They let a portable tensor body describe
index arithmetic and predicated scalar work while a target package remains
free to select its own implementation. Algebraic laws such as reassociation
are not implied; a loop-carried accumulation remains ordered unless a later
transformation has enough evidence to change it safely.
