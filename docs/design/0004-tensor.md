# Design 0004: Tensor semantics

Status: accepted

## Purpose

Joggle needs a target-independent tensor vocabulary before ONNX import,
storage planning, or kernel selection can be evaluated. Tensor semantics must
remain a normal installable Mod: the core gains no tensor declaration kind,
shape object, graph container, operation registry, or lowering interface.

The first vertical slice targets the inference operators required by the
ONNX SqueezeNet 1.1 model. This is deliberately narrower than ONNX while being
large enough to exercise branches, repeated Fire mods, convolution
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
semantics. Later format or storage mods may introduce their own Types and
compiler fns without changing `tensor` or compiler core.

## Fn vocabulary

Calls are ordinary typed `fn` declarations. Generic result types allow an
importer or source annotation to state the verified output tensor explicitly.
Compiler-domain attributes stay in the same parameter list and become Known
callee specialization bindings.

Fn parameters are model inputs. Initializers use
`constant<T>(content: string)`, where `content` is the digest returned by
`Mod::store`; large bytes remain Mod-owned rather than becoming text IR
bindings. The initial mod includes Conv with and without bias, Relu,
MaxPool, AveragePool, two-input Concat, Reshape, Softmax, and Add/Multiply.
Two-input Concat uses independent input types because the channel extents
differ in SqueezeNet. No heterogeneous variadic pack is invented before a
model requires it.

## Transformation use

Implementation calls are not added to the semantic mod. Transformations may
match tensor expressions through ordinary typed lambdas, but replacing them
with an executable kernel requires a real implementation body. Renaming a
Conv/Relu pair is not accepted as fusion evidence.

## ONNX boundary

The ONNX mod parses protobuf in a native compiler fn and
construct Fns using exact tensor declarations and explicit result Types.
It must preserve initializers, attributes, graph inputs/outputs, and source
names, reject unsupported dynamic or operator semantics, and differentially
validate the imported SqueezeNet Fn against ONNX Runtime before support
is claimed.

## Implementation gates

- [x] Add and install the ordinary `tensor@1.0.0` source Mod.
- [x] Register and test static-shape tensor invariants without a core type
  case.
- [x] Materialize a typed SqueezeNet Fire block and round-trip canonical IR.
- [x] Exercise typed structural replacement on its Conv/Relu expression through
  Design 0003 without claiming an executable fused kernel.
- [x] Import the maintained ONNX SqueezeNet 1.1 artifact with initializer and
  attribute fidelity.
- [x] Differentially validate outputs and record unsupported-model
  diagnostics.
