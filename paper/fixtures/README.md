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

Regenerate or verify it from the repository root with Python 3.11 or 3.12 in an
isolated environment:

```sh
python3.12 -m venv .venv-fixtures
.venv-fixtures/bin/python -m pip install -r paper/fixtures/requirements.txt
.venv-fixtures/bin/python paper/fixtures/generate.py
.venv-fixtures/bin/python paper/fixtures/generate.py --check
.venv-fixtures/bin/python paper/fixtures/generate.py --fixture policy
.venv-fixtures/bin/python paper/fixtures/generate.py --fixture policy --check
```

The dependencies are generation-only. Building or running Joggle does not
require Python, NumPy, or the ONNX Python package. A build configured with
`JOGGLE_BUILD_ONNX=ON` verifies the committed hashes and runs
`onnx-matched-implementation` through Joggle's ONNX importer, semantic
conversion, user-defined `ikj` implementation, deterministic VM, generated C,
and the same TensorProto oracle.
