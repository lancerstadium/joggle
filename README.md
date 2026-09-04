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
or Artifact owner. Source-only Modules remain one text file; a Module with
owned weights uses a lossless directory bundle containing `module.joggle` and
`data/<sha256>`, accepted directly by `check`, `run`, `install`, and
`lock`.

## Compiler extension model

An extension consists of one `.joggle` Module and, only when required, one
native library implementing bodyless compiler functions. The source Module is
the schema authority; no generated declaration header is required.

Whole-module import, conversion, optimization, analysis, simulation, and file
output remain ordinary functions:

```joggle
fn read(input: bytes) -> module;
fn optimize(input: module, policy: type) -> module;
fn inspect(input: module) -> bytes;

fn prepare(input: bytes, policy: type) -> module {
  model = @read(input);
  return @optimize(model, policy);
}
```

The user controls composition in source. Joggle does not discover magic pass
names or force a universal sequence of intermediate forms.

`Compiler::specialize` recursively materializes source-defined calls until a
caller-supplied predicate accepts every remaining Function. It is one reusable
transformation primitive, not a prescribed target boundary or lowering ladder.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The repository ships `tensor@1.0.0`, a small target-independent semantic
Module; `transform@1.0.0`, the typed-lambda semantic replacement surface;
`quant@1.1.0`, an affine QDQ boundary with a bit-exact reference
oracle; and an optional, Protobuf-backed
`onnx@1.0.0` inference importer. The real-model paths import hash-pinned FLOAT
and QDQ SqueezeNet artifacts into ordinary typed Functions. Both have exact
ONNX Runtime differential evidence.

## Documentation

- [Architecture](docs/architecture.md)
- [Language reference](docs/language.md)
- [IR model](docs/ir.md)
- [Compiler functions](docs/compiler-functions.md)
- [Transform module](docs/transform.md)
- [C++ API](docs/cpp-api.md)
- [Module design](docs/modules.md)
- [Tensor module](docs/tensor.md)
- [Quant module](docs/quant.md)
- [ONNX inference import](docs/onnx.md)
- [Repository and reproducibility](docs/module-repository.md)
- [Research position](docs/research-position.md)
- [Accepted core-language RFC](docs/rfcs/0001-core-language.md)
- [Reference-bodied transformation RFC](docs/rfcs/0007-reference-bodied-transformations.md)
- [QDQ import RFC](docs/rfcs/0009-qdq-import.md)
