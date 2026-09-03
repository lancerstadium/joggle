# C++ API

The C++ API mirrors the language ownership model:

```text
Compiler -> Module declarations
Function -> Block -> Instruction / Value
```

`Graph`, `Region`, `Operation`, and `Property` are not public ownership
objects. Graph algorithms query a `Function` and return analysis results;
they do not introduce a second program representation.

## Load and reflect Modules

```cpp
joggle::Compiler compiler;
compiler.load("model.joggle");
if (!compiler.link()) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}

auto module = compiler.module("model");
auto add = module ? module->function("add") : std::nullopt;
auto i32 = compiler.make("i32");
```

`Module::FunctionDecl`, `Module::TypeDecl`, and
`Module::AttributeDecl` are immutable, content-identified handles. Prelude
types use their language spelling. Parameterized types use a declaration:

```cpp
auto tensor = module->type("tensor");
auto value_type = compiler.make(*tensor, *i32,
                                std::vector<std::int64_t>{1, 64});
```

`Compiler::make`, `Type::get<T>`, and `Attribute::get<T>` support integers,
reals, Booleans, strings, types, attributes, and homogeneous lists.

Compile-time evaluation is bounded so malformed or adversarial Modules cannot
make specialization unbounded. Applications may choose deterministic limits
when constructing the compiler:

```cpp
joggle::Compiler compiler({.steps = 100'000, .depth = 256});
```

The step count covers expression evaluation and the depth bound covers nested
expressions and text-defined calls. Exhaustion is a diagnostic; it never
silently changes a Known expression into a Residual one.

## Known and Residual Values

A `Value` always has a `Type`. Availability is an independent property:

```cpp
auto int_type = compiler.make("int");
auto width = compiler.known(*int_type, std::int64_t{8});

width->known();                 // true
width->type() == *int_type;     // true
width->get<std::int64_t>();     // 8
```

A function argument or instruction result is normally Residual. One
instruction argument sequence may contain both kinds. The sequence follows the
callee declaration order; there is no separate property map or setter.

When a Known payload must become a program value, the source Module provides a
normal function implementing `prelude.literal`. The evaluator emits that
function as an Instruction; C++ does not register a separate materializer
object. This keeps custom fixed-point, packed, FPGA, or target-specific
constants visible to the same transformation API as every other operation.

Known inputs specialize a named Function before its residual boundary is
created:

```cpp
auto int_type = compiler.make("int");
auto width = compiler.known(*int_type, std::int64_t{8});
auto kernel_decl = module->function("kernel");
auto kernel = compiler.function(*kernel_decl, {*width});
```

For `fn kernel<N: int>(width: N, input: tensor<f32, [N]>)`, the resulting
`Function` has one Residual `tensor<f32, [8]>` argument. The Known `width`
remains available while evaluating its body but is not exposed as a runtime
parameter. Defaults are inserted through the same mechanism.

## Construct a Function

```cpp
auto function = compiler.function();
auto edit = function->edit();

auto lhs = edit.argument(*i32);
auto rhs = edit.argument(*i32);
auto sum = edit.append(*add, {lhs, rhs}).value();
edit.ret(function->entry(), {sum});

joggle::Diagnostics diagnostics;
if (!edit.commit(diagnostics)) {
  diagnostics.print(std::cerr);
}
```

`Instruction::arguments()` returns the complete ordered argument sequence.
`Instruction::results()` returns zero or more Residual results, while
`value()` is the checked one-result convenience. `Instruction::get<T>(name)`
reads a named Known argument.

Result types are inferred from the Function declaration whenever possible. An
explicit result-type vector constrains result-only generics. Missing, extra,
or incompatible Known arguments are rejected at construction. Structural and
type-contract verification occurs again at commit.

A named Function can be used as a callable `Value` without emitting a wrapper
Instruction:

```cpp
auto callable = compiler.make(
    *callable_type,
    std::vector<joggle::Type>{*i32},
    std::vector<joggle::Type>{*i32});
auto body = edit.reference(*callback, *callable);
auto output = edit.append(*map, {input, body}).value();
```

`Value::referenced_function()` exposes the exact Module declaration. Commit
checks that the callable input/result types match that declaration, and a
function reference dominates every Block in its owning Function. Serialization
prints the qualified Function name and includes its Module as a dependency.
In source bodies, the same callable type may flow backward from an enclosing
higher-order call to select an overload or specialize a generic Function.

