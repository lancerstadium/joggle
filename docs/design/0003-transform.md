# Design 0003: Fn transformation

Status: accepted; implementation in progress

## Decision

A transformation receives an ordinary `fn` or `mod` value and returns a new
value. It edits Calls, values, blocks, and nested callable bodies in the same Fn
representation. `pass`, `inline`, and `resolve` are library fns invoked through
the single `@` stage marker.

An equation is an ordinary pure fn returning two values of the same Type: the
left expression and its replacement. Matching uses declaration identity,
Types, Known bindings, def-use structure, and effects. There is no Pattern AST,
rewrite keyword, anchor, behavior class, or operation-name dispatch.

## NN consequence

Format conversion and optimization follow the same mechanism. ONNX-to-NN
equations match source-schema declarations and construct frontend-neutral `nn`
calls. Later NN expansion exposes Tensor `compute/map/reduce/[]` bodies. Fusion
then composes those bodies; it never substitutes a `conv_relu` name or adds a
case for Conv.

Callable parameters are nested Fns with explicit capture edges. A
transformation entering a callable therefore sees ordinary typed dataflow.
Effect tokens remain affine values and cannot be hidden in a capture.

## Implemented

- transactional single-block Fn inlining;
- recursive traversal of existing callable bodies;
- pure generic equation application with dead-producer cleanup;
- generic inference from result Types and straight-line left-hand dataflow;
- effect rejection and exact declaration/binding matching;
- whole-Mod source resolution without executing opaque leaves.

## Pending

- CFG-aware inlining and equations;
- access/dependence summaries derived from Tensor bodies;
- legality-aware `compute/map/reduce` fusion;
- frontend conversion equations with attribute normalization;
- capability selection after semantic optimization.
