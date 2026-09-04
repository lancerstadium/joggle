# Quantization

`quant@1.1.0` is a small semantic boundary for affine tensor quantization:

```joggle
fn quantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
fn dequantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
```

The tensor, scale, and zero point are normal typed Vals. Per-tensor and
per-axis parameters therefore use the same `tensor.tensor` Type and
`tensor.constant` fn as the rest of a model. The storage element Type is
the zero point's element Type; the expressed Type is determined by the input
or result. There is no quantized-tensor subclass, datatype registry, ONNX node
object, or compiler-core quantization category.

The first consumer is the ONNX IR 7/opset 13 QDQ profile. Its importer checks
static shapes, FLOAT scales, matching scale/zero shapes, scalar or one-axis
broadcasting, compatible storage Types, and valid per-axis indices before
constructing a call. The official QDQ SqueezeNet model supplies u8 activations,
i8 weights, i32 biases, and f32 expressed tensors through these declarations.

## Executable reference

Version 1.1 adds compiler-time overloads of the same two fn names:

```joggle
fn quantize(
  input: bytes,
  scale: list<real>,
  zero: list<int>,
  shape: list<int>,
  axis: int,
  storage: type
) -> bytes;

fn dequantize(
  input: bytes,
  scale: list<real>,
  zero: list<int>,
  shape: list<int>,
  axis: int,
  storage: type
) -> bytes;
```

They are selected only by an explicit compiler-time call such as
`@quantize(...)`; they do not execute a residual tensor call implicitly.
Quantize input and dequantize output are little-endian f32 bytes. Storage is
u8 or i8 for quantization and u8, i8, or i32 for dequantization. Shape is the
logical row-major shape. One scale/zero pair means per-tensor quantization;
otherwise the list length must equal the selected axis extent.

The implementation fixes f32 division, round-to-nearest-even independently of
the host rounding mode, saturation, two's-complement storage, negative axes,
and f32 dequantization. Positive finite f32 scales are required. Quantization
rejects NaN because ONNX does not assign it a portable integer result; positive
and negative infinity saturate. The i32 dequantization overload requires a
zero point of zero.

Tests cover positive and negative halfway values, both saturation limits,
per-axis broadcasting, negative axes, i32 bias values, malformed inputs, and
repeated execution. A separately generated standard opset 13 QDQ graph is run
by ONNX Runtime with optimization disabled; its i8 output and dequantized f32
bit patterns exactly match the Joggle oracle.

## Current trust boundary

The tensor overloads remain deliberately bodyless program semantics. The
equivalence checker therefore treats each exact QDQ call as an opaque leaf and
does not infer algebraic identities across quantization boundaries. The bytes
overloads make numerical tests reproducible; they do not silently grant such
identities.

No quantized Conv or MatMul wrapper is provided. A later integer kernel must
have a concrete executable fn body and be checked against this oracle
and a trusted runtime; naming a QDQ subgraph does not constitute an
implementation. Successful ONNX Runtime differential execution remains
evidence for the numerical contract, not a substitute for transformation
correctness.

The operator contract follows the standard ONNX
[QuantizeLinear](https://onnx.ai/onnx/operators/onnx__QuantizeLinear.html) and
[DequantizeLinear](https://onnx.ai/onnx/operators/onnx__DequantizeLinear.html)
semantics.
