# C++ API

The C++ API mirrors the language ownership model:

```text
Compiler -> Module declarations
Function -> Block -> Instruction / Value
```

`Graph`, `Region`, `Operation`, and `Property` are not public ownership
objects. Graph algorithms may later expose non-owning views over a `Function`.

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

## Registered behavior

External `fn` declarations may bind to typed C++ callables. A binding is keyed
by the full Module symbol identity, not a textual name. The same declaration is
used for Known evaluation and Residual calls; Joggle does not maintain separate
operator, compile-time-function, operation, or pass namespaces.

Compiler-oriented functions currently use `Compiler::bind` and
`Compiler::run`. The registration surface is being generalized from the
bootstrap scalar/Function representations to Module-declared host value
representations. See [the execution model](execution-model.md) for the stable
staging contract and [bindings](bindings.md) for the current adapter API.

## Formatting and verification

```cpp
std::string source = joggle::format(*function, "main");
bool valid = compiler.verify(*function);
```

Formatting emits canonical Function syntax with explicit Blocks when control
flow cannot be represented by the structured surface form. Verification uses
the same declaration contracts as parsing and programmatic construction.
