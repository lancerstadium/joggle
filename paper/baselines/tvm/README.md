# TVM matched baseline

This directory contains comparison implementations for the frozen contracts in
[`extension-tasks.json`](../../extension-tasks.json). It is experiment code,
not a Joggle dependency.

The baseline is pinned to Apache TVM tag `v0.26.0`, commit
`c7b458e946bc4266915da582457476bdcd9705ae`. The checked tasks use documented
TVMScript/TIR functions and schedule primitives, emit C through TVM, compile
the artifact, execute it, and check the shared fixture. They require Python
3.10+ with NumPy and the TVM Python package backed by that exact source
revision. The following source-only build is sufficient; LLVM and RPC are not
used by these tasks.

```sh
git clone --recursive --branch v0.26.0 \
  https://github.com/apache/tvm.git "$TVM_ROOT"
test "$(git -C "$TVM_ROOT" rev-parse HEAD)" = \
  c7b458e946bc4266915da582457476bdcd9705ae
cmake -S "$TVM_ROOT" -B "$TVM_ROOT/build-make" \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_CPP_RPC=OFF \
  -DUSE_LLVM=OFF \
  -DUSE_METAL=OFF \
  -DUSE_RANDOM=OFF \
  -DUSE_RPC=OFF
cmake --build "$TVM_ROOT/build-make" --parallel
python3 -m venv "$TVM_ROOT/.venv"
"$TVM_ROOT/.venv/bin/python" -m ensurepip
"$TVM_ROOT/.venv/bin/python" -m pip install \
  -r paper/baselines/tvm/requirements.txt
```

With `TVM_ROOT` set to the pinned checkout and a source build under
`$TVM_ROOT/build-make`:

```sh
TVM_LIBRARY_PATH="$TVM_ROOT/build-make/lib" \
PYTHONPATH="$TVM_ROOT/python" \
"$TVM_ROOT/.venv/bin/python" \
paper/baselines/tvm/implementation.py \
paper/tasks/implementation.json

TVM_LIBRARY_PATH="$TVM_ROOT/build-make/lib" \
PYTHONPATH="$TVM_ROOT/python" \
"$TVM_ROOT/.venv/bin/python" \
paper/baselines/tvm/policy.py \
paper/tasks/policy.json

TVM_LIBRARY_PATH="$TVM_ROOT/build-make/lib" \
PYTHONPATH="$TVM_ROOT/python" \
"$TVM_ROOT/.venv/bin/python" \
paper/baselines/tvm/external_kernel.py \
paper/tasks/external-kernel.json \
examples/edge/kernel.c \
examples/edge/main.c

TVM_ROOT="$TVM_ROOT" \
TVM_LIBRARY_PATH="$TVM_ROOT/build-make/lib" \
PYTHONPATH="$TVM_ROOT/python" \
"$TVM_ROOT/.venv/bin/python" \
paper/measure_baselines.py \
--record paper/baselines/tvm/record.json \
--contracts paper/extension-tasks.json \
--repo . \
--output paper/data/extension-tvm-pilot.csv
```

The three passing Python programs and the external-kernel C bridge and header are
baseline implementation source. This README and `requirements.txt` are
reproducibility metadata. JSON fixtures, the supplied kernels, and the supplied
harness are shared task inputs and are excluded from source-footprint
measurements.

## Numeric-format unsupported boundary

The frozen numeric-format task first requires one registered parametric scalar
type `sat<W>` for widths 2 through 63. At the pinned revision, importing the
previous custom-datatype registration surface from `tvm.target` fails, the
corresponding `python/tvm/target/datatype.py` source is absent, and constructing
`DataType("custom[sat]5")` fails because `dtype.get_custom_type_code` is not
registered. [`numeric_format_probe.py`](numeric_format_probe.py) reproduces
these checks against the shared contract and format map;
[`numeric-format/result.json`](numeric-format/result.json) preserves the exact
revision, diagnostics, and digests.

The later storage, C, VM, and rejection requirements are not counted after the
first mandatory requirement fails. Implementing saturation over ordinary
`int64` TIR would be a useful operator program, but would not satisfy the
contract's type-system requirement and is therefore not substituted here.
