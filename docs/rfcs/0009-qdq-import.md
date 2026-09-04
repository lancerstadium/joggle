# RFC 0009: QDQ as an ordinary semantic Module

Status: import, affine-oracle, and transparent-composite gates implemented

## Problem

The first ONNX slice covered a FLOAT opset 7 model. Extending it by adding a
quantized-tensor subclass, a frontend-op hierarchy, or a QLinear lowering
registry would contradict Joggle's single `type`/`fn` extension plane. It would
also mix three independent concerns: file-format decoding, affine numerical
semantics, and physical storage optimization.

## Decision

`quant@1.1.0` declares two ordinary program functions:

```joggle
fn quantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
fn dequantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
```

Scale and zero point are normal tensor Values. Their Types and shapes state
per-tensor or per-axis parameterization without a privileged quantization
object. The ONNX compiler function maps a checked IR 7/opset 13 QDQ graph to
these calls plus the existing tensor vocabulary. A generated model derives
its `quant` and `tensor` dependencies from the Function itself.

The same names have six-argument compiler-time overloads over bytes, f32
scales, integer zero points, logical shape, axis, and storage Type. They are a
deterministic numerical oracle, not a second operation kind. Their wire format
is explicit little-endian f32 or integer storage, and their implementation
fixes nearest-even rounding, saturation, per-axis row-major indexing, negative
axes, and i32 zero-point constraints.

The importer supports one exact QDQ profile. It does not silently accept all
opset 13 models. It validates static value metadata, dense inline constants,
f32/u8/i8/i32/i64 storage, axis broadcasting, Conv and pool geometry, and the
complete output contract transactionally.

## Why QDQ first

The audited QOperator SqueezeNet uses a `com.microsoft` global-average-pool
operation. The corresponding QDQ model uses only standard ai.onnx nodes and
keeps quantization boundaries visible around normal Conv and tensor
operations. QDQ therefore tests the extension mechanism without conflating it
with a vendor operation or an integer-kernel selection policy.

This decision does not make QOperator import impossible. A later Module may
define source-grounded fused integer kernels and transform equivalent QDQ
regions to them. That transformation must be justified by semantics and
measurement, not by an importer special case.

## Evidence and boundary

The hash-pinned Model Zoo SqueezeNet 1.0 QDQ graph imports as 228 typed
constants, 39 quantize calls, 91 dequantize calls, and 41 ordinary tensor
calls. A test-only reconstruction from the resulting Function and Module data
has exact ONNX Runtime output on a deterministic input (`max_abs=0`,
`mean_abs=0`). Its install/check/lock bundle preserves all 148 deduplicated
payloads and both semantic dependencies. Omitting `quant` fails with a
deterministic diagnostic.

This proves the frontend mapping and dependency composition. The tensor QDQ
calls remain bodyless, so algebraic rewrites across them fail closed. The
compiler-time affine overloads match a standard opset 13 QDQ micrograph in
ONNX Runtime for i8 and f32 outputs at the bit level, including halfway values
and per-axis broadcasting.

`qdq@1.0.0` is an operator-independent library of transparent QDQ composites.
Its first ordinary source-bodied function, `nchw_conv`, plus normal
`run(function|module)` compiler functions reproduce the same opaque QDQ leaves,
allowing all 26 Conv regions in the official model to be packaged behind a
proved semantic seam. Eight shared activation
Dequantizers cover 16 branch candidates and are neither cloned nor deleted
early. The whole Function changes from 399 to 303 calls/constants and remains
definitionally equivalent. Conv is evidence for the general composition
mechanism, not the quantization abstraction. This is not an integer
implementation; a later concrete realization must be checked against the
affine oracle and a trusted runtime.
