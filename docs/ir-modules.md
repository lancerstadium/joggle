# IR Modules

Joggle has one runtime program representation, `Graph`, and any number of
installable IR Modules. A Module is a namespace, schema, version, package, and
function-registration boundary. It is not a second graph container and it does not
occupy a fixed level in a compiler stack.

## Ambient types

The compiler automatically links one canonical bootstrap Module containing the
scalar types required to write a useful Graph:

```text
i1 i8 i16 i32 i64
u8 u16 u32 u64
f16 bf16 f32 f64
index
```

Their declarations belong to the ambient `prelude` Module, not to a parallel
C++ type table. Source uses the short spelling (`i32`, not `prelude.i32`), and
no import is recorded.
The Module also exposes `prelude.scalar` plus classification interfaces such as
`prelude.integer` and `prelude.floating_point`. A custom type may implement
those interfaces:

```joggle
type posit(total_bits: int, exponent_bits: int) : prelude.scalar {
  storage_bits = total_bits;
}
```

An IR function can then state the real contract rather than accepting every
possible type:

```joggle
fn add<T: prelude.scalar>(lhs: T, rhs: T) -> T as +;
```

The type solver checks this constraint during graph construction and parsing.
The term *native* is only a convenience for these ambient Prelude declarations;
parameterized formats remain ordinary Module types and use exactly the same
runtime `Type` handle. The `prelude.scalar.storage_bits` field is implemented
in source for every fixed-width Prelude type, so a constrained
generic can derive parameters from `i32` and a custom `posit` identically.
`index` does not implement `prelude.scalar` because its width belongs to a
selected target rather than the ambient language.

## Shipped Modules

The reusable sources in [`modules`](../modules) are installable packages, not
compiler internals or test fixtures:

| Module | Owns | Deliberately does not own |
|---|---|---|
| `arith` | scalar computation contracts | tensors, devices, schedules |
| `tensor` | ranked/unranked tensor values and shape transforms | NN operators, storage |
| `nn` | common inference operations and explicit NCHW shape relations | file formats, devices, schedules, storage |
| `buffer` | storage values and explicit token-ordered effects | capacities, banks, devices |

The separation is semantic. A tensor value is not assumed to be allocated; a
buffer is not assumed to live on a particular device; and an arithmetic
operation is not assumed to be a machine instruction.

Both value vocabularies expose their structural facts through declarative type
interfaces. `tensor.ranked_tensor` provides `element_type` and `shape`;
`buffer.storage` additionally provides `address_space`. These fields use
`type`, `list<int>`, and `string` through the same parameter system. A bridge or target
Module can therefore accept a compatible custom representation without adding
a C++ base class or recognizing the concrete declaration name.

The shipped `nn` Module is a vocabulary, not a frontend. Layout-bearing
operations say so in their names (`conv2d_nchw`, `batch_norm_nchw`) instead of
hiding dimension order in compiler state. Its result shapes are ordinary type
expressions built from operand dimensions, properties, and the pure
`conv_extent` function. A parser for ONNX or another interchange format belongs
in its own Module as a typed `bytes -> graph` function and may choose `nn`, another
IR vocabulary, or a mixed Graph as its result. The core does not depend on that
format library.

## Conversions are Modules

Joggle does not define high IR, low IR, frontend, backend, or `lower` as core
categories. A compiler function may replace any operations visible through its
Module's imports. A bridge Module imports the vocabularies it connects and owns
the conversion functions:

```joggle
joggle 1;

module a_b@1.0.0 {
  import a@1;
  import b@1;

  fn to_a(input: graph) -> graph;
  fn to_b(input: graph) -> graph;
}
```

This avoids cyclic dependencies between `a` and `b`, permits bidirectional or
partial conversions, and keeps conversion policy out of both vocabularies.
Mixed-Module Graphs make partial conversion explicit: unsupported operations
remain visible instead of being hidden behind a phase boundary.

## Extension rules

1. Put reusable declarations in `modules/`, examples in documentation, and
   compiler-only fixtures in `tests/fixtures/`.
2. Name a Module after the semantics it owns, not a pipeline position.
3. Put structural contracts in `.joggle`; attach C++ only for semantics that
   cannot be expressed declaratively.
4. Keep target facts in target Modules. Core APIs do not know lane counts,
   storage capacities, instruction sets, or scheduling vocabularies.
5. Put a cross-vocabulary conversion in a bridge Module rather than making one
   vocabulary depend on the other.
6. Treat canonical source, semantic version, and digest as the package
   identity. Behavior code may implement declarations but cannot mutate them.
