# Joggle

Joggle is a small C++ compiler substrate for AI software/hardware co-design.
It provides one versioned `Module` abstraction, one typed `fn` declaration,
staged Known/Residual execution, editable CFG/SSA bodies, and installable native
implementations.

The project deliberately does not prescribe a graph hierarchy, lowering
ladder, target base class, pass registry, kernel object, device model, or
deployment container. AI vocabularies and hardware experiments are installable
Modules rather than compiler-core categories.

## Source model

```joggle
joggle 1;

module example@1.0.0 {
  type word(width: int);

  fn (+)(lhs: word<8>, rhs: word<8>) -> word<8>;

  fn twice(input: word<8>) -> word<8> {
    return input + input;
  }
}
```

`fn` is used for residual computation and compiler-time work. Prefix `@`
requests compile-time execution; an ordinary call remains a program call even
when its operands happen to be known. This explicit-staging rule is implemented
and covered by both positive and negative materialization tests.

A `Module` owns declarations, materialized Function bodies, imports, and
content-addressed immutable data. There is no second Program, Graph, Package,
or Artifact owner.

## Compiler extension model

An extension consists of one `.joggle` Module and, only when required, one
native library implementing bodyless compiler functions. The source Module is
the schema authority; no generated declaration header is required.

Whole-module import, conversion, optimization, analysis, simulation, and
emission remain ordinary functions:

```joggle
fn read(input: bytes) -> module;
fn optimize(input: module, policy: type) -> module;
fn estimate(input: module, machine: type) -> bytes;
fn emit(input: module, machine: type) -> bytes;

fn compile(input: bytes, machine: type) -> bytes {
  model = @read(input);
  optimized = @optimize(model, machine);
  return @emit(optimized, machine);
}
```

The user controls composition in source. Joggle does not discover magic pass
names or force a universal `graph -> tensor -> loop -> target` sequence.

`Compiler::specialize` recursively materializes source-defined calls until a
caller-supplied predicate accepts every remaining Function. This lets a target
declare its primitive capability boundary without maintaining a lowering entry
for each source kernel.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The repository currently ships only the compiler core. AI modules will return
only after their public contracts have been derived from executable use cases
and reviewed independently of compiler-core implementation details.

## Documentation

- [Architecture](docs/architecture.md)
- [Language reference](docs/language.md)
- [IR model](docs/ir.md)
- [Compiler functions](docs/compiler-functions.md)
- [C++ API](docs/cpp-api.md)
- [Module design](docs/modules.md)
- [Repository and reproducibility](docs/module-repository.md)
- [Research position](docs/research-position.md)
- [Accepted core-language RFC](docs/rfcs/0001-core-language.md)
