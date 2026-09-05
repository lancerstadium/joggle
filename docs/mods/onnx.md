# ONNX

`onnx@5` is an optional protobuf reader with one compiler-time function:

```joggle
fn read(input: bytes, name: string = "model") -> mod;
```

Call it explicitly with `@onnx.read`. It is a file operation, not an
optimization pass. The reader decodes and validates ONNX records, converts
external CamelCase names to ordinary Joggle naming, and asks the linked
compiler environment for a unique compatible fn. Argument names, defaults,
types, and overloads perform the binding; there is no switch over Conv, Relu,
MatMul, or other node kinds.

The resulting model contains calls to `tensor`, `nn`, and `quant`. It contains
no `onnx.*` program operation, ONNX schema module, temporary ONNX IR, or hidden
conversion phase. Initializer bytes and the original model are immutable data
owned by the returned `Mod`.

The tested profiles are the hash-pinned Model Zoo SqueezeNet FLOAT
IR-3/opset-7 model and QDQ IR-7/opset-13 model. Both import deterministically.
This proves typed format ingestion and preservation, not numerical execution.
Unsupported domains, symbolic shapes, external data, subgraphs, and unmatched
semantics fail with diagnostics.
