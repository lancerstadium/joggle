# Language reference

One `.joggle` file declares one versioned Module. The language has five member
forms: `import`, `interface`, `type`, `attr`, and `fn`. Only `fn` is callable.

## Files and Modules

```joggle
joggle 1;

module schedule@1.2.0 {
  import tensor@^1.0.0;
  import target@1.3 as hw;
}
```

The first integer is the source-language version. A Module version has three
components. Imports accept `1`, `1.2`, `1.2.3`, or a caret range such as
`^1.2.3`. An alias changes local spelling only; linked identity uses the real
Module name, version, and canonical digest.

Unqualified declaration names resolve in the current Module. A direct import
is addressed through its Module name or alias. Operator notation considers
operator functions from the current Module and direct imports because the
symbol has no qualifier. The ambient Prelude supplies compiler-domain names,
functions, and operators without an import. A local function name hides the
same Prelude name; a local or imported operator with the same signature hides
that Prelude operator. `prelude.name` explicitly selects the ambient
declaration. Unrelated installed Modules cannot alter overload resolution.

## Compiler domains and module types

The compiler domains are:

```text
int  real  bool  string  type  attr  bytes  function  list<D>
```

They describe values held by the compiler. `function` carries one executable
IR Function. Prelude additionally declares `module`, whose host representation
is the same `joggle::Module` returned by parsing and used as the pass boundary;
it is not a second container.
`i1`, `i8`, `i16`, `i32`, `i64`,
`u8`, `u16`, `u32`, `u64`, `f16`, `bf16`, `f32`, `f64`, and `index` are
module types declared by the Prelude. Custom module types use the same
declaration mechanism:

```joggle
type fixed(width: int, fraction: int, signed: bool = true);
type tensor(element: type, shape: list<int>);
attr layout(order: list<int>);
```

Type and attribute arguments are ordered and typed. Defaults may omit a suffix
of arguments. A type may define derived fields:

```joggle
interface scalar: type {
  storage_bits: int;
}

type packed(width: int, lanes: int) : scalar, numeric {
  storage_bits = width * lanes;
}
```

An interface on `type` declares compile-time fields. Interfaces on `attr` or
`fn` declare methods or callable contracts. Conformance is declared after `:`;
the linker checks required fields and signatures.

Prelude's `scalar` and `numeric` contracts are deliberately independent.
`scalar` exposes a fixed `storage_bits`; `numeric` permits use by generic
numeric vocabularies such as `arith`. Thus `i1` is scalar but not numeric,
while target-sized `index` is numeric but has no fixed-width promise.

## Functions

All callables use one declaration:

```joggle
fn name<generics>(inputs) -> results;
```

Examples:

```joggle
fn add<T: prelude.scalar>(lhs: T, rhs: T) -> T as +;
fn volume(shape: list<int>) -> int;

fn align(value: int, multiple: int) -> int {
  return ceildiv(value, multiple) * multiple;
}

fn classify(value: int) -> (int, bool);
fn checked(value: int) -> (int, bool) {
  magnitude, valid = classify(value);
  return magnitude, valid;
}
```

A trailing `;` declares behavior supplied elsewhere or a function call that
may remain Residual. Braces define a body. `as` associates prefix, infix, or
postfix notation with the same function and overload set. Interfaces follow
`:`.
Zero results use no arrow, one result uses `-> T`, and multiple positional
results use `-> (T, U, ...)`. A call binds exactly its declared result count;
there is no synthetic unit or `Result` value.

Generic declarations have a name and either a compiler domain or a type
interface constraint:

```joggle
fn make<N: int>(width: N) -> word<N>;
fn encode<T: numeric_format>(input: T) -> word<T.storage_bits>;
```

`width: N` means that this Known input binds generic `N`; its concrete domain
is `int`. Module inputs such as `input: T` infer a type generic from the
operand. Generics participate in dependent result types and overload solving.
A generic bound by a Known input is also a normal Known local inside a defined
body, so `N` or `S: list<int>` may drive expressions and control flow.

