# ONNX

`onnx@5.0.0` is an optional source Mod with one native file decoder. It turns
an ONNX model into an ordinary Joggle `Mod`; it does not add a frontend class,
ONNX Op hierarchy, graph container, or lowering level to compiler core.

```joggle
joggle 1;

mod pipeline@1.0.0 {
  import onnx@5;

  fn load(input: bytes) -> mod {
    return @onnx.read(input, "squeezenet");
  }
}
```

The ONNX source Mod imports `arith@1`, `tensor@4`, and `quant@2`. Callers must
therefore load those dependencies before linking. An imported model records
only the Mods referenced directly by its Fn.

## Extension model

The native decoder handles the protobuf boundary, not operator semantics. For
each node it:

1. looks up every same-named overload in the linked `onnx` Mod;
2. binds tensor inputs, compiler parameters, attributes, and declaration
   defaults by the overload signature;
3. selects the unique type-compatible overload; and
4. asks the ordinary compiler evaluator for the result Type.

There is no `op_type` dispatch chain and no native shape formula. Shape
functions such as `conv_shape`, `reshape_shape`, and `flatten_shape` are
ordinary compiler fns in `mods/onnx/mod.joggle`; their callers mark the single
Residual-to-compiler boundary with `@`. Adding a node
kind normally means adding a source fn declaration or body; the decoder changes
only when the serialized ONNX boundary itself gains a new representation.

`Constant` is the payload materialization boundary. Initializer bytes and the
original model are content-addressed Mod data. INT64 tensors used where a fn
signature expects `int` or `list<int>` are lifted to compiler values; this is
type-directed rather than operator-directed.

## Accepted reference profiles

The importer currently admits two exact, statically shaped profiles:

- ONNX IR 3 / `ai.onnx` opset 7 FLOAT inference;
- ONNX IR 7 / `ai.onnx` opset 13 QDQ inference.

Both reject non-standard node domains, symbolic dimensions, external and
sparse tensor storage, subgraphs, training data, unknown attributes, and calls
that do not match exactly one linked declaration. IR 3 initializers must agree
with their graph-input declarations; IR 7 initializers may not masquerade as
runtime inputs. Intermediate metadata and graph outputs are checked against
the Types inferred from the source signatures.

This is a verified vertical slice, not a claim of general ONNX coverage. A
rank-two `MatMul` now has a real `map + reduce + indexing + scalar arithmetic`
body. `Relu`, `Dropout`, and the QDQ wrappers are also bodyful. Conv, Pool,
Concat, Reshape, Flatten, batched/broadcast MatMul, and Softmax remain semantic
leaves or unsupported shapes and still need tensor-calculus bodies before
generic model fusion is established.

## Build and reference models

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

The fetch commands use the ONNX Model Zoo state at commit
`4c46cd00fbdb7cd30b6c1c17ab54f2e1f4f7b177` and reject digest mismatches.

| Model | SHA-256 |
| --- | --- |
| `squeezenet1.1-7.onnx` | `1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088` |
| `squeezenet1.0-13-qdq.onnx` | `4a567dd7542ef440890d57268fabf47211174c593d7a1837bd7f16a1067169e7` |

The vendored official schema provenance is recorded in
`mods/onnx/third_party/README.md`. Generated protobuf C++ stays in the build
tree. The native library is installed beside `mod.joggle` as a normal Mod
bundle component.

## Evidence

The FLOAT model imports as one `f32[1,3,224,224]` argument, 52 typed constants,
66 semantic calls, and one `f32[1,1000]` result. All 118 calls retain stable
source locations. The original model and 53 initializer payloads occupy 54
distinct content-addressed entries. Inference `Dropout` remains an ordinary
bodyful identity call and is removed only by generic inlining.

The QDQ model imports as one FLOAT argument, 228 constants, and 171 calls:

- 39 `onnx.QuantizeLinear`;
- 91 `onnx.DequantizeLinear`;
- 26 `onnx.Conv`;
- 3 `onnx.MaxPool`;
- 8 `onnx.Concat`;
- one each of `GlobalAveragePool`, `Reshape`, `Flatten`, and `Softmax`.

The two QDQ wrappers are bodyful ONNX fns that call the independent `quant`
semantics. The imported model consequently records `onnx` and `tensor` as
direct dependencies while `quant` remains transitive through `onnx`.

A separate structural test specializes the generic rank-two `MatMul` at
`f32[2,4] x f32[4,3]`. Its materialized body contains an output domain `map`, a
K-domain `map`, ordered Tensor `reduce`, coordinate construction/projection,
two indexed reads, and
ordinary scalar `*`/`+` calls. Both nested lambdas are verified Fns. This proves
body visibility and generic lexical specialization, not numerical execution or
the full ONNX broadcasting contract.

## Optional differential validation

With Python, NumPy, ONNX, and ONNX Runtime available:

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

The test-only executable reconstructs ONNX from the imported Fn, specialized
callee bindings, result Types, and Mod data, then compares the original and
reconstructed models with graph optimization disabled. It is not a production
emitter or a second graph representation.

## Bundle workflow

`joggle run`, `check`, `install`, and `lock` preserve the model and all payload
data. A driver must load the complete source dependency closure:

```bash
joggle run driver.joggle read squeezenet1.1-7.onnx \
  --with /path/to/arith/mod.joggle \
  --with /path/to/tensor/mod.joggle \
  --with /path/to/quant/mod.joggle \
  --with /path/to/onnx/mod.joggle \
  --load-native onnx=/path/to/onnx/native \
  -o squeezenet-bundle
```

See [Bundles](../design/0006-bundles.md) for the lossless repository contract
and [Tensor](tensor.md) for the semantic calculus that ONNX bodies must target.
