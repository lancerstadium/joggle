# Modules and extensions

A Joggle extension is a versioned `Module`. The module is simultaneously the
namespace, dependency unit, schema, and container for materialized functions.
No companion manifest language or generated C++ header is required.

## Declaration surface

```joggle
joggle 1;

module quant@1.0.0 {
  import tensor@1 as t;

  type format(bits: int, signed: bool = true) {
    storage_bits: int = bits;
  }

  fn quantize<F>(input: t.tensor, scheme: F) -> t.tensor;
  fn optimize(input: module, scheme: type) -> module;
}
```

Only three member forms exist:

- `import` names a versioned dependency and optional prefix;
- `type` defines an immutable parameterized compile-time value;
- `fn` defines or declares callable behavior.

Metadata, formats, policies, machine descriptions, and estimates are ordinary
types. Import, conversion, analysis, optimization, simulation, and emission
are ordinary functions. This keeps extensions composable without forcing
authors to implement framework-specific base classes.

## Source authority and native implementation

A bodyless `fn` may be implemented by C++:

```cpp
void joggle_module(joggle::Compiler& compiler,
                   const joggle::Module& module,
                   joggle::Diagnostics& diagnostics) {
  compiler.bind(module, "read",
                [](const joggle::Bytes& input)
                    -> std::optional<joggle::Module> {
                  return decode(input);
                });
}
```

`Compiler::bind` selects a source overload using the C++ callable signature
and rejects mismatches. C++ provides execution, not a parallel declaration
system. Native libraries carry the module identity produced by the CMake
helper and are rejected when their declaration digest does not match.

## User-defined pipelines

Users compose functions in source instead of registering a fixed pass list:

```joggle
fn prepare(input: bytes, policy: type) -> module {
  model = @onnx.read(input);
  folded = @fold_constants(model);
  return @quant.optimize(folded, policy);
}
```

The names and order are ordinary application code. A module can expose a
convenient pipeline while still allowing expert users to call each component.

## Portable and host-only functions

A function with a source body can be materialized into IR. A bodyless function
needs a native binding when invoked at compile time, or remains a residual call
when used as program computation. `@` is the explicit source-level stage
switch; known inputs alone never select host execution during materialization.

## Versioning and dependencies

Imports use exact, major, minor, or caret ranges. Linking chooses one version
per module name, verifies the complete closure, and records exact dependencies
in a materialized module. Repositories and lock files are described in
[Module repository](module-repository.md).

## Design rules for new modules

- Define only domain vocabulary that has an executable use case.
- Prefer a small orthogonal type/function surface over workflow-specific nouns.
- Keep file formats and hardware descriptions outside compiler core.
- Use normal overloads for extensible behavior.
- Return a `Module`, `Function`, `Type`, `bytes`, or another declared type;
  never invent a second ownership container.
- Add real-model tests before claiming a frontend or optimization module is
  supported.
