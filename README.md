# Joggle

Joggle is a lightweight C++ compiler framework for AI hardware/software
co-design. Its user model has four public concepts:

- `Module` is a versioned package of types, functions, and contracts.
- `fn` is the one callable declaration used for program operations and
  compiler work.
- values are either known to the compiler or residualized into a function
  body; availability is independent of type;
- `Compiler` loads Modules, checks programs, binds behavior, and invokes
  functions.

There is no fixed frontend/backend split and no built-in lowering direction.
A function body may use declarations from any installed Modules. A bridge
Module can import two vocabularies and provide conversions in either direction.
Input decoding, transformation, analysis, and output encoding are ordinary
typed functions over module-owned types. The source language does not impose a
frontend/backend direction, a graph domain, or a second pass namespace.

The C++ IR follows `Module -> Function -> Block -> Instruction/Value`.
`Graph` and `Region` are not ownership objects: def-use and control-flow graphs
are relationships over a Function and may be exposed by non-owning analysis
views. The normative architecture and staging semantics are specified in
[the design](docs/design.md) and
[the execution model](docs/execution-model.md).

The language directly supports `i1/i8/i16/i32/i64`, `u8/u16/u32/u64`,
`f16/bf16/f32/f64`, and `index` as program value types. They require no import.
Custom scalar formats implement the ambient `prelude.scalar` interface and can
then participate in interface-constrained generic operations.

```joggle
joggle 1;

module example@1.0.0 {
  fn main(condition: i1, lhs: i32, rhs: i32) -> i32 {
    result = if condition { lhs } else { rhs };
    return result;
  }
}
```

Reusable IR packages live in [`modules`](modules):

- `arith` provides scalar operations over native and registered scalar types;
- `tensor` owns tensor value and shape semantics, not neural-network operators;
- `nn` provides common inference contracts and checked NCHW shape relations;
- `buffer` owns explicit storage values and token-ordered memory effects,
  without a device model or capacity assumptions.

These Modules are ordinary `.joggle` packages rather than C++ built-ins. The
trusted kernel owns bootstrap host representations and checked expression
evaluation; the automatically linked `prelude` Module owns reflected compiler
value types, native scalar declarations, and their interfaces.
Expression-bodied `fn` declarations and
derived type parameters can compute dependent type arguments without host
callbacks. A call prefixed by `@` is evaluated at compile time; the declaration
needs no separate `const` function kind. Fixed-width Prelude scalars expose the
`storage_bits` field through this same mechanism; custom formats implement the
identical interface from their own parameters.

Build and test with standard CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /desired/prefix
```

Useful commands:

```bash
joggle fmt module.joggle --write
joggle check modules/arith.joggle
joggle install modules/arith.joggle
joggle uninstall arith@1.0.0
joggle lock root.joggle -o joggle.lock
joggle run program.joggle main transform_fn -o transformed.joggle
```

Start with [the design](docs/design.md),
[the execution model](docs/execution-model.md),
[the language reference](docs/language.md),
[IR Modules](docs/ir-modules.md), [passes](docs/passes.md), and
[the C++ API](docs/cpp-api.md). Package identity and behavior libraries are
specified in [packages](docs/packages.md) and [bindings](docs/bindings.md).
