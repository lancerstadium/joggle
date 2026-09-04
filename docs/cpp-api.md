# C++ API

Include the umbrella header:

```cpp
#include <joggle/joggle.h>
```

The API follows the language's ownership model. `Module` owns declarations and
materialized bodies, `Type` is an immutable declared value, and `Compiler`
links and executes modules.

## Parse and inspect a module

```cpp
joggle::Diagnostics diagnostics;
auto module = joggle::parse_module(source, diagnostics, "model.joggle");
if (!module) {
  diagnostics.print(std::cerr);
  return;
}

auto tensor = module->type("tensor");
auto calls = module->overloads("conv2d");
std::string canonical = joggle::format(*module);
```

Declaration handles are immutable views backed by their module snapshot.
`Module::Symbol` supplies module name, version, kind, local name, qualified
name, stable name, and declaration provenance.

`digest()` identifies the whole snapshot. `declaration_digest()` identifies
imports and declarations with bodies erased.

## Link modules

```cpp
joggle::Compiler compiler;
compiler.search(repository_root);
compiler.load("driver.joggle");
if (!compiler.link()) {
  compiler.diagnostics().print(std::cerr);
  return;
}
```

`add(text)`, `add(module)`, and `load(path)` add roots. `search(path)` adds a
repository root. `lock(path)` requires exact locked dependencies. `link()`
resolves one coherent closure and freezes it for execution.

## Construct types

```cpp
auto module = compiler.module("tensor");
auto schema = module ? module->type("integer") : std::nullopt;
auto i8 = schema ? compiler.make(*schema, 8, true)
                 : std::optional<joggle::Type>{};

if (i8) {
  auto width = i8->get<std::int64_t>("width");
  auto bits = i8->get<std::int64_t>("storage_bits");
}
```

Parameters accept C++ integers, floating-point values, booleans, strings,
`Type`, and supported ranges of those values. Construction checks domains,
defaults, computed fields, module ownership, and registered type verifiers.

Compile-time metadata uses the same API: define a `type layout(...)`, obtain
its `TypeDecl`, and call `make`.

## Materialize and edit functions

```cpp
auto function = compiler.materialize("model.main");
if (!function) {
  return;
}

auto edit = function->edit();
// append, insert, replace, or erase calls in the isolated edit
if (!edit.commit(diagnostics)) {
  diagnostics.print(std::cerr);
}
```

Function edits are transactional. A commit verifies ownership, CFG, dominance,
call signatures, and result types before publishing. `Module::insert` installs
a materialized function under a new declaration; `Module::body` returns a
mutable copy-on-write body for an exact declaration.

Function values remain ordinary typed `Value`s. A declared reference is built
with `edit.reference(declaration, callable_type)` and inspected with
`value.referenced_function()`. An anonymous, already verified body is built
with `edit.callable(function, callable_type)` and inspected with
`value.inline_function()`. The callable type must exactly match the body's
inputs and results; no public lambda or region object is involved.
The current source form has one expression and one result. Consequently,
canonical formatting or `Module::insert` rejects a C++-constructed inline body
that cannot be represented by that source form instead of emitting hidden
declarations.

## Bind native functions

```cpp
compiler.bind(module, "parse",
              [](const joggle::Bytes& bytes)
                  -> std::optional<joggle::Module> {
                return parse_external_format(bytes);
              });
```

The callable's C++ signature selects and validates the source overload.
Optional `Compiler&` and `Diagnostics&` service parameters are not source
ports. Bindings can return `void`, one supported value, a tuple for multiple
results, or `std::optional<T>` to report failure.

`HostEvaluation::Hermetic` is an explicit promise for deterministic,
side-effect-free host evaluation under residual control. Use the default
guarded mode otherwise.

## Invoke compiler functions

```cpp
auto optimized = compiler.run<joggle::Module>("passes.optimize", model);
bool emitted = compiler.run<void>("driver.write", model, output_path);
```

`invocable<Result, Args...>` checks a reflected signature without running it.
`run` accepts a qualified name or exact declaration. Calls, source bodies, and
native implementations use the same overload resolution.

## Verifiers and host representations

```cpp
compiler.verify(*schema, [](const joggle::Type& type,
                            joggle::Diagnostics& diagnostics) {
  const auto width = type.get<std::int64_t>("width");
  if (!width || *width <= 0) {
    diagnostics.report("width must be positive");
    return false;
  }
  return true;
});
```

Type and function verifiers strengthen declared invariants. `represent<T>`
associates a C++ host type with a Joggle type declaration for native bindings;
an optional projection maps a host value to its concrete Joggle `Type`.

## Native module entry point

```cpp
void joggle_module(joggle::Compiler& compiler,
                   const joggle::Module& module,
                   joggle::Diagnostics& diagnostics) {
  // bind functions and verifiers
}
```

Use the `joggle_module(...)` CMake helper to build the library. It embeds the
canonical module identity in a hidden generated translation unit; it does not
generate declaration wrappers. `Compiler::load_native` rejects a library
built for a different module declaration snapshot.

See [Getting started](getting-started.md) for a complete extension and
[IR model](ir.md) for editing semantics.
