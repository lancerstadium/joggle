# Joggle evaluation artifact

This directory contains the executable evidence path for Section 4. The frozen
design is in [`PROTOCOL.md`](PROTOCOL.md). Raw measurements belong under
`.cache/artifact/`; Git tracks contracts, collectors, validators, and plots.

## Evidence layout

| Figure | Input CSV | Plot |
| --- | --- | --- |
| 4 · extension completion | `figure-04-extension.csv` | `figure_04_extension.py` |
| 5 · change footprint | `figure-05-footprint.csv` | `figure_05_footprint.py` |
| 6 · cross-system update | `figure-06-update.csv` | `figure_06_update.py` |
| 7 · end-to-end performance | `figure-07-performance.csv` | `figure_07_performance.py` |

The release path contains only the four claim-facing figures above. Figure 7
combines operator and model results.

## Build and inputs

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

cmake -DOUT=.cache/onnx-zoo -P test/tools/fetch_onnx_zoo.cmake
python3 artifact/generate_benchmark_inputs.py \
  --output .cache/artifact/benchmark-inputs
python3 artifact/generate_operator_models.py \
  --inputs .cache/artifact/benchmark-inputs \
  --output .cache/artifact/operator-models \
  --verify-runtime
```

The generators validate hashes and numerical fixtures before collection.

## Figure 4

Validate the task contract, run the frozen coding-agent harness for every
model/system provider, and assemble one CSV:

```sh
python3 artifact/validate_extension_specs.py
python3 artifact/merge_agent_rows.py \
  .cache/artifact/agent-model-a-joggle.csv \
  .cache/artifact/agent-model-a-mlir.csv \
  .cache/artifact/agent-model-a-xdsl.csv \
  .cache/artifact/agent-model-b-joggle.csv \
  .cache/artifact/agent-model-b-mlir.csv \
  .cache/artifact/agent-model-b-xdsl.csv \
  --output .cache/artifact/figure-04-extension.csv
```

Each provider CSV contains all 24 tasks, zero- and two-demonstration contexts,
and ten seeded trajectories. Its adjacent `agent-provider/v1` record pins the
model, system revision, API card, demonstrations, action/token budgets,
workspace image, oracle commands, and complete trajectory hashes. The release
matrix contains 2,880 rows and rejects a missing task, seed, or provider.

## Figure 5

Each task starts from a passing candidate patch. Reduce it, pin the resulting
revision and logs in the case file, then collect the frozen coordinates:

```sh
python3 artifact/minimize_patch.py \
  --repo /path/to/system --base BASE --head CANDIDATE \
  --output-patch .cache/artifact/task.patch \
  --oracle-log .cache/artifact/task-oracle.log \
  --log .cache/artifact/task-minimization.json \
  -- ./task-oracle

python3 artifact/collect_footprint.py \
  --cases .cache/artifact/footprint-cases.csv \
  --output .cache/artifact/figure-05-footprint.csv
```

## Figure 6

The shared CSV pairs `full` and `update` for Joggle, MLIR, and xDSL on all 15
models.

```sh
python3 artifact/run_reactive.py \
  --model-manifest artifact/manifests/reactive-models.csv \
  --model-root .cache/onnx-zoo \
  --output .cache/artifact/update-joggle.csv \
  --build-root .cache/artifact/update-joggle-build

python3 artifact/run_joggle_lowering_profile.py \
  --model-root .cache/onnx-zoo \
  --joggle build/joggle \
  --builtin-mods build/modules \
  --output .cache/artifact/update-joggle-production.csv

python3 artifact/merge_update_rows.py \
  .cache/artifact/update-joggle.csv \
  .cache/artifact/update-joggle-production.csv \
  .cache/artifact/update-mlir.csv \
  .cache/artifact/update-xdsl.csv \
  --output .cache/artifact/figure-06-update.csv

python3 artifact/validate_reactive.py \
  .cache/artifact/figure-06-update.csv
python3 artifact/figures/figure_06_update.py \
  .cache/artifact/figure-06-update.csv \
  --output .cache/artifact/figure-06-update.pdf
```

The production and end-to-end collectors both derive the entry signature from
`benchmark-cases.json`. `opt.signature` binds named shape parameters and
refines anonymous input extents before ONNX conversion, so both figures compile
the same fixed workload rather than separate model variants.
The production collector batches inference, conversion, and preparation and
uses the runtime timing report to split these stages. Scalar lowering, storage
planning, and placement retain materialized boundaries; their wall times
include loading and writing the intermediate graph.

MLIR and xDSL adapters must implement the same edit, five logical stages,
counters, and digest. The release gate rejects Figure 6 without the complete
`update-assembly/v1` provenance file.

## Figure 7

Run each backend for operators and models. The Joggle base and optimized paths
differ only in optional optimization stages; required conversion, memory
planning, and emission remain in both.

```sh
python3 artifact/run_joggle_benchmarks.py \
  --group operators --variant joggle-optimized \
  --inputs .cache/artifact/benchmark-inputs \
  --operator-models .cache/artifact/operator-models \
  --joggle build/joggle --builtin-mods build/modules \
  --output .cache/artifact/operators-joggle-opt.csv

python3 artifact/run_onnxruntime_benchmarks.py \
  --group operators \
  --inputs .cache/artifact/benchmark-inputs \
  --operator-models .cache/artifact/operator-models \
  --output .cache/artifact/operators-ort.csv
```

Repeat the Joggle command for `joggle-unoptimized`; repeat all three variants
with `--group models --model-root .cache/onnx-zoo`. Then assemble the one figure
file:

```sh
python3 artifact/merge_benchmark_rows.py \
  --operators \
    .cache/artifact/operators-joggle-base.csv \
    .cache/artifact/operators-joggle-opt.csv \
    .cache/artifact/operators-ort.csv \
  --models \
    .cache/artifact/models-joggle-base.csv \
    .cache/artifact/models-joggle-opt.csv \
    .cache/artifact/models-ort.csv \
  --output .cache/artifact/figure-07-performance.csv

python3 artifact/validate_figure.py 7 \
  .cache/artifact/figure-07-performance.csv
```

Collectors checkpoint complete cases and record unsupported cases explicitly.
Only steady-state execution is repeated. A smoke run uses three iterations;
the release population uses the 100 iterations frozen in the manifest.

Figure 7 retains one row per operator and model. Points show medians; segments
extend to p95, with both divided by the same subject's ORT median. These
segments show timing variation. Failed cases remain visible in the margin and
coverage table. Export the plotted statistics alongside the PDF:

```sh
python3 artifact/figures/figure_07_performance.py \
  .cache/artifact/figure-07-performance.csv \
  --output .cache/artifact/figure-07-performance.pdf \
  --summary .cache/artifact/figure-07-summary.csv
```

`merge_benchmark_rows.py --allow-partial` supports intermediate, hash-checked
snapshots, including operator-only data. Their merge record has
`complete: false`; the complete release still requires the full model matrix.
The operator table in Appendix A uses revision `83aa8d4fc72d` and the
`figure-07-operators-83aa8d4.csv` snapshot. Its 7,200 rows cover all 24 operators,
both Joggle paths, and ONNX Runtime; it contains no model measurements.

## Render and release

Individual validators and plots never execute Joggle. Once all four CSVs and
their provenance records are present:

```sh
python3 artifact/check_release.py \
  --data-dir .cache/artifact/release-data \
  --output-dir .cache/artifact/release
```

The command validates all pairings and correctness gates, verifies hashes,
renders PDF and PNG figures, and publishes one `evaluation-release/v2`
manifest. It never overwrites an existing release directory.
