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
// append, insert, locate, replace, or erase calls in the isolated edit
if (!edit.commit(diagnostics)) {
  diagnostics.print(std::cerr);
}
```

Function edits are transactional. A commit verifies ownership, CFG, dominance,
call signatures, and result types before publishing. `Module::insert` installs
a materialized function under a new declaration; `Module::body` returns a
mutable copy-on-write body for an exact declaration.

`edit.locate(op, source_range)` attaches frontend provenance to a call, and
`op.location()` reads it. Locations improve diagnostics and survive cloning or
typed replacement, but they are not semantic properties and do not change
canonical Module identity.

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

## Replace typed expressions

```cpp
auto changed =
    joggle::replace(compiler, function, before, after, diagnostics);
```

`before` and `after` are ordinary single-expression `Function` values. Their
arguments are typed holes; repeated arguments require the same SSA value.
Matching uses exact call declarations, canonical Known values, and function
reference identity. All maximal non-overlapping matches are replaced in one
transaction. The `Module&` overload applies the same operation on a private
module snapshot and publishes only after every materialized member succeeds.
Zero changes are a successful no-op and preserve revision or module identity.

The `Compiler&` overload first proves conservative definitional equivalence.
It recursively expands eligible pure source bodies with a fixed bound, keeps
bodyless calls as exact-identity leaves, and rejects recursion or a normalized
mismatch before opening an edit. The overload without `Compiler&` is the
low-level structural primitive: it preserves IR invariants but does not claim
the two expressions compute the same value.

Representation modules may call the `equivalent` overload taking a
`TypeProjection`. The callback maps each physical Type to its logical Type and
must be total and idempotent; otherwise equivalence fails. Only Type
observation is projected. Source bodies must still reduce changed calls to the
same exact declarations, so the callback cannot bless opaque physical
instructions. See [RFC 0008](rfcs/0008-logical-representation.md).

## Outline reference-bodied calls

```cpp
auto changed = joggle::outline(
    compiler, function, reference,
    [](const joggle::Function& body, const joggle::Op& root)
        -> std::optional<std::vector<joggle::Value>> {
      // Return the reference function's concrete arguments, or skip root.
    },
    diagnostics);
```

`reference` is one ordinary source-bodied `FunctionDecl`. The selector owns
only extension-specific applicability and argument mapping. For each selected
root, `outline` constructs the concrete reference call, instantiates its source
body, locks every typed hole to the selector-provided SSA value, proves
definitional equivalence, and performs the existing shared-DAG-safe atomic
replacement. A wrong root or argument mapping fails without publishing any
earlier sweep. The `Module&` overload applies the same contract to every
materialized member on a private Module snapshot.

This is reverse inlining, not a pattern hierarchy. It centralizes the
correctness and transaction machinery that reference-bodied extensions would
otherwise duplicate while leaving their legality policy in the owning Module.

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
