# Tensor module

`tensor@1.0.0` is Joggle's first target-independent AI vocabulary. It is an
ordinary source Module installed beside the language tools; it adds no tensor
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
device. Those are separate Types introduced by later user Modules when an
optimization needs them.

The element is an ordinary Type. Joggle has no privileged numeric trait or
closed format enumeration; an importing module decides which element Types it
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
canonical Function formatting.

Model inputs are Function parameters. Large initializers remain immutable
Module data: `Module::store` returns a content digest, and
`constant<T>(content: string)` records that digest rather than embedding bytes
in textual IR.

## User kernels and fusion

The semantic module intentionally does not declare fused kernels. A user
extension can declare its own call and replace a typed expression with it:

```joggle
fn conv_relu<X, W, B, Y>(
  input: X,
  weight: W,
  bias: B,
  strides: list<int>,
  pads: list<int>,
  dilations: list<int>,
  group: int
) -> Y {
  convolved: Y = t.conv(
    input, weight, bias, strides, pads, dilations, group
  );
  return t.relu(convolved);
}
```

The tensor integration test materializes a shape-complete SqueezeNet Fire
block, matches its squeeze Conv/Relu DAG, and replaces exactly that pair with
an extension-local `conv_relu`. The transformed Function verifies and
round-trips through canonical source without changing `tensor@1.0.0`.

## Evidence boundary

Implemented and tested:

- ordinary installable tensor Module;
- static-shape validation through the general type-verifier API;
- typed Conv, Relu, and Concat materialization for a Fire block;
- preservation of convolution and concatenation properties;
- extension-local typed fusion and canonical round-trip;
- a source-bodied fused kernel whose positive replacement is definitionally
  proved and whose type-correct semantic mismatch is rejected atomically.

The separate `onnx` Module now imports the exact pinned SqueezeNet 1.1 model,
preserves its initializers and supported properties, and has exact differential
ONNX Runtime evidence. This does not broaden `tensor` itself into an ONNX
schema.

Not yet claimed:

- general ONNX operator-set coverage or dynamic shapes;
- physical layout, packed formats, or storage planning;
- executable hardware implementation and performance.

The semantic transformation boundary is defined by
[RFC 0007](rfcs/0007-reference-bodied-transformations.md).
