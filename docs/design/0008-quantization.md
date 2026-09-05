# Design 0008: Quantized ONNX import

Status: accepted

## Problem

Quantized import must preserve affine QDQ semantics without adding a
quantized-value subclass, frontend operation hierarchy, execution registry, or
one native callback for every program operation.

## Decision

`quant@2.0.0` is a source-only semantic Mod:

```joggle
fn quantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
fn dequantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
```

Scale and zero point are normal tensor values. Their types and shapes state
per-tensor or per-axis parameterization. The ONNX compiler fn maps a checked
IR 7/opset 13 QDQ graph to these calls plus ONNX semantic calls over tensor
Types; the generated Mod derives those dependencies from its Fn.

There is no byte-oriented compiler-time overload and no `quant` native
library. Numerical execution belongs to a whole-program implementation, not
to per-Op host dispatch.

## Evidence and boundary

The hash-pinned Model Zoo SqueezeNet 1.0 QDQ graph imports as 228 typed
constants, 39 quantize calls, 91 dequantize calls, and 41 ONNX calls.
Reconstruction from the imported Fn and Mod-owned data produces exact ONNX
Runtime output on the deterministic validation input. Omitting `quant` fails
with a deterministic diagnostic.

This proves the frontend mapping and dependency composition. QDQ calls remain
bodyless semantic leaves until a source implementation or explicit
transformation supplies an implementation. No QLinear kernel or quantized
Conv wrapper is implied.
