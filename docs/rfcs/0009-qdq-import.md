# RFC 0009: QDQ as an ordinary semantic Module

Status: external-model import and differential gate implemented

## Problem

The first ONNX slice covered a FLOAT opset 7 model. Extending it by adding a
quantized-tensor subclass, a frontend-op hierarchy, or a QLinear lowering
registry would contradict Joggle's single `type`/`fn` extension plane. It would
also mix three independent concerns: file-format decoding, affine numerical
semantics, and physical storage optimization.

## Decision

`quant@1.0.0` declares only two ordinary program functions:

```joggle
fn quantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
fn dequantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
```

Scale and zero point are normal tensor Values. Their Types and shapes state
per-tensor or per-axis parameterization without a privileged quantization
object. The ONNX compiler function maps a checked IR 7/opset 13 QDQ graph to
these calls plus the existing tensor vocabulary. A generated model derives
its `quant` and `tensor` dependencies from the Function itself.

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

This proves the frontend mapping and dependency composition. It does not prove
rewrites through QDQ calls: version 1 leaves them bodyless, so equivalence
fails closed at those boundaries. The next gate is a portable affine reference
definition with explicit rounding, saturation, and broadcasting, followed by
one source-grounded integer kernel replacement.
