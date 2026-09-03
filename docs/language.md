# Joggle language

Joggle is a small declaration and function language for compiler extensions
and AI programs. One source file contains one versioned Module. The only member
forms are:

```text
import      another Module
interface   a reusable contract
type        a value type
attr        structured compiler data
fn          anything callable
```

There is no `op`, `pass`, `graph`, or `region` declaration. A function's typed
signature and body determine how it can be evaluated or residualized.

## Module

```joggle
joggle 1;

module model@1.2.0 {
  import tensor@^1.0.0;
  import arithmetic@1.3 as arith;
}
```

Imports accept a major range (`1`), minor range (`1.2`), exact version
(`1.2.3`), or caret range (`^1.2.3`). An alias changes only local spelling.
Linked identity always contains the imported Module's canonical digest.

## Types, attributes, and interfaces

```joggle
type fixed(width: int, fraction: int, signed: bool = true);
type tensor(element: type, shape: list<int>);
attr layout(order: list<int>);

interface scalar: type {
  storage_bits: int;
}

type packed(width: int) : scalar {
  storage_bits = width;
}
```

The small compiler-value vocabulary is:

```text
int  real  bool  string  type  attr  bytes  list<D>
```

These names describe values held by the compiler; they are not target scalar
types. They and `i32`, `f32`, and similar program types have reflected
declarations in the ambient Prelude Module. The trusted kernel supplies the
former group with bootstrap host representations; it does not give them a
second declaration system. Custom formats implement the same interfaces.
Compiler collections use `list<D>`; variadic `T...` is reserved for program
inputs so a native binding always retains a finite, typed compiler signature.

Type constructors are ordinary declarations. Parameters of one type may be
computed from other parameters or interface fields:

```joggle
type word(bits: int);
fn encode<T: scalar>(input: T) -> word<T.storage_bits>;
```

## One function

Every callable member has one form:

```joggle
fn name<generics>(parameters) -> results body
```

An external function ends in `;`:

```joggle
fn add<T: scalar>(lhs: T, rhs: T) -> T as +;
```

A defined function has an ordinary body:

```joggle
fn align(value: int, multiple: int) -> int {
  return ceildiv(value, multiple) * multiple;
}

fn layer(input: tensor<f32, [1, 64]>, bias: tensor<f32, [64]>)
    -> tensor<f32, [1, 64]> {
  shifted = add(input, bias);
  return relu(shifted);
}
```

These are not different function kinds. Availability at a particular call
decides which expressions execute in the compiler and which become residual
Instructions. Prefix `@` requires a Known result:

```joggle
tile = @choose_tile(device, shape);
```

`@` does not change overload resolution, select a second body, or change the
meaning of `=`.

Knownness belongs to a particular invocation, not to a declaration. The same
`fn` may execute completely during compilation for one call and leave a typed
Instruction for another. Evaluation follows one rule:

| Inputs and effects | Result |
| --- | --- |
| all required values are Known and the body/binding is admissible | execute now |
| a required value is Residual | preserve the remaining call or control flow |
| either case under `@` fails to produce a Known value | staging diagnostic |

This is ordinary partial evaluation with a Known-result assertion. It avoids a
second `const fn` namespace and lets Modules extend compile-time computation by
defining or binding the same typed functions used elsewhere. Host bindings run
under the configured determinism and evaluation limits; a non-Hermetic binding
is not speculated below Residual control.

Type annotations use the same expression grammar as signatures and ordinary
function bodies. Computed local types do not fall back to a restricted
constructor-only syntax:

```joggle
fn build() -> word<6> {
  value: word<@sum([1, 2, 3])> = source();
  return value;
}
```

The same rule applies to explicitly written Block argument types. Every such
annotation must evaluate to one Known `type`; a Residual result is a staging
error rather than a request to create a second type language.

## Known values entering the program

A Known compiler value sometimes has to become a Residual program value, for
example when different integer literals leave a dynamic branch. The Prelude
defines the marker interface `literal`; a visible Module supplies ordinary
functions implementing it:

```joggle
fn integer_literal<T: prelude.integer>(value: int) -> T
    : prelude.literal;
```

