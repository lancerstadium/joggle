# C++ API

Include the complete public surface with:

```cpp
#include <joggle/joggle.h>
```

Joggle is a C++20 library. A Module is the sole top-level IR owner. Declaration
handles are immutable and Function edits are explicit transactions.

## Namespaces and ownership

| Type | Role |
| --- | --- |
| `joggle::Compiler` | Linked environment, behavior, execution, diagnostics |
| `joggle::Module` | Versioned symbol scope and multi-Function IR owner |
| `joggle::Type`, `joggle::Attribute` | Immutable schema instances |
| `joggle::ir::Function` | Executable IR owner |
| `joggle::ir::Block` | CFG node owned by a Function |
| `joggle::ir::Instruction` | Declared call owned by a Block |
| `joggle::ir::Value` | Typed Known or Residual value handle |
| `joggle::ir::Terminator` | Return, jump, or branch owned by a Block |

There are no `joggle::Function` or `joggle::Value` aliases.

## Parse, load, and link

Parse a standalone Module when only syntax and reflection are needed:

```cpp
joggle::Diagnostics diagnostics;
auto module = joggle::parse_module(text, diagnostics, "model.joggle");
auto canonical = module ? joggle::format(*module) : std::string{};
```

Use `Compiler` for imports, type checking, behavior, and execution:

```cpp
joggle::Compiler compiler({.steps = 100'000, .depth = 256});
compiler.search(module_root);
compiler.load("model.joggle");
compiler.lock("joggle.lock");  // optional exact replay

if (!compiler.link()) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}
```

`add(text, source)` loads memory text. `load(path)` loads a file. `search(root)`
adds an installed-Module repository. `link()` resolves and seals the complete
environment. Diagnostics accumulate and retain source ranges.

Reflect declarations through `compiler.module(name)` and the returned
Module's `interface`, `type`, `attribute`, `declaration`, `overloads`, or
`members` queries. `function(name)` is reserved for a materialized editable
Function, so signature reflection cannot be confused with IR lookup. Symbols
expose qualified and stable names.

## Construct types, attributes, and Known values

Parameterless Prelude types use their spelling:

```cpp
auto i32 = compiler.make("i32");
auto integer = compiler.make("int");
```

Parameterized declarations take ordinary C++ values:

```cpp
auto model = compiler.module("model");
auto tensor = model ? model->type("tensor") : std::nullopt;
auto shaped = tensor && i32
                  ? compiler.make(*tensor, *i32,
                                  std::vector<std::int64_t>{1, 64})
                  : std::nullopt;
```

Read declared or derived parameters by name:

```cpp
auto shape = shaped->get<std::vector<std::int64_t>>("shape");
```

`Compiler::known(type, payload)` creates a typed Known `ir::Value`. Supported
payloads are compiler scalars, Types, Attributes, and homogeneous ranges:

```cpp
auto width = compiler.known(*integer, std::int64_t{8});
```

Known values are passed to `Compiler::function` to specialize compiler inputs.
They are not inserted as runtime Function arguments.

## Instantiate a source Function

```cpp
auto declaration = model->declaration("kernel");
auto function = declaration && width
                    ? compiler.function(*declaration, {*width})
                    : std::nullopt;
```

The resulting `ir::Function` contains only its Residual boundary. Source
bodies, generic values, defaults, and `@` expressions are evaluated while it
is built. Use `joggle::format(*function, "kernel")` for canonical source.

`Compiler::function()` creates an unnamed empty Function for programmatic
construction.

## Edit a Function

```cpp
auto function = compiler.function();
auto edit = function->edit();

auto lhs = edit.argument(*i32);
auto rhs = edit.argument(*i32);
auto sum = edit.append(*add_decl, {lhs, rhs}).value();
edit.ret(function->entry(), {sum});

joggle::Diagnostics diagnostics;
if (!edit.commit(diagnostics)) {
  diagnostics.print(std::cerr);
}
```

`append`, `insert`, `replace`, and `erase` operate in the transaction. Result
types are inferred from declaration contracts when possible. Explicit result
types constrain result-only generics. Dropping an uncommitted edit, or failing
commit, restores the prior Function.

