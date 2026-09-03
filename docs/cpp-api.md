# C++ API

Include the complete public surface with:

```cpp
#include <joggle/joggle.h>
```

Joggle is a C++20 library. A Module is the sole top-level IR owner.
`Module::FunctionDecl` handles are immutable; materialized `ir::Function` body
edits are explicit transactions.

## Namespaces and ownership

| Type | Role |
| --- | --- |
| `joggle::Compiler` | Linked environment, behavior, execution, diagnostics |
| `joggle::Module` | Versioned symbol scope and multi-Function IR owner |
| `joggle::Module::FunctionDecl` | Named callable member and canonical signature |
| `joggle::Type`, `joggle::Attribute` | Immutable schema instances |
| `joggle::ir::Function` | Copy-on-write materialized CFG value of a Module Function |
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
Module's `interface`, `type`, `attribute`, `function`, `overloads`, or
`members` queries. `compiler.lookup("module.function")` performs the same
unique Function-member lookup across the linked environment. Neither query
constructs IR; `compiler.materialize(...)` does that explicitly. Symbols expose
qualified and stable names.

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

Known values are passed to `Compiler::materialize` to specialize compiler
inputs. They are not inserted as runtime Function arguments.

## Instantiate a source Function

```cpp
auto declaration = model->function("kernel");
auto function = declaration && width
                    ? compiler.materialize(*declaration, {*width})
                    : std::nullopt;
```

The resulting `ir::Function` contains only its Residual boundary. Source
bodies, generic values, defaults, and `@` expressions are evaluated while it
is built. Use `joggle::format(*function, "kernel")` for canonical source.

`Compiler::create_function()` creates an unnamed empty `ir::Function` for
programmatic construction. It does not perform declaration lookup or
source-body specialization.

## Edit a Function

```cpp
auto function = compiler.create_function();
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

`ir::Function` copies share an immutable revision until one copy starts an
edit. This makes read-only analysis handoff constant-time while preserving
value semantics for `function -> function` transformations. `Compiler::run`
creates that COW value boundary once; it does not eagerly deep-copy a second
checkpoint. A successful edit detaches, while a failed pipeline simply does
not publish its private value.

`compiler.verify(function)` validates one body against the linked declaration
environment. `compiler.verify(module)` validates every materialized Function in
the Module. Typed invocation performs the same validation at Function and
Module input/output boundaries, so an analysis or emitter never receives an
unverified artifact.

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

## Rewrite transactionally

`joggle::ir::rewrite` accepts a lambda over each committed Instruction and a
single `Function::Edit`. The lambda may insert calls, replace one call with a
positional result list, redirect uses, or erase an unused Instruction. It
returns `true` only when it changed the IR. The Function overload commits one
verified transaction; the Module overload publishes only after every changed
Function verifies.

No-op sweeps preserve the Function revision and Module storage. Failure returns
`std::nullopt` and preserves the complete input value.

`joggle::ir::rewrite_to_fixpoint` repeats those sweeps with a required maximum
iteration count. It publishes only after a zero-change sweep proves
convergence; reaching the limit rolls back every intermediate sweep.

`joggle::ir::convert` takes the same rewrite lambda followed by a legality
predicate over the resulting Instructions. It commits only if the complete
Function or Module is legal. Legality is caller-defined, so the utility does
not introduce a target registry or assume that conversions move downward.

## Map calls transactionally

`joggle::ir::replace_calls` replaces one exact declaration with another in a
Function or Module. `joggle::ir::map_calls` accepts a callable returning an
optional replacement declaration for each Instruction. Both return an optional
change count: zero means a successful no-op and absence means failure.

Both overloads use the same `rewrite` transaction. The Module overload edits a
private value and publishes it only when every changed Function verifies.
Unchanged Functions retain their shared storage.

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

for (const auto function : module.functions()) {
  if (const auto* body = function.body()) {
    // Inspect or transform the materialized CFG.
  }
}
const auto main_decl = module.function("main");
auto* main = main_decl ? module.body(*main_decl) : nullptr;
```

Copies use Function-granular copy-on-write. Const lookup shares; mutable lookup
detaches the state selected by its complete `FunctionDecl`. `insert` accepts
same-name Functions when their signatures differ and rejects only an exact
signature duplicate; consequently, mutable access never guesses among
overloads. `joggle::format(module)` emits that same Module as canonical source
and derives exact dependencies from its current IR, so a transformation cannot
leave a stale import list behind.

The embedded Prelude type `module` is automatically represented by
`joggle::Module`; `function` is represented by a materialized
`joggle::ir::Function` body. Compiler-function signatures using either type
need no generated wrapper or manual `Compiler::represent` call.

## Bind and run compiler functions

The Compiler installs Hermetic bindings for the exact arithmetic, comparison,
logic, and helper functions declared by the embedded Prelude. Extension
functions remain explicit:

```cpp
const auto analysis_module = compiler.module("analysis");
const auto estimate_decl =
    analysis_module ? analysis_module->function("estimate") : std::nullopt;

compiler.bind(*analysis_module, "estimate",
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
const auto classify_decl = analysis_module->function("classify");
compiler.bind(*analysis_module, "classify", [](std::int64_t value) {
  return std::tuple{value < 0 ? -value : value, value >= 0};
});

auto result = compiler.run<std::tuple<std::int64_t, bool>>(
    *classify_decl, std::int64_t{-4});
```

The tuple is a positional boundary mapping, not a Joggle wrapper object.
Binding by Module and local name uses the callable signature to select an
overload. Passing a reflected `FunctionDecl` remains available when code
already needs the declaration as a rewrite target or exact identity.
`invocable<Result, Args...>` checks the entire reflected C++ signature.
`lookup("module.function")` reflects one unique linked Function member for
tools that receive a qualified name; the same handle is passed to `run`.
Declared inputs bind by value or `const&`, never by mutable lvalue reference.
A transformation returns its changed artifact instead of creating an
undeclared in-place output.
Function transforms, Module transforms, analyses, loaders, and emitters all
use the same `run<Result>(declaration, arguments...)` operation. Assigning a
returned artifact is an ordinary C++ choice rather than a transformation-only
Compiler API.

Semantic hooks use `compiler.verify(declaration, callback)`. This is distinct
from `bind`: verification augments the invariant of a Type, Attribute, or
residual Instruction, while binding implements a declared `fn` and must match
its complete signature.

See [Extensions](extensions.md) for representations, verifiers, interface
methods, and behavior libraries.
