# ONNX

The optional `onnx@1.0.0` Mod converts a deliberately narrow ONNX
inference model into an ordinary Joggle `Mod`:

```joggle
joggle 1;

mod pipeline@1.0.0 {
  import onnx@1;

  fn load(input: bytes) -> mod {
    return @onnx.read(input, "squeezenet");
  }
}
```

`read` is one explicitly staged compiler fn. It does not create an
ONNX graph class, frontend registry, pass kind, or lowering level. The returned
Mod owns one typed `main` Fn and content-addressed copies of the
source model and initializer bytes.

For QDQ input, the calling environment must also load `quant@2`; the returned
model then derives its exact `quant` dependency from its Fn calls. FLOAT
input does not acquire that dependency.

## Implemented profiles

The importer accepts two exact profiles rather than a range of vaguely
compatible versions.

The FLOAT profile is ONNX IR 3, `ai.onnx` opset 7, static dense NCHW inference
with:

- Conv with optional bias;
- Relu;
- MaxPool and AveragePool in floor mode;
- two-input Concat;
- inference-mode Dropout as an identity;
- Reshape with a constant INT64 shape.

The QDQ profile is ONNX IR 7, `ai.onnx` opset 13, static dense inference with:

- standard QuantizeLinear and DequantizeLinear using scalar or one-axis
  FLOAT scales and u8, i8, or i32 zero points;
- Conv, MaxPool, two-input Concat, Reshape, and Softmax;
- GlobalAveragePool expressed as an ordinary AveragePool call; and
- Flatten expressed as an ordinary Reshape call.

QDQ calls belong to the independent `quant` Mod; all other calls remain in
`tensor`. Initializer payloads retain f32, u8, i8, i32, and i64 element Types.
The importer accepts unused non-standard opset imports but rejects every node
outside `ai.onnx`, so the supported QDQ path does not depend on a vendor
operator.

Every IR 3 initializer must match its graph-input declaration, while IR 7
initializers must not masquerade as runtime inputs. Symbolic dimensions,
external or sparse tensors, quantization annotations, subgraphs, training
data, unknown attributes, and unsupported operators are rejected before a
Mod is returned. Intermediate metadata, inferred shapes, element Types,
and the declared graph output are checked for agreement.

This is not a general ONNX compatibility claim. Mod-owned bytes are
preserved by the lossless CLI and repository bundle defined in
[Design 0006](../design/0006-bundles.md).

## Optional build

The compiler core and tensor Mod do not depend on Protobuf. Enable the
importer explicitly:

```bash
cmake -S . -B build \
  -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_ONNX_TEST_MODEL="$PWD/build/models/squeezenet1.1-7.onnx" \
  -DJOGGLE_ONNX_QDQ_TEST_MODEL="$PWD/build/models/squeezenet1.0-13-qdq.onnx"
cmake --build build --target joggle_fetch_squeezenet
cmake --build build --target joggle_fetch_squeezenet_qdq
cmake --build build
ctest --test-dir build -R '^(onnx|onnx_qdq)$' --output-on-failure
```

The fetch target downloads the ONNX Model Zoo SqueezeNet 1.1 model from commit
`4c46cd00fbdb7cd30b6c1c17ab54f2e1f4f7b177` into the build directory and
requires SHA-256
`1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088`.
The QDQ target fetches SqueezeNet 1.0 opset 13 from the same commit and requires
SHA-256
`4a567dd7542ef440890d57268fabf47211174c593d7a1837bd7f16a1067169e7`.
The generic verified-download script never writes a model into the source tree
and refuses to overwrite a cache file whose digest differs.

The native library is installed beside `mod.joggle`, preserving the normal
Mod bundle layout. Generated Protobuf C++ remains in the build directory.
The vendored schema provenance is recorded in
`mods/onnx/third_party/README.md`.

## Differential runtime validation

The runtime test is opt-in because it needs Python, NumPy, and ONNX Runtime:

```bash
cmake -S . -B build-runtime \
  -DCMAKE_BUILD_TYPE=Release \
  -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_ONNX_RUNTIME_VALIDATION=ON \
  -DJOGGLE_ONNX_TEST_MODEL="$PWD/build/models/squeezenet1.1-7.onnx" \
  -DJOGGLE_ONNX_QDQ_TEST_MODEL="$PWD/build/models/squeezenet1.0-13-qdq.onnx"
cmake --build build-runtime
ctest --test-dir build-runtime \
  -R '^(onnx_runtime|onnx_qdq_runtime)$' -V
```

The test reconstructs a standard ONNX graph from only the imported Joggle
Fn, call properties, result Types, and Mod-owned initializer bytes.
It then runs the original and reconstructed graphs through the ONNX Runtime CPU
provider with graph optimization disabled. A fixed ramp input is used twice to
check determinism. The audited run produced output shape `[1,1000]`,
`max_abs=0`, and `mean_abs=0`. The QDQ round trip independently produced
shape `[1,1000,1,1]`, `max_abs=0`, and `mean_abs=0`.

The reconstruction executable is test-only. It is not a public ONNX emitter,
Mod fn, compiler-core category, or second production graph.

## Lossless CLI workflow

A driver may call `@onnx.read` and write the returned Mod to a bundle:

```bash
joggle run driver.joggle read squeezenet1.1-7.onnx \
  --with /path/to/tensor/mod.joggle \
  --with /path/to/onnx/mod.joggle \
  --load-native onnx=/path/to/onnx/native \
  -o squeezenet-bundle

joggle run driver.joggle read squeezenet1.0-13-qdq.onnx \
  --with /path/to/tensor/mod.joggle \
  --with /path/to/quant/mod.joggle \
  --with /path/to/onnx/mod.joggle \
  --load-native onnx=/path/to/onnx/native \
  -o squeezenet-qdq-bundle

joggle install /path/to/tensor/mod.joggle
joggle check squeezenet-bundle
joggle install squeezenet-bundle
```

The real-model CLI tests verify 54 FLOAT and 148 deduplicated QDQ payload
files, install their dependencies and models, reload the installed identities
as bundles, and lock the exact model, tensor, and quant digests.

## Reference-model evidence

The exact audited model imports as:

- one runtime input, `tensor<f32, [1, 3, 224, 224]>`;
- 52 FLOAT `tensor.constant` calls;
- 65 semantic tensor calls after inference Dropout is elided;
- one output, `tensor<f32, [1, 1000]>`.

All 117 retained calls have deterministic ONNX source locations. The original
model plus all 53 initializer payloads are retained as 54 distinct
content-addressed Mod data entries. The test fixes the independently audited
65-node call/shape sequence, operator properties and counts, every payload
digest, the exact source digest, and repeated-import identity.

The exact audited QDQ model imports as:

- one runtime input, `tensor<f32, [1, 3, 224, 224]>`;
- 228 typed constants: 88 f32, 36 u8, 52 i8, and 52 i32;
- 39 `quant.quantize` and 91 `quant.dequantize` calls;
- 41 ordinary tensor calls, including all 26 Conv operations; and
- one output, `tensor<f32, [1, 1000, 1, 1]>`.

Content addressing deduplicates the source model and 229 initializer payloads
to 148 data files. The QDQ bundle test installs both semantic dependencies,
reloads the model, verifies the same 148 payloads, and locks exact `quant` and
`tensor` versions.

The QDQ path deliberately stops at preserving the standard affine boundary.
See [Quantization](quant.md) for why transformations may not yet rewrite
through those two opaque fns.
