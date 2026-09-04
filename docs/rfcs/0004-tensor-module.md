# RFC 0004: Static tensor semantic module

Status: implementation gates 1--4 complete

## Purpose

Joggle needs a target-independent tensor vocabulary before ONNX import,
storage planning, or kernel selection can be evaluated. Tensor semantics must
remain a normal installable Module: the core gains no tensor declaration kind,
shape object, graph container, operation registry, or lowering interface.

The first vertical slice targets the inference operators required by the
ONNX SqueezeNet 1.1 model. This is deliberately narrower than ONNX while being
large enough to exercise branches, repeated Fire modules, convolution
attributes, pooling, concatenation, reshape, and classification output.

## Type boundary

```joggle
type tensor(element: type, shape: list<int>);
```

`element` is an ordinary installed Type. Operations may constrain which
element Types they support; the compiler core does not maintain a numeric-type
registry. `shape` is ordered semantic axis extent and contains non-negative
static dimensions in this first slice. The integration registers that
invariant through the normal type-verifier API. Symbolic dimensions are
deferred until a real imported model requires them; `-1`, magic strings, and a
second dimension AST are rejected.

Physical layout, packing, address space, and storage allocation are not tensor
semantics. Later format or storage modules may introduce their own Types and
compiler functions without changing `tensor` or compiler core.

## Function vocabulary

Calls are ordinary typed `fn` declarations. Generic result types allow an
importer or source annotation to state the verified output tensor explicitly.
Compiler-domain attributes stay in the same parameter list and become Known
Op properties.

Function parameters are model inputs. Initializers use
`constant<T>(content: string)`, where `content` is the digest returned by
`Module::store`; large bytes remain Module-owned rather than becoming text IR
properties. The initial module includes Conv with and without bias, Relu,
MaxPool, AveragePool, two-input Concat, Reshape, Softmax, and Add/Multiply.
Two-input Concat uses independent input types because the channel extents
differ in SqueezeNet. No heterogeneous variadic pack is invented before a
model requires it.

## Transformation use

Fused calls are not added to the semantic module. A user or target extension
may declare `conv_relu` and use ordinary typed lambdas with `@replace` to turn
the semantic pair into that call. This directly tests whether a new kernel can
be introduced without modifying tensor core or a global lowering table.

## ONNX boundary

The later ONNX module will parse protobuf in a native compiler function and
construct Functions using exact tensor declarations and explicit result Types.
It must preserve initializers, attributes, graph inputs/outputs, and source
names, reject unsupported dynamic or operator semantics, and differentially
validate the imported SqueezeNet Function against ONNX Runtime before support
is claimed.

## Implementation gates

- [x] Add and install the ordinary `tensor@1.0.0` source Module.
- [x] Register and test static-shape tensor invariants without a core type
  case.
- [x] Materialize a typed SqueezeNet Fire block and round-trip canonical IR.
- [x] Fuse its Conv/Relu pair through RFC 0003 using an extension-local call.
- [ ] Import the maintained ONNX SqueezeNet 1.1 artifact with initializer and
  attribute fidelity.
- [ ] Differentially validate outputs and record unsupported-model
  diagnostics.

Gates 5--6 are required before documentation calls this an ONNX frontend.
