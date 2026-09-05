# Design 0003: Fn transformation

Status: accepted; implementation in progress

## Decision

A transformation receives an ordinary `fn` or `mod` compiler value and returns
a new value. It edits the concrete calls, nested callable bodies, blocks, and
values of the same `Fn`; it does not describe a separate before/after graph.

```joggle
fn optimize(input: fn) -> fn {
  input = @inline(input);
  input = @fuse(input);
  input = @loops(input);
  return @dce(input);
}
```

The names are library API, not keywords or compiler hooks. `@` is the only
staging marker. C++ implementations use the transactional `Fn::Edit` surface;
source implementations will use the same `fn`, `blk`, `op`, and `val` domains
once reflection is exposed.

## One body, not fusion op substitution

High-level compute fns have real source bodies. Expanding

```joggle
y = relu(conv(x, w));
```

reveals tensor construction, indexing, reduction, and scalar expressions in
the caller. Fusion composes the producer and consumer bodies and removes the
intermediate tensor in that same caller. It does not create or call a
`conv_relu` fn, and it does not match either operator name.

Named hardware or library calls belong only to later implementation selection.
They are not evidence that high-level fusion occurred.

## Structured calls

Tensor construction, reduction, and loops remain ordinary Calls whose callable
arguments are typed lambdas. A lambda is an anonymous `Fn`; captures are
explicit edges of its callable `Val` and hidden trailing arguments of its body.
This retains enough structure for transformation without adding public Loop,
Region, Graph, TensorOp, Pattern, or Rewrite classes.

Structured control stays in this form until an explicit CFG transformation.
The lower CFG still uses the existing `Blk` and `Term` representation, so this
is a change in Fn structure rather than a second IR.

## Generic legality

Transformations depend on semantic structure, Types, def-use edges, and effect
tokens:

- producer-consumer fusion uses iteration domains and access expressions;
- loop transforms use bounds, steps, carried values, and nested bodies;
- storage transforms use value lifetimes and explicit memory effects;
- opaque calls stop transformations that require unavailable semantics.

No operation-name switch is accepted for Conv, Relu, GEMM, quantized variants,
or imported framework operators. A new high-level operator becomes optimizable
by defining it in terms of the common tensor calculus, not by adding a compiler
case.

Effects remain ordinary affine `effect<domain>` values. A transformation may
move or replace stateful computation only when it preserves the visible token
boundary. An effect token cannot be hidden in a closure capture.

## Function expansion and implementation closure

Inlining and source resolution are distinct:

- `inline` clones a selected source body into its caller so local structural
  transformations can see and change it;
- `resolve` materializes a closed call graph without invoking opaque leaves.

Inlining is selective and reversible through ordinary function factoring.
Resolution is a packaging boundary. Neither operation executes Residual calls
through host callbacks.

## Later capability selection

After tensor and loop transformations, an optional selection pass may cover a
concrete body region with a function supplied by a capability Mod. A bodyful
candidate provides its semantics once; the compiler derives the candidate
shape from that body. A genuinely opaque instruction needs one semantic
contract and one emitter binding on the same declaration identity, never a
second matcher fn.

Selection is deliberately later than fusion and scheduling. It must not turn
the compiler back into a catalog of `before`/`after` pairs.

## Rejected designs

- `replace(input, before, after)` as the primary pass abstraction;
- a `rewrite` declaration or pattern-specific syntax;
- matching by operator name or string;
- one fused fn for every producer-consumer combination;
- lowering structured loops to CFG before tensor and schedule transforms;
- public cursor, anchor, behavior, interface, region, or pattern objects;
- treating a renamed composite call as executable fusion.

## Implementation gates

1. [complete] One `Call(callee: Val, arguments...)` operation.
2. [complete] Typed anonymous fns and explicit closure-capture edges.
3. [complete] Remove expression-template replacement from the C++ API,
   transform Mod, tests, fixtures, and public narrative.
4. [complete] Implement transactional, name-independent single-block Fn
   inlining with callable and capture remapping.
5. [planned] Extend inlining across explicit CFG while preserving successor
   arguments and effects.
6. [planned] Retain structured `for` as a higher-order Call until explicit CFG
   conversion.
7. [in progress] Give the tensor Mod a small bodyful
   iteration/access/reduction
   calculus.
8. [in progress] Expand high-level tensor fns into that calculus. Generic map
   and Relu are the first implemented definitions.
9. [planned] Implement dependence-checked producer-consumer fusion inside one
   Fn.
10. [planned] Expose compact source-level Fn inspection and functional editing.
11. [planned] Validate generic fusion on imported, unmodified ONNX models.
