# Quant module

`quant@1.0.0` is a small semantic boundary for affine tensor quantization:

```joggle
fn quantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
fn dequantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
```

The tensor, scale, and zero point are normal typed Values. Per-tensor and
per-axis parameters therefore use the same `tensor.tensor` Type and
`tensor.constant` function as the rest of a model. The storage element Type is
the zero point's element Type; the expressed Type is determined by the input
or result. There is no quantized-tensor subclass, datatype registry, ONNX node
object, or compiler-core quantization category.

The first consumer is the ONNX IR 7/opset 13 QDQ profile. Its importer checks
static shapes, FLOAT scales, matching scale/zero shapes, scalar or one-axis
broadcasting, compatible storage Types, and valid per-axis indices before
constructing a call. The official QDQ SqueezeNet model supplies u8 activations,
i8 weights,
i32 biases, and f32 expressed tensors through these two declarations.

## Current trust boundary

Both functions are deliberately bodyless program semantics in version 1.
They preserve the standard QDQ boundary exactly and can be emitted or executed
by a consumer that implements it, but the current equivalence checker will not
expand through them. Consequently Joggle does not yet claim that a rewrite
which changes or removes a QDQ boundary is proved correct.

The next semantic gate is a portable, executable affine reference body (or an
equally explicit checked definition) covering rounding, saturation,
broadcasting, and per-axis behavior. Until that exists, format optimization
must treat `quantize` and `dequantize` as opaque calls. This limitation is
intentional: successful ONNX Runtime differential execution is evidence for
the importer, not a substitute for transformation correctness.

The operator contract follows the standard ONNX
[QuantizeLinear](https://onnx.ai/onnx/operators/onnx__QuantizeLinear.html) and
[DequantizeLinear](https://onnx.ai/onnx/operators/onnx__DequantizeLinear.html)
semantics.