The compiler selects exactly one visible `literal` function whose compiler
input accepts the Known payload and whose result resolves to the required
concrete program type. It emits an ordinary call to that function before the
dynamic edge. There is no built-in constant instruction or materializer
callback. Missing and ambiguous matches are diagnostics. Visibility is the
current Module plus its direct imports, so adding an unrelated installed
Module cannot change a program.

## Bindings and calls

`=` introduces or updates a source binding. The compiler may rename bindings
to SSA Values internally; authors do not write percent-prefixed names.

```joggle
sum = lhs + rhs;
output = relu(sum);
return output;
```

Named call arguments use `:` so they cannot be confused with bindings or
parameter defaults:

```joggle
output = conv2d(
  input,
  weight,
  stride_h: 2,
  stride_w: 2
);
```

An optional binding annotation constrains an otherwise ambiguous result:

```joggle
value: word<8> = source();
```

Calls, prefix operators, infix operators, and postfix operators all resolve
through the same visible `fn` overload set. `as` associates a spelling with an
ordinary Function; there is no operator-specific evaluator.

Operator signatures are fully typed rather than restricted to closed
arithmetic. For example, `fn less(lhs: int, rhs: int) -> bool as <;` may be
bound by an extension and used directly as a compile-time branch condition.
Symbolic spellings such as `<`, `>`, `<=`, `>=`, `==`, `!=`, `&&`, and `||`
use the same precedence parser. A `<...>` immediately following a declaration
name is a type or generic argument list; angle brackets between expressions
are operators. An angle-bracket comparison used inside a generic argument is
parenthesized, for example `flag<(lanes >= 4)>`; this keeps closing `>`
unambiguous without lexer modes. Adjacent nested closings such as
`tensor<word<8>>` remain ordinary delimiters.

## Control flow

A Function owns Blocks, Instructions, and Values. Structured source syntax is
the authoring view; a control-flow graph is the relation among those Blocks,
and a data-flow graph is the def-use relation among the same Values. They are
analysis views of one Function, not additional owning objects. Consequently a
neural network is still graph-shaped without requiring a `graph` declaration
or a conversion between Graph IR and control-flow IR.

`if` is an expression:

```joggle
fn choose<T: type>(condition: i1, lhs: T, rhs: T) -> T {
  value = if condition {
    lhs
  } else {
    rhs
  };
  return value;
}
```

With a Known condition, the compiler executes only the selected branch. With a
Residual condition, the same expression becomes Blocks and typed successor
edges. Applying `@` to the whole `if` rejects a Residual condition.

`if` is also an ordinary statement when several computations or outer
rebindings are needed:

```joggle
value = input;
if condition {
  value = fast(value);
} else {
  value = safe(value);
}
return value;
```

Names first introduced inside an arm remain local. Existing names rebound by
an arm are converted to SSA internally: Known control keeps the selected value;
Residual control carries the value from each arm into a merge Block argument.
An omitted `else` carries the incoming value along the false edge. Authors do
not declare merge variables or write `yield`.

`return` may appear directly inside a structured arm or loop body:

```joggle
if ready(input) {
  return fast(input);
}
return safe(input);
```

A returned arm terminates only that path. If the other arm falls through, it
continues directly without a synthetic merge Block. If both arms return, no
continuation Block or trailing dummy `return` is required. A Known condition
instantiates only its selected return path. The parser rejects both incomplete
Function paths and statements written after an unconditional control transfer.

Structured loops use direct syntax:

```joggle
current = start;
while less(current, limit) {
  current = next(current);
}
```

A Known condition executes the loop in the compiler within its deterministic
evaluation budget. A Residual `i1` condition becomes header, body, and exit
Blocks. Rebound values used after the loop become typed Block arguments; the
author writes no phi, `yield`, `iter_args`, or Region signature. `continue`
passes the current inferred loop values to the header; `break` passes them to
the exit. Under Known control they execute in the compiler and create no IR.

Most authors never write Blocks explicitly. The low-level form exists for
lossless formatting of arbitrary pass output:

```joggle
fn choose<T: type>(condition: i1, lhs: T, rhs: T) -> T {
  entry():
    branch condition, yes(), no();

  yes():
    jump merge(lhs);

  no():
    jump merge(rhs);

  merge(value: T):
    return value;
}
```

An explicit Block header is `name(arguments...):`. Every Block ends with
`return`, `jump`, or `branch`. Successor arguments must match the target Block
arguments exactly.

