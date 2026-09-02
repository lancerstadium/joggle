# Execution model

This document fixes Joggle's staging and control-flow semantics. It is the
design baseline for the parser, evaluator, IR, and extension API.

## One value model

A value has a type and an availability. These are independent facts:

```text
type          what operations are valid
availability  Known(value) | Residual(value)
```

`Known` means the compiler owns a concrete value now. `Residual` means the
value must remain in the generated program. A target `i32` constant can be
Known; a symbolic `int` parameter can be Residual. Therefore a type name does
not select a compilation stage.

`ParameterDecl` has no static/value kind. Declaration checking resolves whether
an annotation denotes a compiler domain or a program type, while every
instantiated call carries one ordered sequence of `Value` arguments.

## One function and one overload set

Functions do not have compile-time and run-time variants. Calls and operator
spellings use one overload set:

```joggle
fn combine(lhs: int, rhs: int) -> int as // {
  return lhs + rhs;
}

fn tiles(n: int, width: int) -> int {
  return n // width;
}
```

`combine(n, width)` and `n // width` resolve to the same declaration. The
evaluator then acts on the resolved call:

1. If every required input is Known and the function has an evaluable body or
   registered evaluator, execute it and return Known.
2. Otherwise, if residual execution is legal, emit a call and return
   Residual.
3. If neither is possible, report a staging error at the call site.

An external declaration needs at most one host binding:

```cpp
compiler.bind(double_width, [](std::int64_t value) { return value * 2; });
```

When its arguments are Known, that binding is the function's implementation
and may produce a Known result. When an argument is Residual, the evaluator
keeps an ordinary call to the same function symbol. Residualization therefore
does not require an `emit` callback, an intrinsic subclass, or a second
registration table. A later function may transform that call for a target.

An unbound external call is legal while it can remain Residual. It is an error
only when a Known result is required, for example below `@(...)` or inside a
type argument.

## `@` is a requirement, not another language

`@` applies to any expression:

```joggle
count = @(elements // width);
shape = @([batch, count]);
choice = @(if configured { lhs } else { rhs });
```

Canonical source always writes `@(expression)`. Evaluation proceeds normally,
then `@` requires the result to be Known. A Residual result is a diagnostic.
The assignment operator has no staging meaning.

Contexts that intrinsically require a Known value, such as a type constructor
argument, impose the same requirement without needing `@`. Authors use `@`
when they want the constraint to be visible or when a mixed-stage function
would otherwise residualize the expression.

## Residualizing evaluation

Evaluation returns one of:

```text
Known(value)
Residual(expression, effects)
Error(diagnostic)
```

A Known value is not restricted to a closed kernel variant. Its representation
is a Joggle `Type` plus a type-erased host payload. A Module may associate one
of its declared types with an ordinary C++ type; typed `Compiler::bind`
adapters erase and recover that payload internally, so extension authors do
not implement an evaluator interface or subclass an IR node.

Equality, canonical serialization, and materialization are optional
capabilities of a host representation. Equality can preserve a Known value
across a control-flow join. Serialization enables deterministic caches and
artifacts. Materialization turns a Known value into a residual constant when
it must cross a dynamic edge. A compile-time-only object needs no materializer;
it becomes an error only if the program tries to place it in residual code.

This makes constant folding, specialization, compile-time execution, and IR
construction one operation rather than four unrelated passes. It also gives
cloud/edge specialization a precise boundary: cloud-known model, device, and
shape facts are consumed; only the residual program is deployed.

Evaluation is deterministic. Recursion is permitted within explicit step,
depth, and allocation budgets; a repeated active invocation with identical
Known arguments is diagnosed as a non-progressing cycle. Non-termination,
integer overflow, forbidden effects, and budget exhaustion are errors rather
than reasons to silently change program meaning.

## Branches

`if` is an expression and its blocks return their final expression:

```joggle
fn choose<T>(condition: i1, lhs: T, rhs: T) -> T {
  return if condition {
    lhs
  } else {
    rhs
  };
}
```

The evaluator uses one rule:

- Known condition: evaluate only the selected block. No branch enters IR.
- Residual condition: validate both blocks, require compatible result types and
  effects, and create ordinary Blocks and typed successor edges.
- `@(if ...)` with a Residual condition: report that compile-time evaluation
  cannot be completed.

Both arms are name-resolved and type-checked during linking. "Evaluate only the
selected block" means that unselected host calls and effects do not run; it
does not make ill-typed source legal.

The residual form is target-neutral ordinary control flow:

```text
entry:                  then:              else:
  cond_br c, then, else   ...                 ...
                         br merge(a)         br merge(b)

merge(value: T):
  ...
```

Block arguments carry merged values, so the core needs no `phi` instruction
and no `Region` object. A later function may turn this CFG shape into
predication, a mux, or another target operation. Those choices are extensions,
not source-language control-flow kinds.

