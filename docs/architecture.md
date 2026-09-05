# Architecture

Joggle is a C++ compiler substrate with one source owner and one extension
mechanism. The owner is `Mod`; the extension mechanism is ordinary typed
`fn` declarations. Tensor operators, importers, transformations, measurements,
and output writers are library vocabulary rather than core subclasses.

## Core objects

| Object | Responsibility |
| --- | --- |
| `Mod` | versioned declarations, imports, fn bodies, immutable data |
| `Type` | immutable instance of a `type` declaration |
| `Fn` | editable typed CFG/SSA body |
| `Expr` | immutable declaration-language expression inside a `Mod` |
| `Blk` | basic block inside a `Fn` |
| `Op` | one call in a `Fn` |
| `Val` | typed fn argument, result, or known compiler value |
| `Compiler` | dependency linking, overload resolution, staging, bindings |

These six high-frequency IR atoms use one compact vocabulary everywhere:
`Mod`, `Fn`, `Val`, `Expr`, `Blk`, and `Op`. Their public accessors use the
same stems (`mod`, `fn`, `blk`, and their plurals). Descriptive roles such as
`TypeDecl`, `Term`, and `Loc` keep full words. Diagnostics use
`Diag` for the collector and `Issue` for one reported item; Joggle does not
abbreviate every identifier indiscriminately.

There is no second Program, Graph, Package, Attribute, Pass, Target, or Result
owner. A model graph is a `Mod` containing fns. Metadata is a normal
`Type`. A transformation is a fn from one compiler value to another.

## Source layout

Implementation files are grouped by ownership rather than compilation phase
names embedded in filenames:

| Directory | Owns |
| --- | --- |
| `src/base` | diagnostics and content digests |
| `src/lang` | lexing, source syntax, formatting, and the Prelude |
| `src/ir` | `Type`, `Mod`, `Fn`, `Blk`, `Op`, and `Val` storage |
| `src/sema` | domains, call resolution, inference, and validation |
| `src/compile` | linking, staging, native binding, and evaluation |
| `src/transform` | rewriting, equivalence, and call-graph resolution |
| `src/pkg` | installed-mod repository operations |

These directories are not public namespaces, libraries, dialects, or runtime
components. Joggle still builds one compiler library. Their purpose is to let
short filenames such as `call.cpp`, `infer.cpp`, and `eval.cpp` communicate a
single responsibility without suffixes such as `_internal` or `_contract`.
In particular, `lang/mod.cpp` owns the textual Mod grammar, `lang/print.cpp`
owns canonical printing, and `ir/mod.cpp` owns the in-memory Mod object and
its identity semantics.
Likewise, `lang/fn.cpp` owns fn parsing and printing, `lang/check.cpp` checks
source-body structure, and `compile/body.cpp` specializes a valid source body
into typed `Fn` IR. Within the compiler owner, `compile/compiler.cpp`
coordinates materialization and type construction, `compile/bind.cpp` owns
host representations and callable bindings, `compile/link.cpp` seals the Mod
closure, `compile/native.cpp` owns host-library loading, and `compile/run.cpp`
invokes compiler fns.

## Mod snapshots

Parsing creates a `Mod`. Materialization attaches editable `Fn`
bodies to declarations in a new `Mod` snapshot. Committed edits use
copy-on-write storage, so earlier snapshots remain valid.

`Mod::digest()` identifies the complete canonical snapshot, including
materialized bodies and stored data. `Mod::declaration_digest()` identifies
the imports and declarations with bodies erased. Symbols retain the latter as
provenance so a declaration from another compiler snapshot cannot be used by
accident.

## Declarations

The public declaration forms are `import`, `type`, and `fn` inside a mod.
`type` covers both run-time value types and compile-time descriptions:

```joggle
type word(width: int) {
  storage_bits: int = width;
}

type layout(order: list<int>);
fn pack<E, S: list<int>, O: list<int>>(
  input: tensor<E, S>,
  order: layout<O>
) -> tensor<E, S>;
```

There is no separate attribute or capability declaration. Generic parameters
bind directly to types or compiler domains, and a concrete type's computed
fields are checked when referenced.

## Calls and staging

