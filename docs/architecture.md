# Architecture

Joggle is a C++ compiler substrate with one source owner and one extension
mechanism. The owner is `Module`; the extension mechanism is ordinary typed
`fn` declarations. Tensor operators, importers, transformations, cost models,
and emitters are library vocabulary rather than core subclasses.

## Core objects

| Object | Responsibility |
| --- | --- |
| `Module` | versioned declarations, imports, function bodies, immutable data |
| `Type` | immutable instance of a `type` declaration |
| `Function` | editable typed CFG/SSA body |
| `Op` | one call in a `Function` |
| `Value` | typed function argument, result, or known compiler value |
| `Compiler` | dependency linking, overload resolution, staging, bindings |

There is no second Program, Graph, Package, Attribute, Pass, Target, or Result
owner. A model graph is a `Module` containing functions. Metadata is a normal
`Type`. A transformation is a function from one compiler value to another.

## Source and IR are one module

Parsing creates a `Module`. Materialization attaches editable `Function`
bodies to declarations in a new `Module` snapshot. Committed edits use
copy-on-write storage, so earlier snapshots remain valid.

`Module::digest()` identifies the complete canonical snapshot, including
materialized bodies and stored data. `Module::declaration_digest()` identifies
the imports and declarations with bodies erased. Symbols retain the latter as
provenance so a declaration from another compiler snapshot cannot be used by
accident.

## One declaration plane

The public declaration forms are `import`, `type`, and `fn` inside a module.
`type` covers both run-time value types and compile-time descriptions:

```joggle
type word(width: int) {
  storage_bits: int = width;
}

type layout(order: list<int>);
fn pack(input: tensor, order: layout<[0, 2, 1]>) -> tensor;
```

There is no separate attribute or capability declaration. Generic parameters
bind directly to types or compiler domains, and a concrete type's computed
fields are checked when referenced.

## One call plane

Ordinary calls are program calls. `@call(...)` requests compiler-time
execution. Both resolve the same overload and use the same declaration
identity; staging does not create a second kind of function.

The explicit-`@` rule is enforced during materialization. A compiler-domain
call without `@` is diagnosed, and an ordinary program call remains in IR even
when its inputs are Known.

Native C++ bodies are optional implementations of bodyless functions. Their
signatures are checked against the source declaration when bound. The source
module remains authoritative; no generated declaration header is required.

## Editable functions

A `Function` owns blocks, block arguments, calls, returns, and typed values.
Edits are transactional: append, insert, replace, erase, then commit. A failed
commit does not publish a partially invalid body. The verifier checks ownership,
dominance, terminators, call signatures, result types, and cross-module symbol
provenance.

The edit API is the low-level substrate. A typed source lambda becomes an
anonymous `Function` held by a callable `Value`; it uses the same calls, types,
verification, cloning, and formatting as a named body. It is not a module
declaration and does not introduce an alternate graph or pattern IR.

The remaining higher-order gate is explicit compiler-time invocation of such
function values. Effect-safe replacement follows only after that value path is
complete.

## Extension boundary

An extension normally contains:

1. one `.joggle` module declaring its types and functions;
2. optional source bodies for portable behavior;
3. an optional native library for host-only parsing, analysis, or emission.

Composition is explicit in source:

```joggle
fn compile(input: bytes, machine: type) -> bytes {
  model = @read(input);
  optimized = @optimize(model, machine);
  return @emit(optimized, machine);
}
```

Names such as `read`, `optimize`, and `emit` are module APIs, not magic hooks.
The core imposes no lowering direction or fixed hardware hierarchy.

## Near-term implementation order

The binding plan is RFC 0001: declaration unification, explicit staging,
higher-order typed functions, effect-safe replacement, then real tensor and
ONNX modules. Kernel scheduling and hardware-specific formats remain outside
the core until that end-to-end path works.
