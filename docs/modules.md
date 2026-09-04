# Modules

A Joggle `Module` is a versioned package. It is simultaneously a namespace,
dependency and installation unit, declaration source, and container for
materialized functions. It is not an IR level, pass, rewrite pattern, kernel
kind, or optimization profile. No companion manifest language or generated C++
header is required.

## Admission rule

A new Module is justified only when its contents need an independently named,
versioned, distributable dependency. A single transformation, operator
combination, model profile, or experiment belongs as a `fn` in an existing
owner or application package. Moving one expression behind another function
name is not grounds for a package.

The shipped boundary is deliberately small:

- `tensor` owns target-independent tensor types and program functions;
- `quant` owns affine Q/DQ semantics and its executable numerical oracle;
- `transform` owns reusable explicitly staged Function transformations; and
- optional `onnx` owns one external-format adapter.

These are package roles, not source-language categories. Every declaration is
still a `type` or `fn`, and every program or compiler action is still a normal
call distinguished only by explicit `@` staging.

The user-facing problems map to the language directly:

| Need | Representation | Package rule |
| --- | --- | --- |
| import or export a model format | `fn(bytes) -> module` or `fn(module) -> bytes` | owned by the external-format package |
| express a model or kernel | an ordinary typed `fn` body | owned by the vocabulary that defines its calls |
| optimize or convert IR | `fn(function) -> function` or `fn(module) -> module`, called with `@` | a function in the domain owner; generic transactions live in `transform` |
| define a data format or policy | a parameterized `type` plus ordinary conversion functions | a new package only when independently reusable and distributable |
| emit or simulate an implementation | an explicitly staged function returning declared data | a future implementation package, never a core `Target` hierarchy |

This is the intended extensibility point for AI hardware/software co-design:
new representations and implementation choices are declared as types and
functions, while the core continues to own only linking, staging, verified
Function edits, and execution. A device, layout, schedule, or cost model does
not receive a privileged base class merely because one experiment needs it.

This boundary is intentionally narrower than established multi-level stacks.
[MLIR](https://mlir.llvm.org/docs/DialectConversion/) uses dialects for durable
IR semantics and passes for conversion. [TVM](https://tvm.apache.org/docs/arch/index.html)
keeps graph-level Relax Functions and executable TensorIR PrimFuncs in one
IRModule; its fusion pipeline ultimately creates a low-level function.
[IREE](https://iree.dev/reference/mlir-dialects/Stream/) introduces Flow,
Stream, and HAL only when partitioning, asynchronous scheduling, and resource
management become explicit program semantics. Joggle does not reproduce those
layers as Modules. It keeps one Function model and admits new packages only for
independently distributed vocabulary or tools.

## Declaration surface

```joggle
joggle 1;

module project@1.0.0 {
  import onnx@1;

  fn canonicalize(input: module) -> module;

  fn compile(input: bytes, name: string) -> module {
    model = @onnx.read(input, name);
    return @canonicalize(model);
  }
}
```

Only three member forms exist:

- `import` names a versioned dependency and optional prefix;
- `type` defines an immutable parameterized compile-time value;
- `fn` defines or declares callable behavior.

Metadata and policies are ordinary types. Import, analysis, transformation,
simulation, and output are ordinary functions. This keeps extensions
composable without forcing authors to implement framework-specific base
classes or create a Module per action.

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
fn prepare(input: bytes, name: string) -> module {
  model = @onnx.read(input, name);
  folded = @fold_constants(model);
  return @canonicalize(folded);
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
[Repository](repository.md).

## Design rules for new modules

- Require independent naming, versioning, installation, and at least one real
  consumer; otherwise add a function to its owning package.
- Never create a Module for one pass, pattern, operator combination, layout
  profile, benchmark, or paper example.
- Define only domain vocabulary that has an executable use case.
- Prefer a small orthogonal type/function surface over workflow-specific nouns.
- Keep file formats and hardware descriptions outside compiler core.
- Use normal overloads for extensible behavior.
- Return a `Module`, `Function`, `Type`, `bytes`, or another declared type;
  never invent a second ownership container.
- Add real-model tests before claiming a frontend or optimization module is
  supported.
