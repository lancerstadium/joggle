# C++ behavior bindings

The `.joggle` Module is the schema authority. C++ attaches behavior to exact,
content-addressed declarations; it never recreates the schema in classes or a
generated header.

## Verifiers

```cpp
compiler.bind(integer,
  [](const joggle::Type& type, joggle::Diagnostics& diagnostics) {
    const auto width = type.get<std::int64_t>("width");
    if (!width || *width <= 0) {
      diagnostics.report("integer width must be positive");
      return false;
    }
    return true;
  });
```

A callable beginning with `Type`, `Attribute`, or `Instruction` binds a
verifier for that declaration. Structural arity, ownership, type, CFG, SSA,
and dominance verification always runs; bindings add domain-specific checks.

A callback result is accepted only when the callback reports no diagnostic.
Diagnostics emitted while checking an Instruction inherit its source location.

## Interface behavior

Attribute and Instruction interfaces may declare callable methods. Bind one by
its resolved method declaration or by an unambiguous name:

```cpp
compiler.bind(instruction, "latency",
  [](const joggle::Instruction& value, std::int64_t lanes) {
    return estimate_latency(value, lanes);
  });
```

The adapter infers ordinary C++ argument and result types and checks them
against the Module signature. A final `Diagnostics&` is optional. Generic or
overloaded C++ callables can use the explicit
`bind<Result, Arguments...>` overload.

Type-interface fields such as `storage_bits` are declarative Module content.
They are queried with `Type::get<T>` and can participate in dependent types;
they are not replaced by callbacks.

## Compiler functions

The current bootstrap representation maps Prelude `function` to
`joggle::Function`:

```joggle
fn canonicalize(input: function) -> function;
fn count(input: function) -> int;
```

```cpp
compiler.bind(canonicalize,
  [](joggle::Function& function, joggle::Diagnostics& diagnostics) {
    auto edit = function.edit();
    // Transform Blocks, Instructions, and Values.
    return edit.commit(diagnostics);
  });

compiler.bind(count,
  [](const joggle::Function& function) -> std::int64_t {
    return static_cast<std::int64_t>(function.instructions().size());
  });
```

Invocation uses the declaration or its qualified name:

```cpp
compiler.run(function, canonicalize);
auto nodes = compiler.run<std::int64_t>(count, function);
```

`Compiler::run` verifies inputs, checkpoints mutable Function arguments,
invokes the callable, and verifies the result. A false result, diagnostic,
exception, or failed verification restores the Function checkpoint.

This bootstrap path is implemented today. General Module-declared host
representations are the next extension boundary: they will let Modules map
types such as `device.target`, `cost.report`, or `ir.module` to ordinary C++
types without adding another callable namespace.

## Known evaluation

A bodyless compiler-domain function can use the same typed `bind` form:

```joggle
fn choose_width(elements: int) -> int;
```

```cpp
compiler.bind(choose_width,
  [](std::int64_t elements) { return elements < 128 ? 8 : 16; });
```

With Known arguments the binding produces a Known value. A function returning
program values residualizes as an Instruction when needed. `@(...)` merely
requires the ordinary evaluation to succeed as Known.

Host callbacks are not speculatively executed below Residual control. Known
evaluation also obeys configured expression-step and nesting-depth limits.

## Behavior libraries

A Module may keep bindings in its host application or package them in a shared
library:

```cpp
namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto integer = module.type("integer");
  if (!integer) {
    diagnostics.report("behavior does not match its Module");
    return false;
  }
  compiler.bind(*integer, [](const joggle::Type& value) {
    const auto width = value.get<std::int64_t>("width");
    return width && *width > 0;
  });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
```

Build it without a generated public header:

```cmake
joggle_add_behavior(arith_behavior
  MODULE arith.joggle
  SOURCES arith_behavior.cpp
)
```

Then load it after linking:

```cpp
compiler.load_behavior("arith", library_path);
// Or discover the locked, installed behavior for this host:
compiler.load_behavior("arith");
```

The loader checks ABI version, target, exact Module identity, entry points, and
when applicable the locked binary digest. A failed load rolls back every
binding made by that library. Loading executable behavior remains explicit
because it is a trust decision.
