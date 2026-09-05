# Tensor

`tensor@1.1.0` is a target-independent, bodyful AI vocabulary. It is an
ordinary source Mod and adds no tensor node, graph owner, operation registry,
or lowering interface to compiler core.

## Types

```joggle
type tensor(element: type, shape: list<int>);
type coord(shape: list<int>);
```

The shape is logical extent. It does not encode physical layout, packing,
address space, allocation, or device placement. `coord<S>` is an index in that
same logical space; targets may later choose how either Type is represented.

The current verifier accepts static non-negative dimensions, including an
empty shape for a scalar tensor. Symbolic extents remain future work.

## Structural basis

The first basis has two opaque leaves:

```joggle
fn generate<E, S: list<int>>(
  body: (coord<S>) -> E
) -> tensor<E, S>;

fn at<E, S: list<int>>(
  input: tensor<E, S>,
  position: coord<S>
) -> E;
```

`generate` constructs each result element by calling its typed body. `at`
reads one logical element. They are ordinary fns rather than privileged IR
operations. A machine or lower-level library eventually implements this small
basis; it does not implement every neural-network operation separately.

Higher-level fns have inspectable source bodies. For example, `map` expands to
one `generate` whose nested Fn captures the input tensor and user callable:

```joggle
fn map<E, S: list<int>, R>(
  input: tensor<E, S>,
  body: (E) -> R
) -> tensor<R, S> {
  return generate(
    (position: coord<S>) => body(at(input, position))
  );
}
```

Relu uses the same basis:

```joggle
fn relu<E, S: list<int>>(
  input: tensor<E, S>
) -> tensor<E, S> {
  return generate(
    (position: coord<S>) => relu_value(at(input, position))
  );
}
```

Materializing these calls yields normal `Fn`, `Op`, and `Val` objects. The
element body is a nested `Fn`; its input-tensor dependency is an explicit
closure edge. A transformation can enter, inline, clone, or edit that body
without matching the name `relu`.

## Remaining operators

Conv, pooling, concatenation, reshape, and softmax are still typed opaque
declarations retained for the existing ONNX importer. They are not considered
complete tensor semantics. Each must either receive a definition over the
construction/access/reduction basis or remain an explicit unsupported leaf.

Initializers remain immutable Mod data: `Mod::store` returns a content digest,
and `constant<T>(content: string)` records that digest instead of embedding
large bytes in textual IR.

## Current evidence and limits

Tests now prove that:

- Relu specializes and materializes to `generate`, `at`, and scalar calls;
- its element Fn explicitly captures the input tensor;
- `map` invokes an arbitrary typed callable from its nested element Fn;
- all resulting Fns pass the normal verifier;
- tensor programs retain canonical source round trips.

This is the first bodyful vertical slice, not a fusion or performance claim.
Reduction, Conv/GEMM definitions, dependence analysis, generic fusion,
symbolic shapes, physical formats, storage planning, and emission remain
unfinished.
