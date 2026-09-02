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
Instructions. `@(expression)` requires a Known result:

```joggle
tile = @(choose_tile(device, shape));
```

`@` does not change overload resolution, select a second body, or change the
meaning of `=`.

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

## Control flow

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
edges. `@(if ...)` rejects a Residual condition.

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
author writes no phi, `yield`, `iter_args`, or Region signature. `break` and
`continue` are reserved for the same direct-control model but are not yet
implemented.

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

## Closures

Nested code is a function value, written as a closure:

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
nested Blocks, and the language exposes no Region or yield protocol.

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
               | "while" expression "{" { statement } "}"
               | "return" [ expressions ] ";"
               | explicit-block
binding       := identifier [ ":" expression ]

expression    := literal | reference | list | call | operator-expression
               | if-expression | known-expression | closure
known-expression := "@" "(" expression ")"
if-expression := "if" expression "{" expression "}"
                 "else" "{" expression "}"
call          := reference "(" [ call-argument { "," call-argument } ] ")"
call-argument := expression | identifier ":" expression
closure       := "{" [ identifiers ] "=>" { statement } "}"

explicit-block := identifier "(" [ block-parameters ] ")" ":"
                  { statement } terminator
terminator    := "return" [ expressions ] ";"
               | "jump" successor ";"
               | "branch" expression "," successor "," successor ";"
successor     := identifier "(" [ expressions ] ")"
```

The implementation uses this single expression grammar for direct Known `if`,
ordinary straight-line bodies, Residual and nested `if`, structured `while`,
and explicit typed CFG blocks. The public owning IR is
Function/Block/Instruction/Value. Multi-statement branch expressions,
`break`, `continue`, `for`, and closures remain under construction. Nested
Region syntax and storage have been removed.

## Canonical source

For valid source `x`, formatting is idempotent:

```text
format(parse(format(parse(x)))) = format(parse(x))
```

Canonical source is hashed into Module identity. Comments, source paths, host
addresses, and registration order never affect that identity.
