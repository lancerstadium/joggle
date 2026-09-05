# IR

Joggle uses `Mod` for whole-program state and `Fn` for executable IR.
There is no separate graph IR: dataflow is the def-use graph of calls and
values inside a fn, while blocks and terminators provide control flow.

## Vals

Every `Val` has a `Type`. A value is a fn input, block argument, call
result, known compiler value, or callable literal. Known scalar payloads are
immutable compiler-domain values: integers, reals, booleans, strings, types,
bytes, and their supported homogeneous lists.

A callable value has the ordinary `prelude.callable<inputs, results>` type.
It may be a Fn argument, block argument, call result, declared-fn reference,
or anonymous `Fn`. A declared reference stores the compile-time arguments that
specialized it; an anonymous body stores verified IR rather than parser syntax.
Neither representation creates a synthetic Mod member.

An anonymous callable closes over Residual values through explicit capture
edges. The captured values become trailing hidden arguments of its nested
`Fn`; they are not part of the visible callable Type or ordinary Call argument
list. Capture edges participate in dominance, liveness, dependency collection,
formatting, and identity. Effect values cannot be captured implicitly.

Metadata is represented by a normal `Type` instance. This means a layout,
numeric format, or policy uses the same construction, named-field access,
identity, serialization, and list behavior as every other type.

## Operations

There is one operation: `Call(callee, arguments)`. `callee` is a `Val` with a
concrete callable type, so a named call, an operator, `body(input)`, and a call
through a returned fn share the same IR shape. Runtime arguments are ordinary
typed value edges. Compile-time arguments such as a convolution stride or
numeric format are bindings of the declared-fn reference, not a second
attribute/property channel on Call.

The C++ `edit.call(declaration, arguments)` overload is construction sugar: it
resolves compile-time arguments, creates the specialized callable value, and
then creates the same Call. `op.callee()` always returns that value;
`referenced_fn()` and `bindings()` inspect a named specialization when one is
present.

An Op may carry an optional `Loc`. Parsers and import Mods use it
for diagnostics and source provenance; it is not a semantic attribute, tensor
property, or part of canonical Mod identity. Public Fn edits may attach a
range, and cloning or direct inlining preserves it.

## Blks and control flow

A `Fn` contains ordered blocks. Blks own arguments and operations and
end in a return or branch terminator. CFG structure is explicit in C++, while
the source language offers structured `if` and `for` syntax. The parser and
materializer lower structured syntax into this single representation.

## Transactional editing

`Fn::edit()` creates an isolated edit. `call`, `call_before`, `replace`, and
`erase` modify that private revision; `commit()` verifies and publishes it
atomically. Typed source lambdas and named references use the same callable
`Val` representation as C++ edits. `Val::captures()` exposes closure edges;
`inline_fn()` exposes the nested body with its visible arguments followed by
the hidden capture arguments.

## Verification

Verification checks:

- value, block, and declaration ownership;
- callee dominance and callable-type agreement;
- call argument and result type agreement;
- dominance and use-before-definition;
- block argument and branch agreement;
- one valid terminator per block;
- fn result agreement;
- declaration provenance across mod snapshots.
- callable type/body agreement, specialization bindings, and the inline
  body's Mod closure;
- affine use of `effect<domain>` values, including exclusive branch transfer.

## Serialization

`format(mod)` emits canonical Joggle source. Materialized fns are
serialized in the same mod, and immutable binary payloads are referenced by
content digest. Re-parsing preserves the canonical mod digest.
