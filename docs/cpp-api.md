# C++ API

Include the complete public surface with:

```cpp
#include <joggle/joggle.h>
```

Joggle is a C++20 library. A Module is the sole top-level IR owner.
`Module::FunctionDecl` handles are immutable; materialized `Function` body
edits are explicit transactions.

`joggle::sha256(bytes)` returns the lowercase hexadecimal digest of an exact,
length-aware byte string. Module and repository identities use the same public
primitive. `Module::store(bytes)` returns the compatible
`sha256:<digest>` name, deduplicates equal payloads, and keeps the immutable
data inside that Module value. `Module::data(name)` returns a read-only span;
`Module::data()` lists names in canonical order.

## Namespaces and ownership

| Type | Role |
| --- | --- |
| `joggle::Compiler` | Linked environment, staging, execution, diagnostics |
| `joggle::Module` | Versioned symbol scope and multi-Function IR owner |
| `joggle::Module::FunctionDecl` | Named callable member and canonical signature |
| `joggle::Type`, `joggle::Attribute` | Immutable schema instances |
| `joggle::Function` | Copy-on-write materialized CFG value of a Module Function |
| `joggle::Block` | CFG node owned by a Function |
| `joggle::Op` | Declared call owned by a Block |
| `joggle::Value` | Typed Known or Residual value handle |
| `joggle::Terminator` | Return, jump, or branch owned by a Block |

These are concrete public types in one namespace. There is no parallel
`joggle::ir` namespace and no compatibility alias layer.

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

`add(module)` places an existing Module value into an unlinked Compiler
without formatting it or writing a temporary file. Copies preserve
content-addressed data and materialized bodies. This is the intended entry for
an artifact decoder or an in-memory package resolver; the same identity and
import-conflict checks apply as for source input.

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

`Compiler::known(type, payload)` creates a typed Known `Value`. Supported
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

The resulting `Function` contains only its Residual boundary. Source
bodies, generic values, defaults, and `@` expressions are evaluated while it
is built. Use `joggle::format(*function, "kernel")` for canonical source.

`Compiler::create_function()` creates an unnamed empty `Function` for
programmatic construction. It does not perform declaration lookup or
source-body specialization.

`Compiler::materialize(module)` copies a linked Module and materializes every
source Function whose results are IR-representable and whose compiler inputs
and generics have a complete default specialization. External declarations,
compiler-result functions, and specializations that still need Known arguments
remain declarations. The linked source Module is unchanged; the returned value
is suitable for `module -> module` compiler functions.

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

An Op has one declaration but exposes its arguments by role:

```cpp
for (const auto& operand : op.operands()) {
  // SSA data-flow edge.
}
for (const auto& [name, property] : op.properties()) {
  // Immutable compiler-domain input.
}
auto axis = op.property<std::int64_t>("axis");
```

The split follows declared input domains, not whether a particular Value is
currently Known. Supplying a Known scalar to an `i32` port still creates an
operand; an `int` port is a property. Passes therefore share the same schema as
source calls and native behavior.

`Function` copies share an immutable revision until one copy starts an
edit. This makes read-only analysis handoff constant-time while preserving
value semantics for `function -> function` transformations. `Compiler::run`
creates that COW value boundary once; it does not eagerly deep-copy a second
checkpoint. A successful edit detaches, while a failed pipeline simply does
not publish its private value.

`compiler.verify(function)` validates one body against the linked declaration
environment. `compiler.verify(module)` validates every materialized Function in
the Module. Typed invocation performs the same validation at Function and
Module input/output boundaries, so an analysis or emitter never receives an
unverified Module.

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
arity and types, result signatures, Known arguments, and Module contracts.
Ops never own Blocks.

## Inspect relations

```cpp
for (const auto& block : function->blocks()) {
  for (const auto& op : block.ops()) {
    for (const auto& result : op.results()) {
      auto consumers = result.users();
    }
  }
}
```

`Value::defining_op()` and `Value::users()` provide the common local def-use
walk for Residual values. `Function::predecessors`, `users`, `has_uses`, and
`dominates` query the committed Function directly. They include terminator
uses where appropriate and do not
create a Graph owner. Analysis libraries may cache their own products against a
Function snapshot.

## Rewrite transactionally

`joggle::rewrite` accepts a lambda over each committed Op and a
single `Function::Edit`. The lambda may insert calls, replace one call with a
positional result list, redirect uses, or erase an unused Op. It
returns `true` only when it changed the IR. The Function overload commits one
verified transaction; the Module overload publishes only after every changed
Function verifies.

