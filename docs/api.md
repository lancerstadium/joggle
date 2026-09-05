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
// call, call_before, locate, replace, or erase in the isolated edit
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
transactional edits, but they are not semantic properties and do not change
canonical Mod identity.

Fn values remain ordinary typed `Val`s. A specialized declared reference is
built with `edit.reference(declaration, callable_type, bindings)` and inspected
with `value.referenced_fn()` and `value.bindings()`. The bindings are Known
compile-time parameters; they are not Call arguments. An anonymous, already
verified body is built with `edit.callable(fn, callable_type, captures)` and
inspected with `value.inline_fn()` and `value.captures()`. Capture-free callers
may omit the third argument. Captures are explicit parent-Fn edges and
correspond, in order, to trailing hidden arguments of the nested body; the
callable type describes only its visible source parameters and results. Effect
values cannot be hidden in captures. No public lambda, closure, or region
object is involved.
The current source form has one expression and one result. Consequently,
canonical formatting or `Mod::insert` rejects a C++-constructed inline body
that cannot be represented by that source form instead of emitting hidden
declarations.

## Inline source calls

```cpp
joggle::Diag diagnostics;
auto changed = joggle::inline_calls(compiler, fn, diagnostics);
```

One invocation considers the Calls present in the input snapshot, including
those inside callable bodies already present in that snapshot. It expands
source-defined and anonymous single-block callees, remaps arguments, results,
fn bindings, locations, nested callable values, and closure captures. Opaque,
dynamic, and multi-block calls remain unchanged. Newly cloned calls are
considered by a later invocation, so the operation is deterministic and
bounded. A failing Fn edit is not published.

## Typed equation passes

```cpp
joggle::Diag diagnostics;
auto changed = joggle::apply_pass(compiler, fn, laws, diagnostics);
```

`laws` is an ordinary `Mod`. Each bodyful fn with two identical result Type
expressions states one oriented equation: its first returned expression is the
pattern and its second is the replacement. Generic parameters are specialized
from the candidate result Type and the Types of structurally corresponding
arguments in the source left expression. Straight-line local definitions are
followed through ordinary names, including a selected result of a multi-result
call. The materialized equation then matches declaration identity, compiler
bindings, exact Types, and dataflow; the source probe cannot accept a match by
itself. The replacement is cloned transactionally and dead pure calls are
removed. The same pass recursively transforms nested callable bodies and
rejects effectful or control-flow equations. The source wrapper is
`@transform.pass(input, laws)`.

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

Bindings represent compiler-time values and host services. Fixed-width
Residual types such as `i8`, `i32`, and `f32` are deliberately not mapped to
C++ execution values: ordinary program calls must be compiled as a whole
rather than interpreted through one binding per Op.

`HostEval::Hermetic` is an explicit promise for deterministic,
side-effect-free host evaluation under residual control. Use the default
guarded mode otherwise.

## Invoke compiler fns

```cpp
auto optimized = compiler.run<joggle::Mod>("passes.optimize", model);
bool emitted = compiler.run<void>("driver.write", model, output_path);
```

`invocable<Result, Args...>` checks a reflected signature without running it.
`run` accepts a qualified name or exact declaration. It is the C++ entry to an
explicitly staged compiler computation, not a runtime for Residual `Fn` IR.

## Resolve source calls

```cpp
joggle::Diag diagnostics;
auto resolved = compiler.resolve(model, diagnostics);
```

Resolution instantiates reachable source-defined callees at their concrete
types and compiler parameters. Bodyless calls remain explicit leaves for a
later whole-Mod emitter. It is also available as the normal staged fn
`transform.resolve(mod) -> mod`.

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
