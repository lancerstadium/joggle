# Design 0004: Tensor semantics

Status: accepted; implementation pending

## Purpose

Joggle needs a target-independent tensor calculus in which high-level neural
network fns have inspectable bodies. Tensor semantics remain a normal
installable Mod: the core gains no tensor declaration kind, operation
registry, graph container, or lowering interface.

An imported Conv or Relu call is not sufficient semantics. Its source fn must
either expand into the common calculus or remain visibly opaque. Only the
first case is eligible for generic fusion and loop transformation.

## Type boundary

```joggle
type tensor(element: type, shape: list<int>);
```

`element` is an ordinary Type. `shape` is a semantic extent, not a physical
layout. The first executable slice may use static extents, but symbolic
extents must later be expressible through ordinary types and fns rather than a
second shape AST in compiler core.

Physical layout, packing, address space, and storage allocation are not tensor
semantics. Later format or storage mods may introduce their own Types and
compiler fns without changing `tensor` or compiler core.

## Structural basis

The basis contains only concepts that expose computation:

- tensor construction from an extent and a typed element lambda;
- element access by a typed index;
- reduction over an extent with an initial value and typed update lambda;
- views whose index mapping is itself inspectable;
- structured iteration for effectful or explicitly materialized storage.

These are ordinary fns and Calls. The exact source names are not language
keywords and will be accepted only with the first executable implementation.
A schematic Relu body is therefore:

```joggle
fn relu<E, S>(input: tensor<E, S>) -> tensor<E, S> {
  return generate<S>((index: index<S>) =>
    max(at(input, index), zero<E>()));
}
```

`generate`, `at`, `max`, and `zero` are overloadable library fns. The lambda
is a real nested `Fn`. Conv and GEMM use the same construction and reduction
basis; they do not require privileged operator nodes. A user-defined tensor fn
becomes transformable by providing such a body, not by registering its name in
each pass.

The example is a design sketch until the required generics and index Types are
implemented. It does not reserve special syntax or claim a working API.

## Transformation consequence

Inlining a high-level fn exposes its construction, accesses, reductions, and
scalar expressions in the caller. Producer-consumer fusion substitutes the
producer element body into consumer accesses when dependence and effects
permit, then removes the intermediate construction. Loop transforms edit the
nested iteration `Fn`. Neither transform manufactures `conv_relu`, dispatches
on a Conv name, or uses a second pattern IR.

## ONNX boundary

The ONNX mod parses protobuf through a staged importer and maps standard
operators to ordinary Joggle declarations. Known operators select definitions
from a separately versioned semantic Mod. Unknown operators remain typed,
opaque leaves with their original domain, name, and attributes; import must not
fail merely because an optimization definition is absent.

Initializers remain Mod-owned bytes. Imported shapes, attributes, graph
inputs/outputs, and source names must be preserved. Model support is claimed
only after differential validation against a trusted runtime.

## Implementation gates

- [x] Typed lambdas are real nested Fns.
- [x] Residual captures are explicit closure edges.
- [ ] Define the minimal extent, index, tensor, and view Types.
- [ ] Implement bodyful construction, access, and reduction fns.
- [ ] Express elementwise, GEMM, and convolution fns using only that basis.
- [ ] Inline those bodies into a caller without name-specific compiler code.
- [ ] Implement dependence-checked producer-consumer fusion.
- [ ] Import an unmodified ONNX model using semantic definitions plus opaque
  fallback leaves.
- [ ] Differentially validate baseline and transformed outputs.
