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

An operator is only another spelling for a typed function. Its input and
result domains need not match, so an extension can define a compiler predicate
without adding a special expression node:

```joggle
fn less(lhs: int, rhs: int) -> bool as <;

fn earlier(lhs: int, rhs: int) -> int {
  return if lhs < rhs { lhs } else { rhs };
}
```

Binding `less` to an ordinary `bool(std::int64_t, std::int64_t)` C++ callable
makes the branch executable during specialization. The same overload spelling
can denote a Residual target operation when its declared operands are program
values. Comparison and logical operators are not hard-coded semantic cases:
`<=`, `>=`, `==`, `!=`, `&&`, `||`, prefix `!`, and extension-defined symbols
all enter this declaration lookup before the minimal bootstrap arithmetic is
considered. Overloads may share a symbol and result domain: Known operand
domains select between, for example, `int < int` and `string < string` before
either implementation runs.

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
only when a Known result is required, for example below `@` or inside a
type argument.

## `@` is a requirement, not another language

`@` applies to any expression:

```joggle
count = @(elements // width);
shape = @[batch, count];
tile = @choose_tile(shape, target);
```

`@` is a prefix requirement. Primary operands need no punctuation
(`@choose_tile(...)`, `@value`, `@[a, b]`); parentheses only group a larger
expression such as `@(elements // width)`. Evaluation proceeds normally, then
`@` requires the result to be Known. A Residual result is a diagnostic. The
assignment operator has no staging meaning.

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

A Known compiler-function value is not restricted to a closed kernel variant.
A Module may associate a parameterless declared type with an ordinary copyable
C++ type; typed `Compiler::bind` adapters erase and recover that payload
internally, so extension authors do not implement an evaluator interface or
subclass an IR node. For a parameterized declared type, registration supplies a
lambda returning its ordered parameter tuple. The compiler constructs the
concrete Joggle `Type`, carries it with the erased payload, and applies the
ordinary dependent-type solver before and after native execution. Schema-only
registration remains rejected because it would lose type arguments.

Equality and canonical serialization are optional capabilities of a future
host representation contract. Equality can preserve a Known value across a
control-flow join, while serialization enables deterministic caches and
artifacts.

Materialization itself remains in the language's single function system. The
Prelude declares `literal: fn`; visible Modules implement it with an ordinary
function from one compiler-domain payload to one program value. When a Known
value must cross a dynamic edge, overload and dependent-type resolution select
exactly one such function and emit its ordinary call. A compile-time-only
object needs no literal function; it becomes an error only if the program tries
to place it in residual code.

This makes constant folding, specialization, compile-time execution, and IR
construction one operation rather than four unrelated passes. It also gives
cloud/edge specialization a precise boundary: cloud-known model, device, and
shape facts are consumed; only the residual program is deployed.

Evaluation is deterministic. Recursion is permitted within explicit step,
depth, and allocation budgets; a repeated active invocation with identical
Known arguments is diagnosed as a non-progressing cycle. Non-termination,
integer overflow, forbidden effects, and budget exhaustion are errors rather
than reasons to silently change program meaning.

Scalar and list compiler domains follow the same rule. A registered evaluator
receives `list<int>`, `list<real>`, `list<bool>`, `list<string>`, `list<type>`,
and `list<attr>` as ordinary typed vectors. The resolved function signature
supplies the element domain, including for an empty list; staging never depends
on inspecting the first element of a payload.

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
- `@` applied to an `if` with a Residual condition: report that compile-time evaluation
  cannot be completed.

Both arms are name-resolved and type-checked during linking. "Evaluate only the
selected block" means that unselected host calls and effects do not run; it
does not make ill-typed source legal.

The statement form uses the identical rule for a sequence of computations.
Each arm starts from the same lexical environment. New names are arm-local;
outer names rebound in either arm are compared afterward. Equal values remain
unchanged, while differing compatible values become merge Block arguments. If
`else` is absent, the false arm contributes the incoming value. This is source
reassignment elaborated to SSA, not mutable storage in the IR.

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

`return` and direct calls follow the same rule. A source `while` evaluates in
the compiler while its condition is Known. A Residual `i1` condition creates
header, body, and exit Blocks. Lexical names rebound by the body are converted
to loop-carried Block arguments automatically. The source never exposes an
internal region, phi, iteration-argument, or yield protocol.

Returns inside structured control terminate only the current path. Residual
control therefore creates no join when both arms return and uses the sole
fallthrough arm directly when only one returns. A return from a loop body is a
normal Function exit; the loop's false edge remains the continuation.

`continue` and `break` use the same inferred loop-carried values as the normal
back edge and false exit. Under Residual loop control they are direct edges to
the header and exit Blocks. Under Known loop control they are evaluator
transfers and do not appear in the Function when their controlling path is
also Known.

When a Residual branch controls a transfer inside a finite Known loop, the
evaluator retains separate continuation paths with separate lexical
environments. It specializes the remaining statements on each path and delays
joining them until a program type is actually required. This is controlled
multi-versioning, not a hidden Region: each path is ultimately an ordinary
Block in the same Function. A Known value is materialized only at a typed use,
such as an `i32` return, so the compiler never guesses that compiler `int` or
`bool` means a particular target width.

A runtime-dependent cycle may prevent that path set from reaching a finite
specialization. The evaluator detects a repeated staged loop state before
blindly exhausting its budget. When the repeated path is already beneath
Residual control, it terminates the current Block with an edge to the Block
where that staged state first appeared. The resulting finite-state CFG encodes
Known control state in Block identity instead of inventing a runtime type for
it. This works for computed Known conditions as well as direct variables and
does not require a `bool -> i1` materialization.

