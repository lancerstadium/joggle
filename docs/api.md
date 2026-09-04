# C++ API

Include the umbrella header:

```cpp
#include <joggle/joggle.h>
```

The API follows the language's ownership model. `Mod` owns declarations and
materialized bodies, `Type` is an immutable declared value, and `Compiler`
links and executes mods.

## Parse and inspect a mod

```cpp
joggle::Diag diag;
auto mod = joggle::parse_mod(source, diag, "model.joggle");
if (!mod) {
  diag.print(std::cerr);
  return;
}

auto tensor = mod->type("tensor");
auto calls = mod->overloads("conv2d");
std::string canonical = joggle::format(*mod);
```

Declaration handles are immutable views backed by their mod snapshot.
`Mod::Symbol` supplies mod name, version, kind, local name, qualified
name, stable name, and declaration provenance.

`digest()` identifies the whole snapshot. `declaration_digest()` identifies
imports and declarations with bodies erased.

## Link mods

```cpp
joggle::Compiler compiler;
compiler.search(repository_root);
compiler.load("driver.joggle");
if (!compiler.link()) {
  compiler.diag().print(std::cerr);
  return;
}
```

`add(text)`, `add(mod)`, and `load(path)` add roots. `search(path)` adds a
repository root. `lock(path)` requires exact locked dependencies. `link()`
resolves one coherent closure and freezes it for execution.

## Construct types

```cpp
auto mod = compiler.mod("tensor");
auto schema = mod ? mod->type("integer") : std::nullopt;
auto i8 = schema ? compiler.make(*schema, 8, true)
                 : std::optional<joggle::Type>{};

if (i8) {
  auto width = i8->get<std::int64_t>("width");
  auto bits = i8->get<std::int64_t>("storage_bits");
}
```

Parameters accept C++ integers, floating-point values, booleans, strings,
`Type`, and supported ranges of those values. Construction checks domains,
defaults, computed fields, mod ownership, and registered type verifiers.

Compile-time metadata uses the same API: define a `type layout(...)`, obtain
its `TypeDecl`, and call `make`.

## Materialize and edit fns

```cpp
auto fn = compiler.materialize("model.main");
if (!fn) {
  return;
}

auto edit = fn->edit();
// append, insert, locate, replace, or erase calls in the isolated edit
if (!edit.commit(diagnostics)) {
  diagnostics.print(std::cerr);
}
```

Fn edits are transactional. A commit verifies ownership, CFG, dominance,
call signatures, and result types before publishing. `Mod::insert` installs
a materialized fn under a new declaration; `Mod::body` returns a
mutable copy-on-write body for an exact declaration.

`edit.locate(op, source_range)` attaches frontend provenance to a call, and
`op.location()` reads it. Locations improve diagnostics and survive cloning or
typed replacement, but they are not semantic properties and do not change
canonical Mod identity.

Fn values remain ordinary typed `Val`s. A declared reference is built
with `edit.reference(declaration, callable_type)` and inspected with
`value.referenced_fn()`. An anonymous, already verified body is built
with `edit.callable(fn, callable_type)` and inspected with
`value.inline_fn()`. The callable type must exactly match the body's
inputs and results; no public lambda or region object is involved.
The current source form has one expression and one result. Consequently,
canonical formatting or `Mod::insert` rejects a C++-constructed inline body
that cannot be represented by that source form instead of emitting hidden
declarations.

## Replace typed expressions

```cpp
auto changed =
    joggle::replace(compiler, fn, before, after, diagnostics);
```

`before` and `after` are ordinary single-expression `Fn` values. Their
arguments are typed holes; repeated arguments require the same SSA value.
Matching uses exact call declarations, canonical Known values, and fn
reference identity. All maximal non-overlapping matches are replaced in one
transaction. The `Mod&` overload applies the same operation on a private
mod snapshot and publishes only after every materialized member succeeds.
Zero changes are a successful no-op and preserve revision or mod identity.

The `Compiler&` overload first proves conservative definitional equivalence.
It recursively expands eligible pure source bodies with a fixed bound, keeps
bodyless calls as exact-identity leaves, and rejects recursion or a normalized
mismatch before opening an edit. The overload without `Compiler&` is the
low-level structural primitive: it preserves IR invariants but does not claim
the two expressions compute the same value.

## Bind native fns

```cpp
compiler.bind(mod, "parse",
              [](const joggle::Bytes& bytes)
                  -> std::optional<joggle::Mod> {
                return parse_external_format(bytes);
              });
```

The callable's C++ signature selects and validates the source overload.
Optional `Compiler&` and `Diag&` service parameters are not source
ports. Bindings can return `void`, one supported value, a tuple for multiple
results, or `std::optional<T>` to report failure.

`HostEvaluation::Hermetic` is an explicit promise for deterministic,
side-effect-free host evaluation under residual control. Use the default
guarded mode otherwise.

## Invoke compiler fns

```cpp
auto optimized = compiler.run<joggle::Mod>("passes.optimize", model);
bool emitted = compiler.run<void>("driver.write", model, output_path);
```

`invocable<Result, Args...>` checks a reflected signature without running it.
`run` accepts a qualified name or exact declaration. Calls, source bodies, and
native implementations use the same overload resolution.

## Verifiers and host representations

```cpp
compiler.verify(*schema, [](const joggle::Type& type,
                            joggle::Diag& diagnostics) {
  const auto width = type.get<std::int64_t>("width");
  if (!width || *width <= 0) {
    diagnostics.report("width must be positive");
    return false;
  }
  return true;
});
```

Type and fn verifiers strengthen declared invariants. `represent<T>`
associates a C++ host type with a Joggle type declaration for native bindings;
an optional projection maps a host value to its concrete Joggle `Type`.

## Native mod entry point

```cpp
void joggle_mod(joggle::Compiler& compiler,
                   const joggle::Mod& mod,
                   joggle::Diag& diagnostics) {
  // bind fns and verifiers
}
```

Use the `joggle_mod(...)` CMake helper to build the library. It embeds the
canonical mod identity in a hidden generated translation unit; it does not
generate declaration wrappers. `Compiler::load_native` rejects a library
built for a different mod declaration snapshot.

See [Getting started](getting-started.md) for a complete extension and
[IR model](ir.md) for editing semantics.
