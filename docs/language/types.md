---
title: Types
description: Define and use structural types, generics, constructors, open terms, and type refinement.
---

# Types

Joggle types are immutable trees. The core parses, compares, unifies,
substitutes, and prints those trees; mods define what each constructor means.

## Scalar types

```jog
fn negate_i32(x: i32) -> i32 { return -x }

fn widen(x: i32) -> i64 {
  return i64(x)
}

fn mix(x: f32, y: f32) -> f32 {
  return x * f32(0.5) + y * f32(0.5)
}
```

Explicit constructors such as `i64(x)` and `f32(0.5)` make width visible to
overload resolution.

## Define a type constructor

A body-less function returning `Ty` declares a type constructor. Its generic
parameters become the constructor arguments:

```jog
mod pair

fn pair<A: Ty, B: Ty>() -> Ty;
```

This creates a family, including `pair<i32, bool>` and nested
`pair<f32, pair<i32, i32>>`. It does not allocate storage or define operations.
Add functions over the type separately:

```jog
fn make<A: Ty, B: Ty>(left: A, right: B) -> pair<A, B>;
fn first<A: Ty, B: Ty>(value: pair<A, B>) -> A;
fn second<A: Ty, B: Ty>(value: pair<A, B>) -> B;
```

The declarations describe semantics. A source implementation, native binding,
conversion, or target companion may supply behavior later.

## Value-parameterized types

Generic terms need not themselves be types:

```jog
mod sat

fn sat<W: int>() -> Ty;
fn clamp<W: int>(value: int) -> sat<W>;
fn +<W: int>(left: sat<W>, right: sat<W>) -> sat<W>;
```

`sat<8>` and `sat<16>` share a constructor but are distinct types. Type and
value terms can be combined:

```jog
fn vector<E: Ty, N: int>() -> Ty;
fn splat<E: Ty, N: int>(value: E) -> vector<E, N>;
fn lane<E: Ty, N: int>(value: vector<E, N>, index: int) -> E;
```

## Structural trees

```jog
tensor<f32, [2, N]>
```

```text
tensor
├── f32
└── []
    ├── 2
    └── N
```

The core sees constructor `tensor`, type term `f32`, and a list term containing
an integer literal and generic integer. There is no separate shape language.

## Generic matching

```jog
fn transpose<E: Ty, M: int, N: int>(
  input: tensor<E, [M, N]>
) -> tensor<E, [N, M]>;
```

For `tensor<f32, [2, 3]>`, unification binds `E=f32`, `M=2`, and `N=3`; the
result becomes `tensor<f32, [3, 2]>`.

More-specific overloads win:

```jog
fn keep<T: Ty>(x: T) -> T { return x }
fn keep<W: int>(x: sat<W>) -> sat<W> { return x }
```

`keep(sat<8>)` selects the second function. Equal specificity is an ambiguity
error. Result types never disambiguate a call.

## Construct and inspect `Ty`

```jog
fn describe(type: Ty) -> dict {
  return {
    constructor: name(type),
    arguments: args(type),
    spelling: text(type)
  }
}

let i32_type = ty("i32")
let width = ty(8)
let sat8 = ty("sat", [width])
let pair_type = ty("pair", [i32_type, sat8])
```

| Function | Contract |
| --- | --- |
| `ty(text)` | parse and validate canonical type text |
| `ty(integer)` | create an integer term |
| `ty(name, terms)` | create one validated tree node |
| `name(type)` | return the constructor name |
| `args(type)` | return direct child terms |
| `int(type)` | project an integer term or fail |
| `text(type)` | return canonical source spelling |

## Open types

`_` means “not yet proved”:

```jog
fn source_call(input: _) -> _;
```

It is allowed at intermediate boundaries and is not a rewrite wildcard. Find
remaining open results with:

```sh
joggle query opt.untyped source.jog -M build/modules
```

## Refine graph types

```jog
fn refine(m: Mod, value: Val, proved: Ty) -> bool {
  return ir.type(m, value, proved)
}

fn refine_all(m: Mod, values: list<Val>, types: list<Ty>) -> bool {
  return ir.type(m, values, types)
}
```

The batch form validates ownership, lengths, carried-value families, and
conflicts before one atomic change. `ir.returns` updates result signatures;
verification checks every return path.

## The same tree in C++

```cpp
joggle::Ty parsed("sat<8>");
std::array<joggle::Ty, 1> terms{joggle::Ty("8")};
joggle::Ty structured("sat", terms);

if (!parsed.valid() || parsed != structured) return false;
std::string constructor = parsed.name();
std::span<const joggle::Ty> arguments = parsed.args();
```

Use the structured constructor when child terms already exist; do not print and
reparse them. Target representation does not belong in the semantic tree:
`sat` owns meaning, while `sat.c` and `sat.vm` own target preparation.

Continue with [Values and collections](values.md), or see the
[`sat` mod API](../api/mods/numeric/sat.md).
