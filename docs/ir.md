# IR

Joggle uses `Mod` for whole-program state and `Fn` for executable IR.
There is no separate graph IR: dataflow is the def-use graph of calls and
values inside a fn, while blocks and terminators provide control flow.

## Vals

Every `Val` has a `Type`. A value is a fn input, block argument, call
result, known compiler value, or callable literal. Known scalar payloads are
immutable compiler-domain values: integers, reals, booleans, strings, types,
bytes, and their supported homogeneous lists.

A callable literal has the ordinary `prelude.callable<inputs, results>` type
and stores either an exact declared-fn reference or an anonymous
`Fn`. The anonymous body is verified IR, not retained parser syntax. It
does not become a synthetic mod member or participate in overload lookup;
it is part of the owning fn's structure, dependencies, formatting, and
digest.

Metadata is represented by a normal `Type` instance. This means a layout,
numeric format, or policy uses the same construction, named-field access,
identity, serialization, and list behavior as every other type.

## Operations

An `Op` is a call to an exact `Mod::FnDecl`. Inputs and results are
checked against the selected overload. Operator syntax in source resolves to
the same fn identity as named calls.

An Op may carry an optional `SourceRange`. Parsers and import Mods use it
for diagnostics and source provenance; it is not a semantic attribute, tensor
property, or part of canonical Mod identity. Public Fn edits may
attach a range, clone preserves it, and expression replacement transfers the
matched root range to new calls.

## Blks and control flow

A `Fn` contains ordered blocks. Blks own arguments and operations and
end in a return or branch terminator. CFG structure is explicit in C++, while
the source language offers structured `if` and `for` syntax. The parser and
materializer lower structured syntax into this single representation.

## Transactional editing

`Fn::edit()` creates an isolated edit. Operations can be appended,
inserted, replaced, or erased. `commit()` verifies the candidate and publishes
it atomically. Typed source lambdas materialize through the same fn body
engine and produce the same callable `Val` representation used by the C++
edit API.

## Verification

Verification checks:

- value, block, and declaration ownership;
- call arity and type agreement;
- dominance and use-before-definition;
- block argument and branch agreement;
- one valid terminator per block;
- fn result agreement;
- declaration provenance across mod snapshots.
- callable type/body agreement and the inline body's mod closure.
- affine use of `effect<domain>` values, including exclusive branch transfer.

## Serialization

`format(mod)` emits canonical Joggle source. Materialized fns are
serialized in the same mod, and immutable binary payloads are referenced by
content digest. Re-parsing preserves the canonical mod digest.