No-op sweeps preserve the Function revision and Module storage. Failure returns
`std::nullopt` and preserves the complete input value.

`joggle::rewrite_to_fixpoint` repeats those sweeps with a required maximum
iteration count. It publishes only after a zero-change sweep proves
convergence; reaching the limit rolls back every intermediate sweep.

`joggle::convert` takes the same rewrite lambda followed by a legality
predicate over the resulting Ops. It commits only if the complete
Function or Module is legal. Legality is caller-defined, so the utility does
not introduce a target registry or assume that conversions move downward.

## Clone while changing representation

`joggle::clone(compiler, function, map_value_type, diagnostics)` preserves the
full CFG while applying `map_value_type` to Function arguments, Block
arguments, Op results, callable references, and returns. The callback receives
the source Value, so it can distinguish graph inputs, constants, intermediate
results, and block arguments when choosing a representation. An overload also
accepts a callee mapper for one-to-one vocabulary conversion. Known properties
remain the same typed Values. Failure to map or verify any element returns no
Function.

The result is a standalone Function ready for `Module::insert`. This is the
primitive for tensor element, layout, or reference-type conversion; local
many-to-one and one-to-many patterns still use `rewrite` before or after the
clone.

## Specialize to an accepted boundary

`Compiler::specialize` recursively materializes source-defined calls until a
caller predicate accepts every remaining call:

```cpp
auto closed = compiler.specialize(
    module,
    [&](const joggle::Module::FunctionDecl& function) {
      return compiler.conforms(function, target_primitive);
    },
    diagnostics);
```

Concrete Known properties are baked into each generated local Function;
Residual operands remain ordinary SSA arguments. Repeated concrete
specializations are shared. The input Module is unchanged, and an opaque
unaccepted call or recursive expansion returns `std::nullopt`.

This utility is intended for emitters and other consumers that define a typed
acceptance boundary. It does not register a target, assign a lowering level, or
interpret Function names.

## Map calls transactionally

`joggle::replace_calls` replaces one exact declaration with another in a
Function or Module. `joggle::map_calls` accepts a callable returning an
optional replacement declaration for each Op. Both return an optional
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
signature duplicate. Insertion also attaches the new declaration to the body
and fixes its argument and result contract; later body edits cannot change the
member signature. Consequently, mutable access never guesses among overloads
and `Compiler::verify(module)` can reject a body attached to the wrong member.
The generated declaration retains fully qualified type identities internally,
which makes newly inserted members immediately usable as call targets from
other materialized Functions in the same Module.
`joggle::format(module)` emits that same Module as canonical source and derives
exact dependencies from its current IR, so a transformation cannot leave a
stale import list behind.

`module.digest()` hashes the complete canonical artifact. It changes after a
committed body transformation and is the identity used by repositories, locks,
and behavior libraries. `module.interface_digest()` hashes imports and member
declarations with Function bodies erased, and Compiler boundaries use it for
exact interface compatibility. A `Module::Symbol` has a versioned qualified
logical name and retains the interface digest only as provenance. Existing
types, call targets, and verifier registrations therefore survive body-only
transformations and unrelated additions to a Module.

The embedded Prelude type `module` is automatically represented by
`joggle::Module`; `function` is represented by a materialized
`joggle::Function` body. Compiler-function signatures using either type
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
              [](const joggle::Function& function) -> std::int64_t {
                return static_cast<std::int64_t>(
                    function.ops().size());
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
tools that need a declaration handle. In contrast,
`run<Result>("module.function", arguments...)` uses its input and result C++
types to select an overload directly.
Declared inputs bind by value or `const&`, never by mutable lvalue or rvalue
reference. Declared results return by value. A transformation returns its
changed artifact instead of creating an undeclared in-place output.
Function transforms, Module transforms, analyses, loaders, and emitters all
use the same `run<Result>(declaration, arguments...)` operation. Assigning a
returned artifact is an ordinary C++ choice rather than a transformation-only
Compiler API.

Semantic hooks use `compiler.verify(declaration, callback)`. This is distinct
from `bind`: verification augments the invariant of a Type, Attribute, or
residual Op, while binding implements a declared `fn` and must match
its complete signature.

See [Modules](modules.md) for representations, verifiers, interface
methods, and behavior libraries.