Edits are transactional. A failed commit or an abandoned `Edit` restores the
previous Function. `replace`, `insert`, and `erase` operate in the same
transaction.

## Control flow

Blocks are siblings owned by the Function. Instructions never own Blocks:

```cpp
auto condition = edit.argument(*i1);
auto lhs = edit.argument(*i32);
auto rhs = edit.argument(*i32);

auto yes = edit.block();
auto no = edit.block();
auto merge = edit.block({*i32});

edit.branch(function->entry(), condition, yes, {}, no, {});
edit.jump(yes, merge, {lhs});
edit.jump(no, merge, {rhs});
edit.ret(merge, {merge.arguments().front()});
```

Typed successor arguments perform value merging, so no public phi instruction
or Region protocol is needed. Verification checks reachability, dominance,
branch condition type, edge arity and types, and consistent Function returns.

The source language normally creates this shape from an ordinary `if`.
Low-level Block construction exists for transformations and lossless formatting.

Passes inspect the same objects they edit. Reverse relations are direct
`Function` queries rather than handles into a second graph:

```cpp
auto incoming = function->predecessors(merge);
auto consumers = function->users(value);

if (function->has_uses(value) &&
    function->dominates(definition, consumer)) {
  // The rewrite is structurally safe.
}
```

`users` returns consuming Instructions once each, in Function order. `has_uses`
also sees branch conditions, successor arguments, and returns, which are owned
by terminators rather than represented as fake Instructions. Block dominance
is available through `dominates(block, block)`; value-to-Instruction dominance
also respects Function arguments, Block arguments, and instruction order.
These are snapshot queries over the current committed Function. Libraries may
cache richer analyses, but those results never own program objects.

## Executable modules

`joggle::ir::Module` carries several named Functions through ordinary compiler
functions without adding a Graph abstraction:

```cpp
joggle::ir::Module program;
joggle::Diagnostics diagnostics;
program.insert("main", std::move(*function), diagnostics);

auto main = program.function("main");
```

Copies share Functions until mutable lookup. Calling the non-const `function`
overload detaches only that Function, so by-value compiler functions are
isolated without an eager whole-program clone. The `ir.module` declaration is
shipped in `modules/ir.joggle`; applications register this standard C++
representation through the same `Compiler::represent` API as any other
Module-declared host type.

The ordinary `format` overload emits a canonical, installable source Module:

```cpp
auto dependencies = joggle::ir::dependencies(program);
auto source = joggle::format(program, "compiled_model", {1, 0, 0});
```

Dependency discovery traverses every Function and all of its CFG blocks rather
than only the entry return. The CLI uses this same path when publishing
transformed Functions, so library and command-line artifacts cannot drift into
different formats.

Typed tools can inspect a declaration without reproducing Joggle name
resolution:

```cpp
if (compiler.invocable<joggle::ir::Module, joggle::ir::Module>(pass)) {
  // A whole-program transform.
}
```

`invocable<Result, Args...>` uses the same linked type and host-representation
rules as `run`, but emits no diagnostic. It is intended for overload
selection and capability discovery; an actual invocation still performs full
argument, result, and IR verification.

## Registered behavior

External `fn` declarations may bind to typed C++ callables. A binding is keyed by
the full Module symbol identity, not a textual name. The same declaration is
used for Known evaluation and Residual calls; Joggle does not maintain separate
operator, compile-time-function, operation, or pass namespaces. `=` only binds
a source name. Prefix `@` adds a Known-result requirement to the ordinary
expression evaluator; it does not select another callable or assignment rule.

Compiler-oriented functions currently use `Compiler::bind` and
`Compiler::run`. Parameterless Module types may be associated one-to-one with
ordinary copyable C++ types through `Compiler::represent<T>`, after which the
same typed binding and invocation path accepts them. Parameterized host types
provide a projection lambda returning a `std::tuple` of declaration parameters;
the compiler constructs and validates the concrete `Type` and preserves it
through composed compiler functions. See
[the execution model](execution-model.md) for the staging contract and
[bindings](bindings.md) for examples.

## Formatting and verification

```cpp
std::string source = joggle::format(*function, "main");
bool valid = compiler.verify(*function);
```

Formatting emits canonical Function syntax with explicit Blocks when control
flow cannot be represented by the structured surface form. Verification uses
the same declaration contracts as parsing and programmatic construction.
