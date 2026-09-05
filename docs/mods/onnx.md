# ONNX frontend

The ONNX frontend is deliberately split by responsibility:

- `onnx@5` owns the Protobuf reader service `read(bytes, name) -> mod`;
- `onnx_schema@1` owns source-format declarations, defaults, and static shape
  functions;
- `nn@1` owns frontend-independent algorithm bodies;
- a conversion pass will rewrite source-schema calls to `nn` calls.

The reader is schema-driven. For each node it looks up declarations by
`op_type`, binds graph inputs and named attributes against the selected source
signature, and uses normal Joggle call inference. There is no C++ switch for
Conv, Relu, MatMul, or other individual operations. Adding a compatible source
declaration therefore does not require rebuilding an operator visitor.

This separation matters for additional frontends. TFLite has different builtin
codes, option tables, defaults, layouts, quantization rules, and versioning; it
must preserve those facts in its own reader/schema. It should not impersonate
ONNX or duplicate NN implementations. Both frontends converge only after
explicit conversion:

```text
ONNX bytes  -> onnx_schema calls  --pass--> nn calls
TFLite bytes -> tflite_schema calls --pass--> nn calls
```

Source-schema functions may contain compiler-time shape equations because the
reader needs typed results. Residual algorithm bodies are forbidden there.
MatMul, Relu, and future Conv bodies belong to `nn`, expressed using
`tensor.compute`, overloaded `[]`, ordinary loops, and scalar overloads.

The current importer supports the audited SqueezeNet opset-7 and QDQ opset-13
fixtures, typed static shapes, deterministic source locations, and Mod-owned
constant payloads. Coverage is intentionally narrower than the ONNX standard;
unsupported schemas fail explicitly. Opset selection, richer ONNX control flow,
symbolic dimensions, and the ONNX-to-NN conversion package remain future work.
