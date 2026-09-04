# ONNX inference import

The optional `onnx@1.0.0` Module converts a deliberately narrow ONNX
inference model into an ordinary Joggle `Module`:

```joggle
joggle 1;

module pipeline@1.0.0 {
  import onnx@1;

  fn load(input: bytes) -> module {
    return @onnx.read(input, "squeezenet");
  }
}
```

`read` is one explicitly staged compiler function. It does not create an
ONNX graph class, frontend registry, pass kind, or lowering level. The returned
Module owns one typed `main` Function and content-addressed copies of the
source model and initializer bytes.

## Implemented boundary

The first vertical slice accepts ONNX IR 3, `ai.onnx` opset 7, static dense
FLOAT NCHW inference graphs, and these operators:

- Conv with optional bias;
- Relu;
- MaxPool and AveragePool in floor mode;
- two-input Concat;
- inference-mode Dropout as an identity;
- Reshape with a constant INT64 shape.

Every initializer must match its IR 3 graph-input declaration. Symbolic
dimensions, external or sparse tensors, intermediate value metadata,
quantization annotations, subgraphs, training data, custom domains, unknown
attributes, and unsupported operators are rejected before a Module is
returned. Shape propagation checks the declared graph output.

This is not a general ONNX compatibility claim. Bundle persistence for
Module-owned bytes remains a separate implementation gate.

## Optional build

The compiler core and tensor Module do not depend on Protobuf. Enable the
importer explicitly:

```bash
cmake -S . -B build \
  -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_ONNX_TEST_MODEL="$PWD/build/models/squeezenet1.1-7.onnx"
cmake --build build --target joggle_fetch_squeezenet
cmake --build build
ctest --test-dir build -R '^onnx$' --output-on-failure
```

The fetch target downloads the ONNX Model Zoo SqueezeNet 1.1 model from commit
`4c46cd00fbdb7cd30b6c1c17ab54f2e1f4f7b177` into the build directory and
requires SHA-256
`1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088`.
It never writes a model into the source tree.

The native library is installed beside `module.joggle`, preserving the normal
Module bundle layout. Generated Protobuf C++ remains in the build directory.
The vendored schema provenance is recorded in
`modules/onnx/third_party/README.md`.

## Differential runtime validation

The runtime test is opt-in because it needs Python, NumPy, and ONNX Runtime:

```bash
cmake -S . -B build-runtime \
  -DCMAKE_BUILD_TYPE=Release \
  -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_ONNX_RUNTIME_VALIDATION=ON \
  -DJOGGLE_ONNX_TEST_MODEL="$PWD/build/models/squeezenet1.1-7.onnx"
cmake --build build-runtime
ctest --test-dir build-runtime -R '^onnx_runtime$' -V
```

The test reconstructs a standard ONNX graph from only the imported Joggle
Function, call properties, result Types, and Module-owned initializer bytes.
It then runs the original and reconstructed graphs through the ONNX Runtime CPU
provider with graph optimization disabled. A fixed ramp input is used twice to
check determinism. The audited run produced output shape `[1,1000]`,
`max_abs=0`, and `mean_abs=0`.

The reconstruction executable is test-only. It is not a public ONNX emitter,
Module function, compiler-core category, or second production graph.

## Reference-model evidence

The exact audited model imports as:

- one runtime input, `tensor<f32, [1, 3, 224, 224]>`;
- 52 FLOAT `tensor.constant` calls;
- 65 semantic tensor calls after inference Dropout is elided;
- one output, `tensor<f32, [1, 1000]>`.

All 117 retained calls have deterministic ONNX source locations. The original
model plus all 53 initializer payloads are retained as 54 distinct
content-addressed Module data entries. The test fixes the independently audited
65-node call/shape sequence, operator properties and counts, every payload
digest, the exact source digest, and repeated-import identity.
