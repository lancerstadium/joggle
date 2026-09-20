---
title: Functions
description: Write definitions, declarations, multiple results, generics, and overloads.
---

# Functions

## Definition

```jog
fn affine(x: f32, scale: f32, bias: f32) -> f32 {
  let scaled = x * scale
  return scaled + bias
}
```

Parameters and results are typed. A body contains ordinary statements and must
return values matching the declared result list.

## Declaration without a body

```jog
fn vendor_gemm(a: tensor<f32, [M, K]>, b: tensor<f32, [K, N]>)
  -> tensor<f32, [M, N]>;
```

A body-less declaration describes a semantic or native boundary. Another
package may supply a compatible body, or a native mod may bind it. It is not an
automatic external call until a selected workflow gives it that role.

## No result and multiple results

```jog
fn notify() -> () {
  return
}

fn quotient_remainder(x: i64, y: i64) -> (i64, i64) {
  return x / y, x % y
}

fn rebuild(x: i64, y: i64) -> i64 {
  let q, r = quotient_remainder(x, y)
  return q * y + r
}
```

## Generics

```jog
fn identity<T: Ty>(x: T) -> T {
  return x
}
```

Generic parameters are compile-time values with types such as `Ty`, `int`, or
`str`. They are inferred by structural unification unless passed explicitly.

## Overloads

```jog
fn +<T: Ty>(a: T, b: T) -> T;
fn +<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
```

Resolution filters by arity and structural unification, then chooses the most
specific signature. Return types do not distinguish overloads. Equally
specific matches are an error instead of depending on load order.

## Compile-time functions are ordinary functions

```jog
fn count(m: Mod) -> int { return len(ir.ops(m)) }
```

`Mod` in a signature does not introduce new syntax. The CLI decides whether a
named function is invoked as `query`, `run`, or `emit`. Continue with
[Types](types.md).
