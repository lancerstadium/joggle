# Joggle type system

This document defines how one function syntax covers compiler-known values and
program values without asking users to select an IR category.

## Two semantic levels, one parameter syntax

Joggle needs values at two times:

```text
compiler-known values   widths, shapes, types, attributes, programs, bytes
program values          inputs and results represented in a dataflow body
```

Both use `name: annotation`. The annotation determines the level:

```joggle
fn example<N: int>(
  width: N,
  input: tensor<f32, [N]>
) -> tensor<f32, [N]>;
```

`width` is known to the compiler because `N` has domain `int`. `input`
is a program value because its annotation evaluates to a type. No `%`,
`static`, `operand`, or `property` marker is part of the source language.

Internally the compiler keeps these levels distinct. Program data cannot
silently become a compiler-known value, because that would make static types
depend on runtime data.

## Trusted compiler domains

The language kernel defines:

```text
int  real  bool  string  type  attr  graph  bytes  list<D>
```

These are domains used by the compiler, not target-specific types. `int`
describes values such as a bit width; `i32` is an ordinary type declaration
from `prelude`. `graph` and `bytes` let ordinary functions express
decoding, transformation, analysis, and emission without adding pass or backend
keywords to the language.

The kernel is deliberately small and fixed by the Joggle language version.
Integer arithmetic is checked for overflow and division by zero. Real
arithmetic must remain finite.

## Prelude and user types

Native scalar names are declarations in the ambient `prelude` Module:

```text
i1 i8 i16 i32 i64 u8 u16 u32 u64 f16 bf16 f32 f64 index
```

They use the same machinery as user types:

```joggle
type packed(width: int) : prelude.scalar {
  storage_bits = width;
}
```

There is no parallel C++ enum of privileged scalar types. Prelude is embedded
only so common spellings need no explicit import.

## Constructors and identity

A type is a constructor over compiler-known values:

```joggle
type fixed(width: int, fraction: int, signed: bool = true);
type tensor(element: type, shape: list<int>);
```

An instance is identified by:

```text
(Module identity, type declaration, canonical constructor arguments)
```

Defaults are applied before identity is formed. Constructor arguments are
immutable and available through `Type::get`.

Attributes use the same parameter calculus but inhabit the `attr` domain.

## Generics

A generic declares the domain of a symbolic value:

```joggle
fn reshape<
  E: prelude.scalar,
  From: list<int>,
  To: list<int>
>(
  input: tensor<E, From>,
  shape: To
) -> tensor<E, To>;
```

- `E` ranges over types conforming to `prelude.scalar`.
- `From` and `To` range over integer lists.
- `input` binds `E` and `From` through its program type.
- `shape: To` is a compiler-known argument that binds `To`.

The implementation may store a domain and a binding expression separately,
but the user sees only `shape: To`.

## Interface fields

A type interface declares compiler-known facts:

```joggle
interface numeric_format: type {
  storage_bits: int;
  is_signed: bool;
}
```

A conforming type supplies a field either through an identically named
constructor parameter or through a derived expression:

```joggle
type integer(storage_bits: int, is_signed: bool) : numeric_format;

type packed(width: int, signed: bool) : numeric_format {
  storage_bits = width;
  is_signed = signed;
}
```

Constrained generics read fields with ordinary member notation:

```joggle
type word(bits: int);
fn encode<T: numeric_format>(input: T) -> word<T.storage_bits>;
```

Derived fields are deterministic parts of the declaration. Host callbacks do
not participate in type identity or inference.

## Compile-time evaluation

A function over compiler-known inputs can be evaluated from its ordinary block
body:

```joggle
fn align(value: int, multiple: int) -> int {
  return ceildiv(value, multiple) * multiple;
}

fn widen<W: int>(input: word<W>) -> word<@(align(W + 1, 8))>;
```

There is no separate `const fn` declaration. The signature and body are
sufficient. `@(expression)` makes the Known-value requirement explicit at the
use site.
Calls resolve through the declaring Module and its imports; recursion is
rejected.

The solver first binds generics from program inputs and named compiler
arguments, then evaluates result annotations. It does not invert arbitrary
arithmetic. For example, it can evaluate `word<W + 1>` after binding `W`,
but it does not solve `W` backwards from an expected `word<8>`.

## Program-value checking

A function parameter whose annotation evaluates to a type is a program input;
a result annotation is checked the same way:

```joggle
fn add<T: prelude.scalar>(lhs: T, rhs: T) -> T as +;
```

For a dataflow call, positional local names bind program inputs and named
arguments bind compiler-known inputs:

```joggle
output = conv2d(input, weight, stride_h = 2);
```

The same type-contract solver checks calls parsed from source, calls built
through C++, and results produced by transformations.

Local variables in a dataflow body are single-assignment and lexically scoped.
That invariant makes dependency order and nested-region capture unambiguous,
but it remains an internal graph property rather than a sigil-based user
language.

## Bootstrap boundary

Joggle is self-describing above a finite kernel:

```text
trusted compiler domains and evaluator
    -> ambient Prelude Module
        -> installable and user-defined Modules
```

The kernel is not defined in terms of user types. Everything above it,
including native scalars, custom formats, tensors, buffers, operations, and
compiler transformations, is declared through ordinary Modules and `fn`.
