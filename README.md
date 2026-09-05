# Joggle

Joggle is a small C++ compiler substrate for AI software/hardware co-design.
It provides one versioned `Mod` abstraction, one typed `fn` declaration,
explicit Known/Residual staging, editable CFG/SSA bodies, and installable
compiler-time services.

The project deliberately does not prescribe a graph hierarchy, lowering
ladder, target base class, pass registry, kernel object, device model, or
deployment container. AI vocabularies and hardware experiments are installable
Mods rather than compiler-core categories.

## Source model

```joggle
joggle 1;

mod example@1.0.0 {
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

A `Mod` owns declarations, materialized Fn bodies, imports, and
content-addressed immutable data. There is no second Program, Graph, Package,
or Artifact owner. Source-only Mods remain one text file; a Mod with
owned weights uses a lossless directory bundle containing `mod.joggle` and
`data/<sha256>`, accepted directly by `check`, `run`, `install`, and
`lock`.

## Compiler extension model

An extension consists of one `.joggle` Mod and, only when required, one
native library implementing bodyless compiler fns. The source Mod is
the schema authority; no generated declaration header is required.

Whole-mod import, conversion, optimization, analysis, simulation, and file
output remain ordinary fns:

```joggle
fn read(input: bytes) -> mod;
fn optimize(input: mod, policy: type) -> mod;
fn inspect(input: mod) -> bytes;

fn prepare(input: bytes, policy: type) -> mod {
  model = @read(input);
  return @optimize(model, policy);
}
```

The user controls composition in source. Joggle does not discover magic pass
names or force a universal sequence of intermediate forms.

`Compiler::resolve` recursively materializes concrete source-defined calls and
leaves bodyless implementation boundaries explicit. The installable
`transform.resolve` fn exposes the same operation through normal `@` staging.
Resolution constructs a call graph; it does not execute Residual Ops through
host callbacks.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The repository ships `arith@1.1.0` and `tensor@3.0.0`. Tensor contains
coordinate construction/projection, explicit-domain `build`, `at`, ordered
`fold`, overloaded indexing, and bodyful `map`; it does not contain an ONNX
operator catalog. `transform@2.1.0` performs recursive single-block
inlining, concrete typed-lambda equation passes, and source resolution.
`quant@2.0.0` and the optional
Protobuf-backed `onnx@4.0.0` import path remain incomplete domain libraries.
Rank-two ONNX MatMul now has a real nested `build/fold/index/arithmetic` body.
Concrete `map(build(S, f), g)` composition and `at(build(S, f), p)` cancellation
already use the same declaration-identity/dataflow matcher rather than a
name-specific transform.
The ONNX fixtures test import and reconstruction fidelity. They are not yet
evidence for generic bodyful fusion or an executable kernel pipeline.

## Documentation

- [Documentation index](docs/README.md)
- [Getting started](docs/getting-started.md)
- [Language](docs/language.md)
- [Architecture](docs/architecture.md)
- [Mods](docs/mods.md)
