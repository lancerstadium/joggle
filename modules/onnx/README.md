# ONNX Module

This optional Module separates format import from semantic conversion with two
ordinary compiler functions:

```joggle
fn read(input: bytes) -> module;
fn to_nn(input: module) -> module;
```

`read` preserves supported nodes as `onnx.constant`, `onnx.conv`,
`onnx.relu`, and the other declared `onnx.*` Ops. ONNX properties such as
strides, dilation, padding, grouping, and Gemm transpose flags remain named
compiler-domain inputs. It does not silently turn the input into a different
operator vocabulary.

`to_nn` is a transactional `module -> module` conversion. It rewrites every
ONNX Op to the portable `nn` and `tensor` vocabularies and publishes only when
no illegal `onnx.*` Op remains. Unsupported semantic combinations are rejected
at this boundary. This gives fusion or source-level analysis a stable ONNX IR,
while target-independent optimization can explicitly request NN IR.

Initializer bytes are stored once in the returned Module under
`sha256:<digest>` names. Both `onnx.constant` and `tensor.constant` implement
`tensor.immutable_data`, so quantization and storage passes can find constants
without depending on ONNX. Conversion shares the same immutable payloads.

Build with:

```sh
cmake -S . -B build-onnx \
  -DJOGGLE_BUILD_MODULES=onnx \
  -DCMAKE_PREFIX_PATH=/path/to/protobuf
cmake --build build-onnx
ctest --test-dir build-onnx -R '^module\.onnx$' --output-on-failure
```

Set `JOGGLE_ONNX_MODEL=/path/to/resnet18_Opset18.onnx` to register the full
Model Zoo import as `onnx_model`; the model remains outside the repository.
The reference file is the official
[`resnet18_Opset18_timm`](https://huggingface.co/onnxmodelzoo/resnet18_Opset18_timm)
model (46,748,544 bytes, SHA-256
`bb6228db49fbdaaa00f5ab51b052166f4cc717559d25880741d3167c98491870`).
This is a semantic integration test, not a performance benchmark.

The build uses protobuf only for this Module's behavior. By default it downloads the
ONNX 1.22.0 schema pinned by commit and SHA-256; an offline build can set
`JOGGLE_ONNX_PROTO` to the same `onnx.proto3` file. Generated protobuf sources
stay in the build directory.

The initial semantic profile is deliberately exact: static ranked tensors,
`ai.onnx` opset 18, inline `raw_data`, and the ResNet-18 operator set (`Conv`,
`Relu`, `Add`, `MaxPool`, `GlobalAveragePool`, `Flatten`, and `Gemm`). Unsupported
domains, attributes, shapes, encodings, or operators fail with node context;
the importer never drops them silently.
