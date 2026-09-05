# ONNX

`onnx@5` is an optional Protobuf reader with one compiler-time fn:

```joggle
fn read(input: bytes, name: string = "model") -> mod;
```

The reader converts ONNX records directly into calls to linked semantic fns.
It normalizes external naming and binds attributes by parameter name, defaults,
types, and overload resolution. It does not create `onnx.Conv` operations, an
ONNX IR, or an automatic conversion pass.

The current importer has deterministic structural tests using hash-pinned ONNX
Model Zoo SqueezeNet FLOAT and QDQ models. Successful import proves faithful
typed ingestion and data preservation; it does not claim numerical execution
for bodyless NN functions.

Unsupported domains, symbolic shapes, external data, subgraphs, and unmatched
semantics fail with diagnostics. Source-format exceptions belong in the reader,
while operator implementations belong in `nn`, `tensor`, `quant`, or a target
package.