Independent Module checking never guesses a value for a required Known
generic. It validates the generic body's syntax, scopes, declarations, and
call shapes; each concrete specialization then validates the selected staged
paths and resolved types. Defaults make a specialization available to package
checking without a caller.

Compiler lists are `list<D>`. Variadic `T...` is reserved for module-value
inputs; compiler callbacks always have a finite C++ signature.

## Prelude functions and operators

Compiler-domain arithmetic is not a second expression evaluator. The Prelude
declares ordinary external functions and associates them with familiar
operators:

```joggle
fn add(lhs: int, rhs: int) -> int as +;
fn less(lhs: int, rhs: int) -> bool as <;
fn logical_and(lhs: bool, rhs: bool) -> bool as &&;
fn ceildiv(lhs: int, rhs: int) -> int;
fn range(stop: int) -> list<int>;
```

The compiler provides deterministic Hermetic implementations for the exact
Prelude declarations. They enter normal visibility, overload selection,
signature checking, Known evaluation, and diagnostics. There is no fallback
that interprets an undeclared symbol or recognizes a call merely by its text.

The shipped surface covers unary `+` and `-`; integer and real `+`, `-`, `*`,
`/`, and `//`; integer `%`; numeric comparisons; equality for `int`, `real`,
`bool`, and `string`; boolean `!`, `&&`, and `||`; `ceildiv`, `min`, and `max`;
and integer `range`. Integer arithmetic is checked for signed 64-bit overflow.
Division by zero and non-finite real results are errors. `ceildiv` accepts a
non-negative dividend and a positive divisor.

Prelude operators are defaults, not privileged syntax. A Module can replace
one signature with its own ordinary function:

```joggle
fn difference_as_add(lhs: int, rhs: int) -> int as + {
  return lhs - rhs;
}
```

Named local functions hide the whole ambient Prelude name, while explicit
qualification remains available. Operator operands are eagerly evaluated;
`&&` and `||` do not define a separate short-circuit control form.

## Expressions and bindings

`=` introduces a local name or rebinds an existing name:

```joggle
sum = lhs + rhs;
output: tensor<f32, [1, 64]> = relu(sum);
return output;
```

Authors write ordinary names. Residual locals are converted to SSA Values and
Block arguments internally. A type annotation constrains an ambiguous result
and uses the same expression grammar as signatures.

Calls support positional arguments followed by named arguments. Named call
arguments use `:`:

```joggle
output = conv2d(input, weight, stride_h: 2, stride_w: 2);
```

Literals, lists, calls, member fields, function types, prefix/infix/postfix
operators, and conditional expressions share one expression grammar. Operator
precedence is syntactic; meaning comes from a visible `fn ... as symbol`
declaration. Parenthesize comparisons inside generic arguments, for example
`flag<(lanes >= 4)>`, to distinguish an operator from closing `>` tokens.

`@expression` requires a Known result:

```joggle
tile = @choose_tile(shape, budget);
value: word<@(align(width, 8))> = source();
```

`@` does not change `=`, select a special function, or enter another language.
It turns failure to evaluate completely into a staging diagnostic.

## Known and Residual values

Type and availability are independent. At a particular invocation a value is
either Known to the compiler or Residual in the generated Function.

- A call with admissible Known inputs executes its source body or C++ binding.
- A call requiring a Residual input becomes an Instruction.
- A computation under `@` must finish Known.

The same declared function can therefore execute now in one invocation and
remain in the module in another. Host execution is bounded by the Compiler's
step and depth limits. A binding marked non-Hermetic is not speculated below
Residual control.

When a Known compiler payload must enter the module, the compiler selects one
visible ordinary function implementing `prelude.literal` for the required
module type. There is no built-in constant Instruction or materializer hook.

## Conditional control flow

An `if` expression produces one value:

```joggle
result = if condition { lhs } else { rhs };
```

An `if` statement may contain a sequence and rebind outer names:

```joggle
result = input;
if condition {
  result = fast(result);
} else {
  result = safe(result);
}
return result;
```

A Known `bool` selects one arm during compilation. A Residual `i1` creates
Blocks and typed successor edges. Rebound outer values become merge Block
arguments when needed. Names first introduced in an arm remain local. An arm
may `return`; only surviving paths continue.

