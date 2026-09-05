# Architecture

Joggle is a small compiler substrate for extensible neural-network and edge
hardware research. The architecture is organized around one package object,
one executable IR object, and one extension mechanism.

## Public object model

| Object | Meaning |
| --- | --- |
| `Mod` | versioned package, namespace, declarations, bodies, and immutable data |
| `Type` | immutable instance of a declared type constructor |
| `Fn` | typed CFG/SSA program |
| `Blk` | basic block in a `Fn` |
| `Op` | one ordinary call |
| `Val` | typed argument, known value, callable, or call result |
| `Compiler` | linking, inference, staging, native bindings, and verification |
| `Diag` | structured diagnostics |

An end-to-end model is a `Fn`; its SSA use-def relation is the computation
graph. A fused computation is a `Fn`. A loop program is also a `Fn`. The
compiler does not convert among `Graph`, `Fusion`, `Kernel`, or `Program`
classes because those classes do not exist.

Compilation states are predicates over ordinary `Fn` contents:

```text
semantic Fn  -- tensor.fuse -->  fused tensor Fn
fused tensor Fn -- tensor.loops --> loop Fn
loop Fn -- future target fn --> target Fn or bytes
```

The arrows are explicitly staged ordinary fns. No pass executes because of its
name, and no call to one pass silently schedules the next.

## Source model

A source file contains one versioned mod. Only `import`, `type`, and `fn` are
module members.

```joggle
joggle 1;

mod sample@1.0.0 {
  pub type packed(bits: int, lanes: int);
  pub fn convert<T, B: int, L: int>(input: T) -> packed<B, L>;
}
```

Declarations are private by default. `pub` defines the importable package
surface; it does not introduce another declaration kind or IR object. Private
helpers and public declarations use the same type checker, bodies, overload
rules, and C++ reflection handles.

The same `fn` form describes residual computation and compiler-time tools.
An ordinary call remains an `Op`; `@call(...)` requests compile-time execution.
This is the only staging distinction.

## Calls, operators, and bodies

Every `Op` calls a typed `Val`. The callee can reference a mod declaration or
hold an anonymous `Fn`. Operators such as `+`, `*`, and `[]` are normal
overloaded fns with surface syntax. There is no operation subclass, attribute
dictionary, interface registry, or behavior object.

A bodyful semantic fn is the implementation and optimization contract. A new
NN operator becomes automatically inspectable when its body is expressed with
the small tensor and scalar basis. A bodyless fn is an explicit opaque
boundary; importing its name does not imply executable support.

## Package boundary

A `Mod` is created only for independently named, versioned, distributable
vocabulary. The shipped ownership is:

| Mod | Owns |
| --- | --- |
| `arith` | scalar operations |
| `tensor` | tensor value semantics and tensor-specific passes |
| `nn` | frontend-independent NN algorithms |
| `transform` | domain-independent Fn transformations |
| `quant` | quantization semantics |
| `onnx` | ONNX byte decoding and schema-to-fn resolution |

Dependencies follow semantics rather than workflow: `tensor` imports `arith`
for scalar loop construction, `nn` imports `arith` and `tensor`, and `quant`
imports `tensor`. Optional `onnx` has no fixed semantic dependency; its reader
resolves against the packages deliberately linked by the caller. No package
reaches into another package's C++ implementation.

There is no current `mem`, `graph`, `kernel`, `device`, or `target` mod. Storage
is not yet a portable package boundary: the logical loop form uses tensor value
updates, while future target packages decide physical reuse and writes.

## Tensor optimization boundary

Tensor operations are pure unless their signatures contain an explicit
`effect<domain>` value. The normal NN path contains no effects. This allows the
compiler to derive maximal pure dataflow from existing types instead of asking
users to mark graph blocks or register purity traits.

`tensor.fuse` works on one straight-line, single-result region. It repeatedly
expands bodyful calls, then composes tensor demand from the returned value back
to input tensors. `tensor.tensor`, `tensor.map`, `tensor.reduce`, and `tensor.[]`
are the only semantic basis it understands. NN names never appear in the pass.

The first implementation deliberately refuses shared tensor producers. It does
not duplicate their computation. A later planner will use use-def,
post-dominance, symbolic traffic, recomputation, and code-size costs to choose
fusion groups. Refusal is preferable to a hidden performance regression.

`tensor.loops` accepts only the result of the fused tensor form. It expands
output coordinates and reductions into CFG loops. The output starts as
`tensor.empty` and each `tensor.set` produces the next logical tensor value.
These are value operations, not allocation and mutation promises. A later
target pass may safely reuse physical storage after conflict analysis.

See [Tensor pipeline](pipeline.md) for the exact contracts and example.

## Core and native code

The C++ core owns only mechanisms shared by every domain:

- parsing and canonical formatting;
- versioned symbol identity and linking;
- type construction and overload inference;
- `Fn` construction, use-def, dominance, CFG, and transactional edits;
- explicit Known/Residual staging;
- native compiler-fn binding and execution;
- verification and diagnostics.

A native package library implements only bodyless compiler-time services that
need host code. Its `.joggle` source remains the ABI authority, and the build
embeds the declaration identity into the library. No generated C++ declaration
header is needed.

## Source tree

| Directory | Responsibility |
| --- | --- |
| `src/base` | diagnostics and content digests |
| `src/lang` | lexer, parser, formatter, Prelude |
| `src/ir` | `Mod`, `Type`, `Fn`, `Blk`, `Op`, `Val` storage |
| `src/sema` | domains, type inference, calls, validation |
| `src/compile` | linking, staging, bindings, execution |
| `src/transform` | generic cloning, inlining, rewriting, resolution |
| `src/pkg` | installation and lock-file repository |
| `mods/<name>` | one shipped package and optional native implementation |

These folders are implementation ownership, not public namespaces or IR
levels. Joggle builds one core library.

## Invariants

Every published transformation must satisfy all of the following:

1. It returns a new valid `Fn` or leaves its input unchanged on failure.
2. All calls retain exact declaration identity and concrete types.
3. No compiler-time operation is triggered without explicit `@` staging.
4. A pass does not introduce a new public owner or side channel.
5. The result is printable, inspectable, and independently verifiable.
6. Unsupported semantics fail with a diagnostic instead of matching an
   operator name or pretending a body exists.
