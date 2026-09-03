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

Module-declared host representations map parameterless types such as
`device.target` or `cost.report` to ordinary C++ value types:

```joggle
type target();
type estimate();
fn measure(input: target) -> estimate;
```

```cpp
compiler.represent<Target>(target_type);
compiler.represent<Estimate>(estimate_type);
compiler.bind(measure, [](const Target& target) {
  return Estimate{estimate_cycles(target)};
});

auto result = compiler.run<Estimate>(measure, Target{/* ... */});
```

No base class, generated wrapper, trait specialization, or adapter object is
required. Registration is one-to-one between a linked Module Type declaration
and a copyable C++ type.

The shipped `ir.module` type uses the same mechanism with the standard
`joggle::ir::Module` representation:

```cpp
auto ir = compiler.module("ir");
auto module_type = ir ? ir->type("module") : std::nullopt;
if (module_type) {
  compiler.represent<joggle::ir::Module>(*module_type);
}
```

An `ir::Module` owns named executable Functions. Its copy-on-write storage
makes ordinary by-value pass composition isolated without eagerly duplicating
the whole program.

A parameterized representation adds one ordinary projection lambda. It returns
the declaration's parameters in their declared order:

```joggle
type target(lanes: int);
type estimate(lanes: int);
fn measure<N: int>(input: target<N>) -> estimate<N>;
```

```cpp
compiler.represent<Target>(target_type, [](const Target& value) {
  return std::tuple{value.lanes};
});
compiler.represent<Estimate>(estimate_type, [](const Estimate& value) {
  return std::tuple{value.lanes};
});
```

Joggle constructs the concrete `target<value.lanes>` or
`estimate<value.lanes>` itself. The existing type solver checks these concrete
instances before native code executes and checks its result afterward, so a
binding cannot silently erase or change dependent type arguments. A
parameterized declaration registered without a projection is rejected.

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

Compiler-domain lists use ordinary C++ vectors. The declaration remains the
schema authority, so no wrapper or generated type is required:

```joggle
fn volume(shape: list<int>) -> int;
```

```cpp
compiler.bind(volume, [](const std::vector<std::int64_t>& shape) {
  return std::accumulate(shape.begin(), shape.end(), std::int64_t{1},
                         std::multiplies<>{});
});
```

The built-in mappings are `list<int>`, `list<real>`, `list<bool>`,
`list<string>`, `list<type>`, and `list<attr>` to the corresponding
`std::vector<T>`. Decoding is guided by the resolved function parameter, not by
guessing from payload elements. An empty list therefore retains its declared
element domain during native evaluation and can safely participate in type,
shape, layout, or schedule computation.

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
