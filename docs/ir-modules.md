# IR Modules

Joggle has one Function IR and any number of installable vocabulary Modules.
`joggle::Module` owns declarations, contracts, versions, and optional behavior;
it is not a graph container or a fixed compiler level. The installable `ir`
Module separately declares the value type `ir.module`, represented in C++ by
`joggle::ir::Module`, for a named set of executable Functions.

## Ambient Prelude

The compiler automatically links the `prelude` Module. It declares compiler
domains and common program scalar types:

```text
compiler domains  int real bool string type attr bytes function list<T>
program types     i1 i8 i16 i32 i64 u8 u16 u32 u64 f16 bf16 f32 f64 index
```

These are reflected Module declarations rather than a parallel C++ type table.
Program scalar spelling is ambient, so source writes `i32`. Parameterized
formats remain ordinary extension types:

```joggle
type posit(total_bits: int, exponent_bits: int) : prelude.scalar {
  storage_bits = total_bits;
}
```

A generic function can use the same interface for Prelude and custom scalars:

```joggle
fn add<T: prelude.scalar>(lhs: T, rhs: T) -> T as +;
```

## Shipped vocabularies

The sources in [`modules`](../modules) are normal installable packages:

| Module | Owns | Does not assume |
|---|---|---|
| `arith` | scalar computation contracts | tensors, devices, schedules |
| `tensor` | tensor values and shape transformations | allocation, NN operators |
| `nn` | inference operation contracts and explicit layouts | file formats, devices |
| `buffer` | storage values and token-ordered effects | capacities, banks, targets |
| `ir` | a named executable-Function artifact | vocabulary levels, targets, pipelines |

An Instruction is identified by the Function declaration it calls. A single
Function may contain Instructions from several imported vocabularies, enabling
partial and bidirectional conversion without a global IR-level enum.

## Conversions and I/O

Loading, conversion, analysis, simulation, and emission are ordinary functions
over typed values. Conceptually:

```joggle
module onnx_io@1.0.0 {
  import ir@1;
  fn read(input: bytes) -> ir.module;
  fn write(input: ir.module) -> bytes;
}
```

`ir.module` has the standard C++ representation `joggle::ir::Module`. It owns
named `joggle::Function` values and is copy-on-write: an ordinary compiler
function may accept and return it by value without cloning every Function, and
the first mutable lookup detaches only the selected Function. Loaders,
transformations, analyses, and emitters can therefore use the same ordinary
function invocation mechanism.

A bridge owns only the relation it implements:

```joggle
module a_b@1.0.0 {
  import a@1;
  import b@1;

  fn to_a(input: ir.module) -> ir.module;
  fn to_b(input: ir.module) -> ir.module;
}
```

There is no core `lower`, frontend, backend, or Graph category. Direction is a
property of the called function and the types at that call site.

## Extension rules

1. Put reusable declarations in `modules/` and compiler-only fixtures in
   `tests/fixtures/`.
2. Name a Module after the semantics it owns, not a pipeline position.
3. Put structural contracts in `.joggle`; attach C++ only for behavior that
   cannot be expressed declaratively.
4. Keep target facts in target Modules. The core does not know capacities,
   lanes, instruction sets, or scheduling policies.
5. Put cross-vocabulary relations in bridge Modules instead of creating cyclic
   imports.
6. Treat canonical source, semantic version, and digest as package identity.
