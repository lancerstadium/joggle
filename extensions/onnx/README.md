# ONNX Module

This optional extension implements ONNX import as an ordinary typed Joggle
function:

```joggle
fn read(input: bytes) -> (module, resources);
```

The returned Module contains `tensor.constant` instructions keyed by
`sha256:<digest>`. Their bytes remain in the explicit `onnx.resources` value;
there is no global resource store and large weights are not expanded into the
textual IR. Calling `read` twice with identical bytes produces identical Module
and resource identities.

Build with:

```sh
cmake -S . -B build-onnx \
  -DJOGGLE_BUILD_ONNX=ON \
  -DCMAKE_PREFIX_PATH=/path/to/protobuf
cmake --build build-onnx
ctest --test-dir build-onnx -R '^onnx$' --output-on-failure
```

Set `JOGGLE_ONNX_MODEL=/path/to/resnet18_Opset18.onnx` to register the full
Model Zoo import as `onnx_model`; the model remains outside the repository.
The reference file is the official
[`resnet18_Opset18_timm`](https://huggingface.co/onnxmodelzoo/resnet18_Opset18_timm)
model (46,748,544 bytes, SHA-256
`bb6228db49fbdaaa00f5ab51b052166f4cc717559d25880741d3167c98491870`).
This is a semantic integration test, not a performance benchmark.

The build uses protobuf only for this extension. By default it downloads the
ONNX 1.22.0 schema pinned by commit and SHA-256; an offline build can set
`JOGGLE_ONNX_PROTO` to the same `onnx.proto3` file. Generated protobuf sources
stay in the build directory.

The initial semantic profile is deliberately exact: static ranked tensors,
`ai.onnx` opset 18, inline `raw_data`, and the ResNet-18 operator set (`Conv`,
`Relu`, `Add`, `MaxPool`, `GlobalAveragePool`, `Flatten`, and `Gemm`). Unsupported
domains, attributes, shapes, encodings, or operators fail with node context;
the importer never drops them silently.