Construct control flow with sibling Blocks and typed edges:

```cpp
auto yes = edit.block();
auto no = edit.block();
auto merge = edit.block({*i32});

edit.branch(function->entry(), condition, yes, {}, no, {});
edit.jump(yes, merge, {lhs});
edit.jump(no, merge, {rhs});
edit.ret(merge, {merge.arguments().front()});
```

Verification checks ownership, reachability, dominance, terminators, edge
arity and types, result signatures, Known arguments, and extension contracts.
Instructions never own Blocks.

## Inspect relations

```cpp
for (const auto& block : function->blocks()) {
  for (const auto& instruction : block.instructions()) {
    for (const auto& result : instruction.results()) {
      auto consumers = function->users(result);
    }
  }
}
```

`predecessors`, `users`, `has_uses`, and `dominates` query the committed
Function directly. They include terminator uses where appropriate and do not
create a Graph owner. Analysis libraries may cache their own products against a
Function snapshot.

## Map calls transactionally

`joggle::ir::replace_calls` replaces one exact declaration with another in a
Function or Module. `joggle::ir::map_calls` accepts a callable returning an
optional replacement declaration for each Instruction. Both return an optional
change count: zero means a successful no-op and absence means failure.

The Function overload uses one `Function::Edit`. The Module overload plans
the complete mapping, edits a private copy, and publishes it only when every
changed Function verifies. Unchanged Functions retain their shared storage.

## Function references

A named declaration can become a typed callable Value without a wrapper call:

```cpp
auto callable = compiler.make(
    *callable_decl,
    std::vector<joggle::Type>{*i32},
    std::vector<joggle::Type>{*i32});
auto callback = edit.reference(*callback_decl, *callable);
```

`Value::referenced_function()` recovers the exact declaration. Commit verifies
the callable signature and Module dependency.

## Modules

```cpp
joggle::Module module("compiled_model", {1, 0, 0});
joggle::Diagnostics diagnostics;
module.insert("main", std::move(*function), diagnostics);

const auto names = module.function_names();
auto* main = module.function("main");
```

Copies use Function-granular copy-on-write. Const lookup shares; mutable lookup
detaches the selected Function state. `joggle::format(module)` emits that same
Module as canonical source and derives exact dependencies from its current IR,
so a transformation cannot leave a stale import list behind.

The embedded Prelude type `module` is automatically represented by
`joggle::Module`; `function` is represented by `joggle::ir::Function`.
Compiler-function signatures using either type need no generated wrapper or
manual `Compiler::represent` call.

## Bind and run compiler functions

The Compiler installs Hermetic bindings for the exact arithmetic, comparison,
logic, and helper functions declared by the embedded Prelude. Extension
functions remain explicit:

```cpp
compiler.bind(*estimate_decl,
              [](const joggle::ir::Function& function) -> std::int64_t {
                return static_cast<std::int64_t>(
                    function.instructions().size());
              });

auto estimate = compiler.run<std::int64_t>(*estimate_decl, *function);
```

Function results have one direct C++ mapping:

| Joggle result sequence | C++ binding return | Invocation return |
| --- | --- | --- |
| no results | `void` | `run<void>` returns `bool` |
| one `T` | `T` or `std::optional<T>` | `run<T>` returns `std::optional<T>` |
| `(T, U, ...)` | `std::tuple<T, U, ...>` or its `std::optional` form | `run<std::tuple<T, U, ...>>` |

For example:

```cpp
compiler.bind(*classify_decl, [](std::int64_t value) {
  return std::tuple{value < 0 ? -value : value, value >= 0};
});

auto result = compiler.run<std::tuple<std::int64_t, bool>>(
    *classify_decl, std::int64_t{-4});
```

The tuple is a positional boundary mapping, not a Joggle wrapper object.
`invocable<Result, Args...>` checks the entire reflected C++ signature.
A mutable Function transform can also be invoked with
`compiler.run(function, transform)`.

See [Extensions](extensions.md) for representations, verifiers, interface
methods, and behavior libraries.