Residual values participate in state identity using their SSA identity. Known
values use their payload identity. Semantically identical visible literal
constructions are also recognized by function symbol, result position, type,
and operands rather than transient SSA identity. If runtime state keeps
producing genuinely new Residual values, no finite quotient has been proved;
the normal evaluation limit remains a deterministic diagnostic.

## Nested code without `region`

`region` is not a source-language declaration or a public IR handle. Nested
code is an ordinary function value. Function types use `(inputs) -> results`.
A concrete named Function can already be passed directly:

```joggle
fn map<T, U>(input: tensor<T>, body: (T) -> U) -> tensor<U>;
fn relu_one(input: f32) -> f32;

fn activate(input: tensor<f32>) -> tensor<f32> {
  return map(input, relu_one);
}
```

Callable expectations participate in the same dependent-type solution as the
outer call. Once ordinary operands and expected results determine `(T) -> U`,
that type selects a Function overload or specializes a generic Function. An
underconstrained case uses an ordinary binding annotation rather than a new
function-reference construct.

The planned anonymous form is a block literal:

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

Common control flow receives direct syntax (`if` and `while`).
Extension-defined structured operations use function-typed parameters. Both
elaborate to functions and blocks. A captured closure is normalized to a
generated function whose captures are explicit parameters; it does not create
a second nested-code abstraction. The anonymous closure syntax remains pending
until that module-level lifting path is implemented; it is not parsed as a
temporary Region form.

### Residual branch elaboration

For a Residual condition, elaboration snapshots the lexical environment and
insertion point, evaluates each arm into a sibling Block, and joins each result
position independently:

1. equal path-independent Known values may remain one Known value;
2. a Residual value, or a Known value with a unique visible `literal`
   implementation, is carried on the successor edge and becomes a merge Block
   argument;
3. missing or ambiguous literal implementations are staging diagnostics.

The branch itself is a terminator. It neither contains its arms nor owns a
hidden container. Native bindings are guarded by default: they run only when
the current control path is Known. A binding explicitly promised
`HostEvaluation::Hermetic` may run while a Residual branch is elaborated on
both sides because it is deterministic and has no observable host effect.
This capability belongs to the native implementation rather than the target
operation's semantics, so the DSL needs no effect keyword and the evaluator
never speculatively performs undeclared I/O.

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

Graph algorithms consume the Function directly. Blocks expose successor edges;
Values and Instructions expose definitions and operands. Predecessors, use
lists, dominators, liveness, and traversal orders are computed analysis
products, not public graph containers that can own or mutate the program.

Compiler transformations operate on registered IR handle types. A standard IR
Module may provide `ir.Module`, while an extension may register another handle
type. These are ordinary module-owned types with host representations, not
trusted kernel domains. Loading, analysis, transformation, and emission remain
ordinary registered functions over those types.

The C++ owning API is Function/Block/Instruction/Value. Analysis results may
use graph terminology, but they neither own nor mutate program objects. The
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
fully Known; prefix `@` adds the explicit Known requirement.

Named Function construction accepts its compiler-domain inputs as Known
`Value`s. Those values bind generics and compute concrete residual input and
result types before the Function boundary is created; they do not become
runtime arguments.

Known `if` conditions select one arm without creating IR. A Residual `i1`
condition residualizes each expression arm independently, including nested
calls and nested `if`, then joins them through sibling Blocks and typed
successor arguments. Structured `while` uses the same evaluator: Known loops
execute during specialization, including values later consumed by dependent
types; Residual loops become header/body/exit Blocks with inferred loop-carried
arguments. Configurable expression-step, loop-iteration, and nesting-depth
budgets fail with a diagnostic instead of silently residualizing.

Top-level invocation of functions over registered C++ representations now
executes structured bodies through `Compiler::execute`. The same entry invokes
native bindings and is used by type-level calls, so multi-statement `if` and
`while` computations can select dependent types. Its call path filters visible
overloads by argument placement, expected result, and evaluated host types;
declared operators enter that same path. Residual specialization now plans a
call over the complete visible overload set using the same positional, named,
default, and variadic argument mapping, then combines Known arguments,
Residual argument types, and expected result types before selecting exactly one
Function. Probing competing candidates permits only Hermetic host evaluation,
so a Guarded binding is never executed merely to choose an overload. Once the
declaration is unique, its dependent type computation may use a Guarded binding
only on a Known control path; beneath Residual control it still requires a
Hermetic implementation.
Linking already traverses every structured arm, including arms not selected by
a particular invocation, and rejects unresolved names or call
shapes before host code can run. The remaining consolidation work is a single
structured traversal implementation for host execution and residual
specialization, plus nested Residual call expressions.
Explicit low-level CFG bodies remain residual artifacts rather than compiler
scripts.

Multi-statement expression arms, anonymous closures, allocation budgets, and
host-object serialization remain to be implemented. Named Functions are
already typed callable Values; outer-call constraints select generic and
overloaded references, and canonical source preserves the selected callable
type.
Statement `break` and `continue` are
implemented for Known and Residual loops. Residual transfers inside finite
Known loops preserve multiple specialized continuations. Runtime-dependent
cycles over a repeated staged state close directly into CFG backedges. This
also covers computed Known conditions and compiler-only state without assigning
it a target representation. General quotienting of ever-changing Residual
state remains to be implemented.
Registered host implementations are conservatively guarded beneath Residual
control. An implementation explicitly registered as Hermetic may evaluate
there without weakening the default.