Ordinary calls are program calls. `@call(...)` requests compiler-time
execution. Both resolve the same overload and use the same declaration
identity; staging does not create a second kind of fn.

The explicit-`@` rule is enforced during materialization. A compiler-domain
call without `@` is diagnosed, and an ordinary program call remains in IR even
when its inputs are Known.

Native C++ bodies implement explicitly staged host services. Their signatures
are checked against the source declaration when bound. They are not the
execution semantics of ordinary Residual calls. The source mod remains
authoritative; no generated declaration header is required.

## Editable fns

A `Fn` owns blocks, block arguments, calls, returns, and typed values.
Edits are transactional: call, call_before, replace, erase, then commit. A failed
commit does not publish a partially invalid body. The verifier checks ownership,
dominance, terminators, call signatures, result types, and cross-mod symbol
provenance.

The edit API is the low-level substrate. A typed source lambda becomes an
anonymous `Fn` held by a callable `Val`; it uses the same calls, types,
verification, cloning, and formatting as a named body. Residual captures are
explicit edges of that `Val` and trailing hidden arguments of the nested body.
It is not a mod declaration and does not introduce an alternate graph or
pattern IR.

Explicit compiler-time calls pass typed lambdas as verified `Fn`
execution values and may return them for later `@` calls. This path shares
compiler-call shaping, overload filtering, default handling, and execution
with source-defined compiler fns; it does not encode fns as scalar
metadata. Typed expression matching and bounded source-body equivalence reuse
these same verified Fns and create no normalization IR. `transform.pass`
interprets two concrete typed lambdas as an equation over the existing Fn:
arguments are pattern variables, calls match declaration identity and
dataflow, and the replacement is verified before publication.

Residual effects use the ordinary `effect<domain>` Prelude type. Tokens flow
through calls and CFG edges as normal SSA values, and the verifier prevents
more than one consumption on an executed path. Exclusive branch successors
may each receive the incoming token and merge through a typed block argument.
Structured Residual control inserts those block arguments for every visible
effect token, including loop headers, bodies, latches, and exits. There is no
purity registry or effect annotation attached to `fn`.

## Extension boundary

An extension normally contains:

1. one `.joggle` mod declaring its types and fns;
2. optional source bodies for portable behavior;
3. an optional native library for host-only parsing, analysis, or file output.

Composition is explicit in source:

```joggle
fn prepare(input: bytes, policy: type) -> mod {
  model = @read(input);
  return @optimize(model, policy);
}
```

Names such as `read` and `optimize` are mod APIs, not magic hooks.
The core imposes no lowering direction or fixed hardware hierarchy.

## Implementation closure

`transform.resolve` constructs a concrete call graph by instantiating reachable
source bodies. Bodyless calls remain visible leaves for a later whole-Mod
compiler fn. Resolution never invokes a leaf binding and never claims that a
leaf is supported by a machine.

An emitter consumes the complete resolved Mod once. Importers, transforms,
emitters, and measurement tools may use a small number of native bindings at
their host boundaries; model and kernel Ops do not. See
[Design 0009](design/0009-implementation.md).

## Near-term implementation order

The implemented path includes the core language, editable Fn IR, explicit
staging, typed anonymous Fns, explicit closure captures, recursive
single-block inlining, and pure concrete typed-equation passes. `tensor@2`
defines only `build`, `at`, `fold`, and
bodyful `map`. ONNX owns its operator vocabulary; its Relu definition expands
first to `map` and then to `build` while remapping nested closures. The pinned
SqueezeNet test exercises this path for all 26 imported Relu calls. Concrete
tests compose `map(build(f), g)` into one producer lambda and cancel
`at(build(f), p)` inside an existing nested body.

Execution semantics for the structural basis, Type-polymorphic equations,
multi-block inlining, and dependence-checked reduction transforms remain
unfinished.
ONNX Conv, pooling, concatenation, reshape, softmax, and quantized operations
are still opaque declarations rather than completed computational definitions.

Only after the complete bodyful path works on an unmodified model do kernel
scheduling, physical layouts, packed formats, storage planning, capability
selection, and machine emission enter scope. The compiler must not simulate
progress by matching operation names, interpreting every Op through a host
callback, or renaming a reference expression as a fused operation.