`return` and direct calls follow the same rule. Loops will use the same block
and typed-edge representation: a Known trip count may specialize or unroll;
a Residual trip count leaves a loop in the program. The source spelling will be
fixed only together with closure and loop-carried-value semantics, rather than
exposing an internal region/yield protocol.

## Nested code without `region`

`region` is not a source-language declaration or a public IR handle. Nested
code is an ordinary function value. Function types use `(inputs) -> results`;
a block literal is a closure expression:

```joggle
fn map<T, U>(input: tensor<T>, body: (T) -> U) -> tensor<U>;

fn activate(input: tensor<f32>) -> tensor<f32> {
  return map(input, { item =>
    relu(item)
  });
}
```

Multiple blocks are ordinary named arguments, using `:` for labels so `=`
remains a binding/default operator:

```joggle
return choose(
  condition,
  then: { => lhs },
  otherwise: { => rhs }
);
```

Common control flow receives direct syntax (`if`, later structured loops).
Extension-defined structured operations use function-typed parameters. Both
elaborate to functions and blocks. A captured closure is normalized to a
generated function whose captures are explicit parameters; it does not create
a second nested-code abstraction.

### Residual branch elaboration

For a Residual condition, elaboration snapshots the lexical environment and
insertion point, evaluates each arm into a sibling Block, and joins each result
position independently:

1. equal Known values remain one Known value and need no Block argument;
2. a Residual value, or unequal materializable Known values, is carried on the
   two successor edges and becomes a merge Block argument;
3. unequal host-only Known values are a staging diagnostic.

The branch itself is a terminator. It neither contains its arms nor owns a
hidden container. Host bindings carry an effect capability: an effectful host
call runs only beneath Known control; beneath Residual control it must remain
as a residual call or produce a diagnostic. The evaluator therefore never
speculatively performs I/O in both arms.

## No language-level `graph`

Data dependencies are properties of a function body; they do not require a
`graph` declaration or compiler domain. A `Compiler` owns the compilation;
the language-visible hierarchy stays:

```text
Module
  Function
    Block
      Instruction
```

There is no extra `Program` or `Graph` wrapper in the DSL. `Body` is likewise
not a handle users must pass around: a defined Function owns an entry Block and
any successor blocks. The implementation may use private storage objects, but
they do not become language concepts.

Straight-line bodies naturally form dataflow DAGs. Control flow adds blocks and
typed edges without changing the source member kind.

The word *graph* remains useful descriptively: def-use edges form a dataflow
graph and blocks form a control-flow graph. It is not, however, a declaration,
a value domain, or a top-level C++ ownership object in the final API.

Graph algorithms consume non-owning views such as `CFGView(Function)` and
`DefUseView(Function)`. A view may cache predecessors, dominators, use lists,
or traversal order, but it cannot own Blocks or serve as a language value.

Compiler transformations operate on registered IR handle types. A standard IR
Module may provide `ir.Module`, while an extension may register another handle
type. These are ordinary module-owned types with host representations, not
trusted kernel domains. Loading, analysis, transformation, and emission remain
ordinary registered functions over those types.

The C++ owning API is Function/Block/Instruction/Value. Analysis views may use
graph terminology, but they neither own nor mutate program objects. The
temporary compiler-callback `function` domain will disappear when ordinary
Module-declared handle types gain host representations.

## Minimal trusted kernel

The trusted implementation contains only:

- parsing and name resolution;
- types, functions, blocks, and values;
- overload resolution;
- Known/Residual evaluation;
- deterministic diagnostics and resource limits;
- registration hooks for host values and function implementations;
- construction of residual calls and control-flow instructions.

Device models, tensor vocabularies, FPGA formats, custom instructions, IR
modules, analyses, and emitters live in installable Modules.

## Implementation status

The Function-body evaluator now uses the same ordered `Value` arguments for
Known and Residual calls. It evaluates compiler-domain literals, arithmetic,
lists, type and attribute constructors, text-defined pure functions, and
registered external functions. Evaluation happens automatically when a call is
fully Known; `@(expression)` adds the explicit Known requirement.

Named Function construction accepts its compiler-domain inputs as Known
`Value`s. Those values bind generics and compute concrete residual input and
result types before the Function boundary is created; they do not become
runtime arguments.

Known `if` conditions select one arm without creating IR. A Residual `i1`
condition residualizes each expression arm independently, including nested
calls and nested `if`, then joins them through sibling Blocks and typed
successor arguments. Configurable expression-step and nesting-depth budgets
bound compile-time evaluation and fail with a diagnostic instead of silently
residualizing. General statement sequences inside arms, loops, closures,
allocation budgets, and materialization remain to be implemented. Registered
host implementations are conservatively forbidden beneath Residual control,
so constructing both branches never executes host side effects. A future
explicit pure/effect capability may permit safe calls without weakening this
default.
