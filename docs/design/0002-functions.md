# Design 0002: Function values

Status: accepted

## Problem

Joggle first supported function types only through values that reference a
declared function:

```joggle
fn apply<T, U>(input: T, body: (T) -> U) -> U;
fn convert(input: i8) -> i16;
fn use(input: i8) -> i16 {
  return apply(input, convert);
}
```

Adding inline function expressions must not create a hidden module declaration
for every lambda: that would make formatting, declaration digests, overload
reflection, and native compatibility depend on synthesized names. Carrying
untyped syntax as metadata would postpone errors and create a second
expression system. Both approaches are rejected.

## Accepted surface

A capture-free typed lambda is an expression:

```joggle
(x: i8) => extend(x)
(x: tensor, w: tensor) => relu(conv2d(x, w))
```

Parameter annotations are mandatory. The result type is inferred from the
body and checked against context when one exists. A result annotation may be
added only if inference later proves insufficient; it is not part of the
first implementation.

The body uses the normal expression grammar and normal overload resolution.
There is no pattern-expression parser, Lambda declaration, Region wrapper, or
builder-only syntax.

The first implementation is deliberately capture-free. A reference in the
body resolves to a lambda parameter or module declaration; literals remain
normal Known expressions. Referring to any outer local is a diagnostic.
Capture semantics require a separate design decision because they affect ABI,
serialization, and transformation equality.

## One callable type

Both declared references and inline functions have the existing type:

```joggle
prelude.callable<[input types], [result types]>
```

Function type syntax remains its readable source form:

```joggle
(i8, i8) -> i16
```

No public `Lambda`, `Closure`, `Pattern`, or `Callable` class is added. C++
continues to inspect an inline callable as `Function`.

## IR representation

A callable `Value` has one of two definitions:

1. a reference to an exact `Module::FunctionDecl`;
2. an immutable inline `Function` body.

This is a property of `Value` storage, not a new IR owner. The inline body is
anonymous and therefore does not appear in `Module::members()`, overload
lookup, or the declaration digest. It contributes structurally to the owning
function and therefore to the complete module digest.

The public query becomes:

```cpp
std::optional<Module::FunctionDecl> Value::referenced_function() const;
std::optional<Function> Value::inline_function() const;
```

Exactly one is present for a callable literal. Existing callers that only care
about declared identity continue to use `referenced_function()`.

## Construction and verification

The parser records a lambda with parameter names, their normal type
expressions, and one normal body expression. Parameter types participate in
overload filtering; the body is checked after one overload is selected rather
than speculatively executed for every candidate. During materialization:

1. parameter type expressions are resolved in the surrounding module scope;
2. a fresh anonymous `Function` is created in the same compiler closure;
3. its arguments are bound to the lambda parameter names;
4. the body expression is materialized by the same expression machinery used
   for named functions;
5. its result type is inferred and the body is verified;
6. the owning function receives an inline callable `Value` with the derived
   `prelude.callable` type.

The implementation must refactor the named-function materializer around a
shared concrete signature/body engine. It must not copy call-resolution logic
into a lambda-only compiler.

Verification checks that:

- the callable type exactly matches the inline function inputs and results;
- the inline function uses only modules in the owner's closure;
- its values and blocks belong to the inline function;
- it contains no outer residual value;
- recursively nested inline functions are acyclic by ownership;
- declared references still match their exact declarations.

## Formatting and identity

A single-expression inline function formats as the source lambda syntax. The
formatter derives parameter and result types from the verified `Function`; it
does not preserve an untyped syntax blob.

Canonical module identity includes callable type, parameter order, operations,
control flow, known constants, and nested callable values. Parameter spelling
is not semantic identity. Canonical formatting assigns deterministic local
names, as it already does for materialized functions.

## Staging

A lambda expression constructs a callable value; it does not execute it.
Passing it to an ordinary higher-order call leaves that call Residual.

Passing a lambda to a compiler function whose parameter has the `function`
domain constructs a verified anonymous `Function`. Explicit `@` invocation
passes that object directly as an execution value; it is never serialized
through scalar `ParameterValue`. A compiler function may return the same
`Function`, which can be bound and passed to a later `@` call. Function-valued
calls are not entered in the scalar hermetic-evaluation cache.

This direct path is required for the later transformation API:

```joggle
@transform.replace(
  model,
  (x: value) => step(step(x)),
  (x: value) => twice(x),
)
```

The call remains one ordinary overload. `replace` is not a declaration form.

## Implementation gates

1. [complete] Add inline-function storage to callable `Value` and exact
   verification.
2. [complete] Refactor function materialization around a concrete anonymous
   signature.
3. [complete] Parse, validate, and canonically format capture-free typed
   lambdas.
4. [complete] Materialize lambdas in ordinary higher-order calls.
5. [complete] Extend explicit `@` invocation to pass typed `Function` values
   directly.
6. [complete] Add source round-trip, ownership, ambiguity, cloning,
   nested-callable, and negative-capture coverage.

Each gate must keep all existing tests passing. No rewrite API is introduced
until callable values complete this sequence.
