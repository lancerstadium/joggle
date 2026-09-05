# Language

Joggle source declares versioned mods, types, and fns. The language is
small by design: domain libraries add AI operations and hardware vocabulary
without adding declaration categories.

## File and mod

```joggle
joggle 1;

mod example@1.2.0 {
  import tensor@^2.0 as t;
  type word(width: int);
  fn identity<T>(input: T) -> T;
}
```

The file header selects the language version. A file contains one mod.
Imports accept exact (`1.2.3`), major (`1`), minor (`1.2`), and caret
(`^1.2.3`) ranges. An optional `as` name changes only the source prefix.

The only public member forms are:

```text
import  type  fn
```

Legacy declarations such as `interface`, `attr`, and `op` are errors.

## Compiler domains

Declaration parameters and compiler-time fn ports use these domains:

```text
int  real  bool  string  type  bytes  fn  mod  list<D>
```

Fixed-width numeric names such as `i8`, `u32`, `f16`, `bf16`, and `f32` are
ordinary Prelude types. A mod may define additional numeric formats with
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
continue the same explicit SSA chain in materialized Fn IR. A call that
returns an updated token must still rebind or return it before the old token can
be consumed again.

Calls with no effect-typed inputs or results are pure by the language
contract. Residual external interaction must expose its ordering token;
compiler-time host interaction remains behind explicit `@`.

## Fns and overloads

```joggle
fn cast<T, U>(input: T, result: U) -> U;

fn clamp(input: i8, low: i8, high: i8) -> i8 {
  return min(max(input, low), high);
}
```

A semicolon declares an external fn. Braces define a source body.
Fns overload by their full typed signature. Results may be absent, one
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

Fn types and typed lambdas use the same expression language:

```joggle
fn apply<T, U>(input: T, body: (T) -> U) -> U;

fn widened(input: i8) -> i16 {
  return apply(input, (value: i8) => extend(value));
}
```

Lambda parameter annotations are required. The expected fn type checks
the annotations and supplies the result context, so generic higher-order calls
use ordinary overload inference. Parameter annotations participate in overload
selection; the body is checked only after one overload is selected, so calls
whose annotations and surrounding result cannot distinguish two candidates
remain explicitly ambiguous. The body is a normal expression and may call
normal named or symbolic fns. Outer Residual values become explicit capture
edges on the callable `Val` and trailing hidden arguments of the nested `Fn`;
the visible callable Type does not change. Effect tokens cannot be captured
and must remain visible parameters.

## Symbolic fns

Operators are fns whose symbol is their real name:

```joggle
fn (+)(lhs: word<8>, rhs: word<8>) -> word<8>;
fn (-)(value: word<8>) -> word<8>;
fn postfix (!)(value: flag) -> flag;
fn ([])(input: view, position: index) -> element;
```

The parser determines prefix or infix form from arity; `postfix` is explicit.
Symbolic and named fns share overload resolution, reflection, native
binding, serialization, and identity. Operator alias clauses do not exist.

Supported expression symbols currently include unary `+ - !`, arithmetic
`+ - * / // %`, comparisons, boolean `&& ||`, and subscript `[]`. A mod can
overload a supported symbol for its own types. Subscript is only a surface
spelling: `input[position]` resolves and materializes exactly like the ordinary
infix call `[](input, position)`.

Fixity follows the source call shape. A variadic declaration such as
`fn infix ([])(input: tensor<E, S>, indices: index...) -> E` still uses bracket
surface syntax, so `input[i, j]` is one ordinary three-argument Call.

## Statements and control flow

```joggle
fn sum(input: tensor<i32, [4]>) -> i32 {
  total: i32 = 0;
  for i in range(4) {
    total = total + input[i];
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

The marker belongs at the boundary. The body of a fn already invoked through
`@` is compiler computation and does not repeat `@` on its arithmetic, control,
or list subscripts. Thus a residual declaration may use
`@flatten_shape(S, A)`, while `flatten_shape` itself writes `S[i]`, not
`@(S[i])`.

A lambda may state its result when the surrounding call cannot infer it:

```joggle
(input: tensor<f32, [1, 4]>) -> tensor<i8, [1, 4]> => quantize(input)
```

The annotation is checked against a surrounding callable type when both are
present. It is especially useful for explicitly staged transformation
templates whose parameter domain is the general compiler value `fn`.

A contextual lambda may omit parameter Types when its caller determines them.
For example, a Known shape determines the index arity of Tensor `compute`:

```joggle
return compute([M, N], (row, column) => input[column, row]);
```

The inference probe does not emit Calls and never executes a Guarded native
compiler fn. If such a fn produces a value needed by nested Type inference,
evaluate it once in an explicit binding and compose with that value:

```joggle
shape = @query_shape();
return consume(compute(shape, body));
```

A lambda constructs a fn value. Passing it to an ordinary call remains
run-time IR. Passing it to a `fn` parameter through `@` instead constructs
a verified anonymous `Fn` execution value. Compiler fns may return
that value for a later explicit call. Fn values never pass through the
scalar metadata representation or its deterministic-value cache.

## Canonical form and identity

`joggle fmt` emits canonical source. Canonical formatting determines mod
digests, lock identities, and native compatibility. `Mod::digest()` covers
the complete snapshot. `Mod::declaration_digest()` erases fn bodies
and data so declaration provenance remains stable across body materialization.

## Grammar sketch

```text
file       := "joggle" integer ";" mod
mod     := "mod" name "@" version "{" member* "}"
member     := import | type | fn
import     := "import" name "@" range ("as" name)? ";"
type       := "type" name "(" parameters? ")"
              ("{" computed-field* "}" | ";")
fn   := "fn" ("postfix")? (name | "(" symbol ")")
              generics? "(" parameters? ")" results
              (body | ";")
generics   := "<" generic ("," generic)* ">"
generic    := name (":" expression)?
results    := ("->" expression | "->" "(" parameters? ")")?
lambda     := "(" (name ":" expression
              ("," name ":" expression)*)? ")"
              ("->" expression)? "=>" expression
subscript  := expression "[" expression "]"
```

The formatter is the normative spelling for details not captured by this
sketch.
