# Language reference

Joggle source declares versioned modules, types, and functions. The language is
small by design: domain libraries add AI operations and hardware vocabulary
without adding declaration categories.

## File and module

```joggle
joggle 1;

module example@1.2.0 {
  import tensor@^1.1 as t;
  type word(width: int);
  fn identity<T>(input: T) -> T;
}
```

The file header selects the language version. A file contains one module.
Imports accept exact (`1.2.3`), major (`1`), minor (`1.2`), and caret
(`^1.2.3`) ranges. An optional `as` name changes only the source prefix.

The only public member forms are:

```text
import  type  fn
```

Legacy declarations such as `interface`, `attr`, and `op` are errors.

## Compiler domains

Declaration parameters and compiler-time function ports use these domains:

```text
int  real  bool  string  type  bytes  function  module  list<D>
```

Fixed-width numeric names such as `i8`, `u32`, `f16`, `bf16`, and `f32` are
ordinary Prelude types. A module may define additional numeric formats with
the same `type` mechanism.

## Types

```joggle
type integer(width: int, signed: bool = true) {
  storage_bits: int = width;
}

type tensor(element: type, shape: list<int>);
type layout(order: list<int>);
```

Constructor parameters are immutable and may have literal defaults. Computed
fields have an explicit domain and an expression. Type construction uses angle
brackets in type positions and compiler expressions:

```joggle
tensor<integer<8, false>, [1, 16, 16, 32]>
layout<[0, 2, 3, 1]>
```

There is no separate metadata value category. A layout, schedule policy,
format, or machine description is a normal type instance.

## Effects

Residual side effects are ordered by ordinary SSA values:

```joggle
type memory();
fn store(token: effect<memory>, address: index, value: i8)
    -> effect<memory>;
```

`effect<domain>` is a normal parameterized Prelude type. It is never a Known
value. One token may have at most one consuming use on an executed path. A
branch may pass the same token to both mutually exclusive successors, while
each successor receives its own block argument; later control-flow merges use
another typed block argument. Repeating a token within one edge or feeding two
calls on the same path is rejected transactionally.

Structured Residual control carries every visible effect token automatically.
`if` arms, `while` bodies, and typed `for` bodies therefore receive fresh block
arguments without extra source syntax; merges, loop headers, latches, and exits
continue the same explicit SSA chain in materialized Function IR. A call that
returns an updated token must still rebind or return it before the old token can
be consumed again.

Calls with no effect-typed inputs or results are pure by the language
contract. Residual external interaction must expose its ordering token;
compiler-time host interaction remains behind explicit `@`.

## Functions and overloads

```joggle
fn cast<T, U>(input: T, result: U) -> U;

fn clamp(input: i8, low: i8, high: i8) -> i8 {
  return min(max(input, low), high);
}
```

A semicolon declares an external function. Braces define a source body.
Functions overload by their full typed signature. Results may be absent, one
value, or a parenthesized list.

Generic parameters default to the `type` domain:

```joggle
fn identity<T>(input: T) -> T;
fn vector<W: int>(input: tensor<i8, [W]>) -> tensor<i8, [W]>;
```

Named and positional call arguments may not be mixed ambiguously:

```joggle
cast(input = value, result = i16)
```

Function types and capture-free lambdas use the same expression language:

```joggle
fn apply<T, U>(input: T, body: (T) -> U) -> U;

fn widened(input: i8) -> i16 {
  return apply(input, (value: i8) => extend(value));
}
```

Lambda parameter annotations are required. The expected function type checks
the annotations and supplies the result context, so generic higher-order calls
use ordinary overload inference. Parameter annotations participate in overload
selection; the body is checked only after one overload is selected, so calls
whose annotations and surrounding result cannot distinguish two candidates
remain explicitly ambiguous. The body is a normal expression and may call
normal named or symbolic functions. Capturing an outer run-time local is
currently rejected; it will not silently create an environment object or a
hidden module declaration.

## Symbolic functions

Operators are functions whose symbol is their real name:

```joggle
fn (+)(lhs: word<8>, rhs: word<8>) -> word<8>;
fn (-)(value: word<8>) -> word<8>;
fn postfix (!)(value: flag) -> flag;
```

The parser determines prefix or infix form from arity; `postfix` is explicit.
Symbolic and named functions share overload resolution, reflection, native
binding, serialization, and identity. Operator alias clauses do not exist.

Supported expression symbols currently include unary `+ - !`, arithmetic
`+ - * / // %`, comparisons, and boolean `&& ||`. A module can overload a
supported symbol for its own types.

## Statements and control flow

```joggle
fn sum(input: tensor<i32, [4]>) -> i32 {
  total: i32 = 0;
  for i in range(4) {
    total = total + at(input, i);
  }
  if total < 0 {
    return -total;
  } else {
    return total;
  }
}
```

Bindings may carry a type annotation. `if`, `for`, calls, and `return` use the
same expression grammar as declaration expressions; there is no region or
graph sublanguage.

## Staging

```joggle
model = @read(input);
optimized = @optimize(model, policy);
output = run_time_op(value);
```

`@` requests compiler-time execution. An ordinary call denotes residual
program computation even when all of its operands are Known. A call that
produces a compiler-domain result without `@` is rejected instead of being
silently executed.

A lambda constructs a function value. Passing it to an ordinary call remains
run-time IR. Passing it to a `function` parameter through `@` instead constructs
a verified anonymous `Function` execution value. Compiler functions may return
that value for a later explicit call. Function values never pass through the
scalar metadata representation or its deterministic-value cache.

## Canonical form and identity

`joggle fmt` emits canonical source. Canonical formatting determines module
digests, lock identities, and native compatibility. `Module::digest()` covers
the complete snapshot. `Module::declaration_digest()` erases function bodies
and data so declaration provenance remains stable across body materialization.

## Grammar sketch

```text
file       := "joggle" integer ";" module
module     := "module" name "@" version "{" member* "}"
member     := import | type | function
import     := "import" name "@" range ("as" name)? ";"
type       := "type" name "(" parameters? ")"
              ("{" computed-field* "}" | ";")
function   := "fn" ("postfix")? (name | "(" symbol ")")
              generics? "(" parameters? ")" results
              (body | ";")
generics   := "<" generic ("," generic)* ">"
generic    := name (":" expression)?
results    := ("->" expression | "->" "(" parameters? ")")?
lambda     := "(" (name ":" expression
              ("," name ":" expression)*)? ")" "=>" expression
```

The formatter is the normative spelling for details not captured by this
sketch.
