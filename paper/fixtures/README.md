# Matched ONNX fixtures

These small models are experimental inputs, not deployment benchmarks. They
encode the exact values and shapes from the frozen extension contracts so
Joggle and system-level baselines receive identical ONNX programs and numerical
oracles. Normal-network compatibility and performance remain separate ONNX
Model Zoo studies.

`implementation/` contains the rank-two MatMul contract:

- `model.onnx`: opset-13 `MatMul` with inputs `a: [2, 3]` and `b: [3, 2]`;
- `test_data_set_0/`: serialized ONNX TensorProto inputs and expected output;
- `manifest.json`: generator, dependency versions, contract digest, and file
  digests.

`policy/` contains the fixed four-input chain from the policy contract. Its
three opset-13 `Add` calls expose three elementwise loop bodies before a
system-specific fusion policy runs. Both the permissive and rejecting policy
cases consume this unchanged model and the same four-element numerical oracle.

`numeric-format/` separates a standard interchange graph from its experimental
type policy. `model.onnx` contains five ordinary opset-13 `int64` Add nodes:
four scalar cases and one length-four tensor case. `format-map.json` assigns a
single `sat<W>` width to each named node; `int64` is an interchange carrier,
not a prescribed target storage type. Every system receives these same two
inputs and must implement the changed arithmetic through its documented
extension path. The stored outputs are the saturating-format oracle, not the
ordinary ONNX Add result; the generator checks both that the oracle follows the
frozen width rule and that normal ONNX evaluation differs from it.

`evolution/` contains the two immutable inputs for the sequential vertical
study. Both are ordinary opset-13 `Conv -> Add -> Relu` graphs with embedded
OIHW weights and broadcast bias. The explicit `Add` keeps the S0 external-Conv
boundary neutral across systems that represent optional Conv bias differently.
`eligible/` has four input channels and can use the later
`OIHW2` target representation; `fallback/` has three input channels and must
remain portable after that revision. Raw weight and bias TensorProtos are
stored next to each model so the payload transformation can be audited without
treating a prepacked buffer as an input. Stage S1 and S2 never regenerate these
files. `input.bin` and `expected.bin` are byte-identical raw float payloads for
the generated C harness; the TensorProto copies remain the interchange oracle.

Regenerate or verify it from the repository root with Python 3.11 or 3.12 in an
isolated environment:

```sh
python3.12 -m venv .venv-fixtures
.venv-fixtures/bin/python -m pip install -r paper/fixtures/requirements.txt
.venv-fixtures/bin/python paper/fixtures/generate.py
.venv-fixtures/bin/python paper/fixtures/generate.py --check
.venv-fixtures/bin/python paper/fixtures/generate.py --fixture policy
.venv-fixtures/bin/python paper/fixtures/generate.py --fixture policy --check
.venv-fixtures/bin/python paper/fixtures/generate.py --fixture numeric-format
.venv-fixtures/bin/python paper/fixtures/generate.py --fixture numeric-format --check
.venv-fixtures/bin/python paper/fixtures/generate.py --fixture evolution
.venv-fixtures/bin/python paper/fixtures/generate.py --fixture evolution --check
```

The dependencies are generation-only. Building or running Joggle does not
require Python, NumPy, or the ONNX Python package. A build configured with
`JOGGLE_BUILD_ONNX=ON` verifies the committed hashes and runs
`onnx-matched-implementation` through Joggle's ONNX importer, semantic
conversion, user-defined `ikj` implementation, deterministic VM, generated C,
and the same TensorProto oracle.
