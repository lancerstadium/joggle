# Tensor

`tensor@1.0.0` is Joggle's first target-independent AI vocabulary. It is an
ordinary source Mod installed beside the language tools; it adds no tensor
kind, graph owner, operation registry, or lowering interface to compiler core.

## Semantic type

```joggle
import tensor@1 as t;

fn classify(input: t.tensor<f32, [1, 3, 224, 224]>)
    -> t.tensor<f32, [1, 1000]>;
```

The shape is logical axis extent. The current tested contract accepts static
non-negative dimensions, including an empty list for a rank-zero tensor. It
does not encode physical layout, packing, address space, allocation, or a
device. Those are separate Types introduced by later user Mods when an
optimization needs them.

The element is an ordinary Type. Joggle has no privileged numeric trait or
closed format enumeration; an importing mod decides which element Types it
accepts through normal verifiers.

## Calls and properties

Tensor operations are ordinary `fn` declarations. Residual tensor arguments
become SSA operands; compiler-domain arguments in the same signature become
immutable typed properties:

```joggle
convolved: t.tensor<f32, [1, 16, 55, 55]> = t.conv(
  input,
  weight,
  bias,
  [1, 1],
  [0, 0, 0, 0],
  [1, 1],
  1
);
```

There is no separate attribute schema. `strides`, `pads`, `dilations`, and
`group` are parameters of the exact selected `conv` overload and survive
canonical Fn formatting.

Model inputs are Fn parameters. Large initializers remain immutable
Mod data: `Mod::store` returns a content digest, and
`constant<T>(content: string)` records that digest rather than embedding bytes
in textual IR.

## Structural transformations and kernels

The semantic Mod intentionally declares neither fused kernels nor a kernel
class. `transform.replace` accepts typed lambdas and transactionally changes an
actual Fn body. Replacing an expression with a source fn whose body
expands to the same expression is valid fn factoring, but it is not by
itself kernel fusion and carries no performance claim.

A future user kernel must therefore provide more than a renamed reference
expression: it needs an executable ordinary fn body (or calls to
source-grounded implementation fns), a checked semantic relation, and
measured evidence. Until that representation exists, `tensor` remains the
portable program vocabulary and no parallel family of format-aware tensor
fns is accepted.

## Evidence boundary

Implemented and tested:

- ordinary installable tensor Mod;
- static-shape validation through the general type-verifier API;
- typed Conv, Relu, and Concat materialization for a Fire block;
- preservation of convolution and concatenation properties;
- typed-lambda structural replacement, shared-DAG preservation, rollback, and
  canonical Fn round-trip.

The separate `onnx` Mod now imports the exact pinned SqueezeNet 1.1 model,
preserves its initializers and supported properties, and has exact differential
ONNX Runtime evidence. This does not broaden `tensor` itself into an ONNX
schema.

Not yet claimed:

- general ONNX operator-set coverage or dynamic shapes;
- executable kernel fusion, physical layout, packed formats, or storage
  planning;
- executable hardware implementation and performance.

The semantic transformation boundary is defined by
[Design 0007](../design/0007-equivalence.md).