## Loops

`while` supports Known and Residual conditions:

```joggle
current = start;
while less(current, limit) {
  current = next(current);
}
```

A Known `bool` executes iterations in the compiler. A Residual `i1` creates a
header, body, and exit in the Function. Rebound outer values become inferred
loop-carried Block arguments. `continue` transfers current values to the
header; `break` transfers them to the exit.

`for` is the finite compile-time iteration form:

```joggle
current = input;
for enabled in Stages {
  if enabled {
    current = transform(current);
  }
}
```

The iterable must evaluate to a Known homogeneous `list<D>`. The iterator is a
new Known local scoped to one iteration. Iterations expand in list order and
may generate Residual Instructions or branches. `continue`, `break`, and
`return` have their usual nearest-loop meaning. The evaluation budget bounds
the expansion. A runtime collection is not silently unrolled; use `while` and
Residual function calls to express a runtime loop.

Integer generics use the same list rule through ordinary Prelude functions:

```joggle
fn repeat<N: int>(count: N, input: word<8>) -> word<8> {
  current = input;
  for index in range(N) {
    current = identity(current);
  }
  return current;
}
```

`range(stop)`, `range(start, stop)`, and `range(start, stop, step)` produce
half-open integer lists. The default start is zero, the default step is one,
negative steps count downward, and a zero step is an error. Range size is
bounded by the same evaluation step limit as staged control flow.

This makes a binding such as `S: list<int>` useful in all three places:

```joggle
fn specialize<S: list<int>>(shape: S, input: tensor<f32, S>)
    -> tensor<f32, S> {
  for dimension in S {
    validate_dimension(dimension);
  }
  return input;
}
```

The same Known `S` determines the signature, drives control, and supplies
ordinary function arguments.

## Explicit Blocks

Structured syntax is preferred for source, but arbitrary transformed IR can be
written without a Region representation:

```joggle
fn choose(condition: i1, lhs: i32, rhs: i32) -> i32 {
  entry():
    branch condition, yes(), no();

  yes():
    jump merge(lhs);

  no():
    jump merge(rhs);

  merge(value: i32):
    return value;
}
```

Every explicit Block ends in `return`, `jump`, or `branch`. Successor values
must match target Block arguments exactly. A body uses either structured form
or explicit Blocks, not a nested mixture.

## Function values

A concrete Function name can be an ordinary callable value:

```joggle
fn map<T: type, U: type>(input: tensor<T>, body: (T) -> U) -> tensor<U>;
fn relu_one(input: f32) -> f32;

fn activate(input: tensor<f32>) -> tensor<f32> {
  return map(input, relu_one);
}
```

`(T) -> U` is a reflected callable type. Context can select an overload or
specialize a generic function value. The IR records the referenced Function
symbol directly; no wrapper Instruction or Region is introduced. Anonymous
closure literals and capture lifting are not implemented.

## Compact grammar

```text
file       := "joggle" integer ";" module
module     := "module" name "@" version "{" { member } "}"
member     := import | interface | type | attr | fn

fn         := "fn" name [ generics ] parameters [ "->" results ]
              [ "as" [ fixity ] operator ] [ interfaces ] ( ";" | body )
fixity     := "prefix" | "infix" | "postfix"
body       := "{" { statement } "}"
statement  := [ bindings "=" ] expression ";"
            | "if" expression body [ "else" body ]
            | "while" expression body
            | "for" name "in" expression body
            | "return" [ expressions ] ";"
            | "break" ";" | "continue" ";"

expression := literal | reference | list | call | operator-expression
            | if-expression | "@" expression | function-type

block      := name "(" [ block-arguments ] ")" ":"
              { statement } terminator
terminator := "return" [ expressions ] ";"
            | "jump" successor ";"
            | "branch" expression "," successor "," successor ";"
```

## Canonical source

Formatting is idempotent:

```text
format(parse(format(parse(source)))) = format(parse(source))
```

Canonical source determines Module identity. Comments, file paths, host
addresses, and behavior registration order do not affect the digest.
