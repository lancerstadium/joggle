# Joggle evaluation artifact

This directory contains the executable evidence path for Section 4. The frozen
design is in [`PROTOCOL.md`](PROTOCOL.md). Raw measurements belong under
`local/cache/artifact/`; Git tracks contracts, collectors, validators, and plots.

## Start here

From the repository root, build and check the current source:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
python3 artifact/validate_extension_specs.py
python3 artifact/validate_benchmark_cases.py
python3 -m pip install -r artifact/requirements.txt
python3 artifact/verify_exports.py
```

The build requires CMake 3.20+, a C++20 compiler and Python 3.11+ for the
validation scripts. Export validation uses NumPy and Matplotlib, shared with
the figure renderers. It reads the checked-in datasets and reference sources;
it does not run models, call an Agent API, or overwrite figures. To also inspect
the retained local measurement files, run `python3 artifact/verify_exports.py --raw`.

`build/` is the current executable tree. `local/legacy-builds/` contains retained
historical outputs, not a second build entry point. Downloaded models, local
Python environments and original raw records are excluded from Git; the
[local workspace map](../local/README.md) describes their locations. A source
commit alone is not a bundle of those inputs. Optional ONNX/TFLite dependencies
and the full measurement commands are described below.

## Evidence layout

| Reported evidence | Input CSV under `paper/data/` | Renderer |
| --- | --- | --- |
| Native extension size | `extension-size.csv` | `paper/render_extensions.py` |
| Change footprint | `package-footprint.csv` | `figure_05_footprint.py --packages` |
| Cross-system update | `figure-06-update.csv` | `paper/render_update.py` |
| Operator execution | `figure-07-operators.csv` | `figure_07_performance.py` |
| Model execution | `figure-07-models.csv` | `figure_07_models.py` |

Build all reported figures and both PDFs with `make -C paper`.
There are four evaluation sections; execution has separate operator and model
displays. Agent collection is stopped at 42 complete trajectories out of 72
planned conditions, with no full-task completion. Its diagnostic CSV is
`paper/data/agent-diagnostics.csv`; Appendix C separates those candidates from
the checked reference programs used for source-size measurements. The detailed guides retain the collection protocol and distinguish it from the
reported snapshot.

The model companion display accepts matched native runs from Joggle, ORT,
TVM, and ONNX-MLIR through `figure_07_models.py --models`. The base-path
ablation is optional. Assembly checks each run's provenance, numerical oracle,
input identity, thread controls, and complete model population before plotting.
The common-set aggregate uses only models correct in every selected system;
the per-model CSV retains absolute medians and p95 values even when the ORT
reference fails validation. Operator and model figures share one canvas size.

## Build and inputs

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

cmake -DOUT=local/cache/onnx-zoo -P test/tools/fetch_onnx_zoo.cmake
python3 artifact/generate_benchmark_inputs.py \
  --output local/cache/artifact/benchmark-inputs
python3 artifact/generate_operator_models.py \
  --inputs local/cache/artifact/benchmark-inputs \
  --output local/cache/artifact/operator-models \
  --verify-runtime
```

The generators validate hashes and numerical fixtures before collection.

## Detailed procedures

| Procedure | Guide |
| --- | --- |
| Reference implementations and optional Agent collection | [Extensions](docs/extensions.md) |
| Package integration and maintenance | [Change footprint](docs/footprint.md) |
| Executable-ready updates and retained-graph scheduling | [Compiler updates](docs/updates.md) |
| Operator/model measurements and cross-system comparisons | [Execution](docs/execution.md) |

## Directory responsibilities

| Path | Purpose |
| --- | --- |
| `manifests/`, `schemas/`, `templates/` | Workload definitions, record contracts, and CSV headers |
| `extensions/` | Task inputs, starters, reference implementations, and API cards |
| `mods/` | Workloads used by the artifact executables |
| `figures/` | Renderers shared with the paper |
| `docs/` | Detailed collection procedures, linked above |
| `../paper/data/` | The reported dataset snapshot |
| `../local/cache/artifact/` | Machine-local raw records and generated outputs |

The public entry point is this README. The protocol records experimental
choices; the individual guides explain execution. Historical build trees are
not required to build the current source and are not included in the Git tag.