## Function values

A concrete, non-overloaded Function name is an ordinary typed value:

```joggle
fn map<T, U>(input: tensor<T>, body: (T) -> U) -> tensor<U>;
fn relu_one(input: f32) -> f32;

fn activate(input: tensor<f32>) -> tensor<f32> {
  return map(input, relu_one);
}
```

The value has reflected type `(f32) -> f32` and refers directly to the
Function symbol. No wrapper call or constant Instruction is inserted.
An expected callable type selects overloaded Functions and specializes generic
Functions. It can come from an explicit local annotation or be propagated from
the surrounding higher-order call after its ordinary arguments and expected
results constrain the signature:

```joggle
fn identity<T: type>(input: T) -> T;
fn apply<T: type>(input: T, body: (T) -> T) -> T;

fn use(input: f32) -> f32 {
  return apply(input, identity);
}
```

If the surrounding call leaves the callable signature genuinely
underconstrained, the author supplies the same ordinary type annotation used
for every other value; there is no function-value-specific syntax.

`(T, U) -> (V, W)` is a real type expression. It resolves to the reflected
Prelude `callable` type whose `inputs` and `results` are lists of ordinary
types, so generic inference can inspect and unify a callable signature. It is
not the bare `function` compiler handle used by whole-Function tools.

## Closure direction

The intended surface for anonymous nested code is a closure expression:

```joggle
fn map<T, U>(input: tensor<T>, body: (T) -> U) -> tensor<U>;

fn activate(input: tensor<f32>) -> tensor<f32> {
  return map(input, { item =>
    relu(item)
  });
}
```

A closure captures lexical values. Normalization creates an ordinary private
Function and makes captures explicit parameters. Instructions do not own
nested Blocks, and the language exposes no Region or yield protocol. Closure
literals are not accepted yet: module-level ownership and capture lifting must
land before this syntax becomes part of the implemented grammar.

## Compact grammar

```text
file          := "joggle" integer ";" module
module        := "module" identifier "@" exact-version
                 "{" { member } "}"
member        := import | interface | type | attr | fn

fn            := "fn" identifier [ generics ] parameters
                 [ "->" results ] [ "as" operator ] ( ";" | body )
body          := "{" { statement } "}"
statement     := binding "=" expression ";"
               | expression ";"
               | "if" expression "{" { statement } "}"
                 [ "else" "{" { statement } "}" ]
               | "while" expression "{" { statement } "}"
               | "return" [ expressions ] ";"
               | "break" ";"
               | "continue" ";"
               | explicit-block
binding       := identifier [ ":" expression ]

expression    := literal | reference | list | call | operator-expression
               | if-expression | known-expression
known-expression := "@" "(" expression ")"
if-expression := "if" expression "{" expression "}"
                 "else" "{" expression "}"
call          := reference "(" [ call-argument { "," call-argument } ] ")"
call-argument := expression | identifier ":" expression

explicit-block := identifier "(" [ block-parameters ] ")" ":"
                  { statement } terminator
terminator    := "return" [ expressions ] ";"
               | "jump" successor ";"
               | "branch" expression "," successor "," successor ";"
successor     := identifier "(" [ expressions ] ")"
```

The implementation uses this single expression grammar for local and Block
argument type annotations, direct Known `if`, ordinary straight-line bodies,
Residual and nested `if`, structured `while`, multi-statement `if`, structured
early returns, function types, and explicit typed CFG blocks. The public owning
IR is Function/Block/Instruction/Value. Multi-statement *expression* arms,
`for`, and closure literals remain under construction. Nested Region syntax
and storage have been removed.

A Residual decision may control `break` or `continue` inside a finite Known
loop. Joggle retains separate specialized continuations, so path-specific Known
state need not acquire an arbitrary program type. A runtime-dependent cycle
over a repeated staged state becomes a CFG backedge to the first Block for that
state. This supports computed Known conditions and compiler-only carried state
without materializing them as target values. States containing continually new
Residual values remain bounded by the deterministic evaluation limit; the
compiler does not guess an equivalence relation.

## Canonical source

For valid source `x`, formatting is idempotent:

```text
format(parse(format(parse(x)))) = format(parse(x))
```

Canonical source is hashed into Module identity. Comments, source paths, host
addresses, and registration order never affect that identity.
