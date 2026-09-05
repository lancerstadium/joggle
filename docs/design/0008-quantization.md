# Design 0008: Quantized ONNX import

Status: accepted; conversion implementation pending

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
per-tensor or per-axis parameterization. `onnx_schema@1` declares source
`QuantizeLinear` and `DequantizeLinear` calls without algorithm bodies. A future
ONNX-to-NN/quant pass will validate ONNX attributes and construct independent
quant semantics; the byte reader does not perform that conversion.

There is no byte-oriented compiler-time overload and no `quant` native
library. Numerical execution belongs to a whole-program implementation, not
to per-Op host dispatch.

## Evidence and boundary

The hash-pinned Model Zoo SqueezeNet 1.0 QDQ graph imports as 228 typed
constants and 171 ONNX calls, including 39 QuantizeLinear and 91
DequantizeLinear calls.
Reconstruction from the imported Fn and Mod-owned data produces exact ONNX
Runtime output on the deterministic validation input. This validates faithful
source import and export, not the pending conversion or quantized execution.

QDQ calls remain bodyless source-schema leaves until an explicit conversion
supplies frontend-neutral semantics. No QLinear kernel or quantized Conv
wrapper is implied.
