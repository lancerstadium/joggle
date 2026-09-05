# Quantization

`quant@2.0.0` declares the two affine boundaries needed by QDQ graphs:

```joggle
fn quantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
fn dequantize<X, S, Z, Y>(input: X, scale: S, zero: Z, axis: int = 1) -> Y;
```

The data, scale, and zero point are ordinary typed values. `axis` is a
compiler parameter retained on the call. The input and expected result types
determine expressed and storage element types; no quantized-tensor subclass or
compiler registry is involved.

The Mod deliberately has no native library. These fns are Residual semantics,
not host callbacks. Importing a model containing hundreds of QDQ calls
therefore creates zero per-call or per-operator bindings. A compiler fn may
transform the complete model, and a later whole-Mod implementation may consume
the resulting call graph.

The ONNX QDQ importer validates static shapes, FLOAT scales, scale/zero
broadcasting, storage types, and axes before constructing these calls. Exact
ONNX Runtime reconstruction checks the imported graph as a whole. It does not
install a second byte-oriented quantization API or claim that a C++ reference
function is an implementation of the Residual program.

No quantized Conv or MatMul wrapper is provided. Integer kernels remain normal
source fns selected by explicit transformations.
