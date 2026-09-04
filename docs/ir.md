# IR model

Joggle uses `Module` for whole-program state and `Function` for executable IR.
There is no separate graph IR: dataflow is the def-use graph of calls and
values inside a function, while blocks and terminators provide control flow.

## Values

Every `Value` has a `Type`. A value is a function input, block argument, call
result, known compiler value, or callable literal. Known scalar payloads are
immutable compiler-domain values: integers, reals, booleans, strings, types,
bytes, and their supported homogeneous lists.

A callable literal has the ordinary `prelude.callable<inputs, results>` type
and stores either an exact declared-function reference or an anonymous
`Function`. The anonymous body is verified IR, not retained parser syntax. It
does not become a synthetic module member or participate in overload lookup;
it is part of the owning function's structure, dependencies, formatting, and
digest.

Metadata is represented by a normal `Type` instance. This means a layout,
numeric format, or policy uses the same construction, named-field access,
identity, serialization, and list behavior as every other type.

## Operations

An `Op` is a call to an exact `Module::FunctionDecl`. Inputs and results are
checked against the selected overload. Operator syntax in source resolves to
the same function identity as named calls.

## Blocks and control flow

A `Function` contains ordered blocks. Blocks own arguments and operations and
end in a return or branch terminator. CFG structure is explicit in C++, while
the source language offers structured `if` and `for` syntax. The parser and
materializer lower structured syntax into this single representation.

## Transactional editing

`Function::edit()` creates an isolated edit. Operations can be appended,
inserted, replaced, or erased. `commit()` verifies the candidate and publishes
it atomically. Typed source lambdas materialize through the same function body
engine and produce the same callable `Value` representation used by the C++
edit API.

## Verification

Verification checks:

- value, block, and declaration ownership;
- call arity and type agreement;
- dominance and use-before-definition;
- block argument and branch agreement;
- one valid terminator per block;
- function result agreement;
- declaration provenance across module snapshots.
- callable type/body agreement and the inline body's module closure.
- affine use of `effect<domain>` values, including exclusive branch transfer.

## Serialization

`format(module)` emits canonical Joggle source. Materialized functions are
serialized in the same module, and immutable binary payloads are referenced by
content digest. Re-parsing preserves the canonical module digest.
