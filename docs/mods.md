# Mods

A Joggle `Mod` is a versioned package. It is simultaneously a namespace,
dependency and installation unit, declaration source, and container for
materialized fns. It is not an IR level, pass, rewrite pattern, kernel
kind, or optimization profile. No companion manifest language or generated C++
header is required.

## Admission rule

A new Mod is justified only when its contents need an independently named,
versioned, distributable dependency. A single transformation, operator
combination, model profile, or experiment belongs as a `fn` in an existing
owner or application package. Moving one expression behind another fn
name is not grounds for a package.

The shipped boundary is deliberately small:

- `tensor` owns target-independent tensor types and program fns;
- `mem` owns target-independent readable views and ordered destinations;
- `nn` owns frontend-independent neural-network algorithms;
- `quant` owns affine Q/DQ semantics;
- `transform` owns reusable explicitly staged Fn transformations; and
- optional `onnx` owns one external-format reader.

These are package roles, not source-language categories. Every declaration is
still a `type` or `fn`, and every program or compiler action is still a normal
call distinguished only by explicit `@` staging.

The user-facing problems map to the language directly:

| Need | Representation | Package rule |
| --- | --- | --- |
| import or export a model format | `fn(bytes) -> mod` or `fn(mod) -> bytes` | owned by the external-format package |
| express a model or kernel | an ordinary typed `fn` body | owned by the vocabulary that defines its calls |
| optimize or convert IR | `fn(fn) -> fn` or `fn(mod) -> mod`, called with `@` | a fn in the destination domain owner; generic transactions live in `transform` |
| define a data format or policy | a parameterized `type` plus ordinary conversion fns | a new package only when independently reusable and distributable |
| emit or simulate an implementation | an explicitly staged fn returning declared data | a future implementation package, never a core `Target` hierarchy |

This is the intended extensibility point for AI hardware/software co-design:
new representations and implementation choices are declared as types and
fns, while the core continues to own only linking, staging, verified
Fn edits, and compiler-time evaluation. A device, layout, schedule, or cost model does
not receive a privileged base class merely because one experiment needs it.

This boundary is intentionally narrower than established multi-level stacks.
[MLIR](https://mlir.llvm.org/docs/DialectConversion/) uses dialects for durable
IR semantics and passes for conversion. [TVM](https://tvm.apache.org/docs/arch/index.html)
keeps graph-level Relax Fns and executable TensorIR PrimFuncs in one
IRMod; its fusion pipeline ultimately creates a low-level fn.
[IREE](https://iree.dev/reference/mlir-dialects/Stream/) introduces Flow,
Stream, and HAL only when partitioning, asynchronous scheduling, and resource
management become explicit program semantics. Joggle does not reproduce those
layers as Mods. It keeps one Fn model and admits new packages only for
independently distributed vocabulary or tools.

## Declaration surface

```joggle
joggle 1;

mod project@1.0.0 {
  import onnx@5;

  fn canonicalize(input: mod) -> mod;

  fn compile(input: bytes, name: string) -> mod {
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
simulation, and output are ordinary fns. This keeps extensions
composable without forcing authors to implement framework-specific base
classes or create a Mod per action.

## Source authority and native implementation

A bodyless `fn` may be implemented by C++:

```cpp
void joggle_mod(joggle::Compiler& compiler,
                   const joggle::Mod& mod,
                   joggle::Diag& diagnostics) {
  compiler.bind(mod, "read",
                [](const joggle::Bytes& input)
                    -> std::optional<joggle::Mod> {
                  return decode(input);
                });
}
```

`Compiler::bind` selects a source overload using the C++ callable signature
and rejects mismatches. C++ implements an explicitly staged host service, not
the semantics of each Residual Op and not a parallel declaration system.
Native libraries carry the mod identity produced by the CMake
helper and are rejected when their declaration digest does not match.

## User-defined pipelines

Users compose fns in source instead of registering a fixed pass list:

```joggle
fn prepare(input: bytes, name: string) -> mod {
  model = @onnx.read(input, name);
  folded = @fold_constants(model);
  return @canonicalize(folded);
}
```

The names and order are ordinary application code. A mod can expose a
convenient pipeline while still allowing expert users to call each component.

## Portable and host-only fns

A fn with a source body can be materialized into IR. A bodyless fn
needs a native binding when invoked at compile time, or remains a residual call
when used as program computation. `@` is the explicit source-level stage
switch; known inputs alone never select host execution during materialization.
Program fns are never bound merely so the compiler can walk their calls.

## Versioning and dependencies

Imports use exact, major, minor, or caret ranges. Linking chooses one version
per mod name, verifies the complete closure, and records exact dependencies
in a materialized mod. Repositories and lock files are described in
[Repository](repository.md).

## Design rules for new mods

- Require independent naming, versioning, installation, and at least one real
  consumer; otherwise add a fn to its owning package.
- Never create a Mod for one pass, pattern, operator combination, layout
  profile, benchmark, or paper example.
- Define only domain vocabulary that has an executable use case.
- Prefer a small orthogonal type/fn surface over workflow-specific nouns.
- Keep file formats and hardware descriptions outside compiler core.
- Use normal overloads for extensible behavior.
- Return a `Mod`, `Fn`, `Type`, `bytes`, or another declared type;
  never invent a second ownership container.
- Add real-model tests before claiming a frontend or optimization mod is
  supported.
