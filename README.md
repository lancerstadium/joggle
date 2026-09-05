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

The repository ships `arith@1.2.0`, `tensor@7.0.0`, `mem@1.0.0`,
`nn@2.0.0`, `transform@3.0.0`, and `quant@3.0.0`. Tensor exposes indexed
construction, rank-polymorphic mapping, reduction, overloaded `[]`, and
immutable constants. `mem` supplies target-independent read views and ordered
write destinations; its explicit `realize(fn)` pass refines the tensor basis
to loops and ordered stores without matching NN operator names. `nn` owns
neural-network functions composed from the pure tensor basis. A call is a
compact graph node until an explicit pass expands or refines it.

The optional Protobuf-backed `onnx@5.0.0` package is only a file reader. It
resolves decoded records directly to linked `nn`, `tensor`, and `quant` fns.
There is no ONNX operation module, ONNX IR, conversion pass, or automatic
`to_nn` stage.

## Documentation

- [Documentation index](docs/README.md)
- [Getting started](docs/getting-started.md)
- [Language](docs/language.md)
- [Architecture](docs/architecture.md)
- [Mods](docs/mods.md)
