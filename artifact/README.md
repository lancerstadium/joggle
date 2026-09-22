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

Validate the task contract, execute the native task oracles, and then collect
model/system trajectories for assembly into one CSV:

```sh
python3 artifact/validate_extension_specs.py
```

`run_extension_task.py` executes candidate code against the shared contract.
The native tasks include `ana-broadcast-shape`, `ana-storage-cost`,
`ana-numeric-range`, `ana-fusion-match`, `emit-storage-plan`, and
`emit-target-capability`.
Each task directory under `extensions/` contains reference implementations for
Joggle, MLIR, and xDSL; the shared `starter.*` files contain their empty entry
points. Attribute-analysis tasks read a request from function metadata in
Joggle or a builtin module dictionary in MLIR and xDSL. The driver
only parses the input and calls the candidate. Shape analysis runs in native
extension code, and expected outputs remain in the external oracle.

```sh
python3 artifact/run_extension_task.py \
  --task ana-broadcast-shape --system Joggle \
  --source artifact/extensions/ana-broadcast-shape/reference.jog \
  --joggle build/joggle --builtin-mods build/modules \
  --build-root .cache/artifact/extension-tasks \
  --output .cache/artifact/extension-reference-joggle.json
```

For MLIR, pass `--system MLIR`, a `.cpp` source, and `--mlir-dir` pointing to
the pinned build's `lib/cmake/mlir`. For xDSL, pass `--system xDSL`, a `.py`
source, and `--xdsl-python` pointing to the pinned environment's interpreter.
Run each reference and its empty starter: the reference must pass every case,
and the starter must fail. Reports contain per-case process output and exact
oracle comparisons. They are task-validation records, not agent trajectories.
Candidate code from an agent runs inside its isolated execution environment.

The attribute tasks include numeric range propagation through binary64 compiler
metadata, with fractional and negative inputs. `ana-fusion-match` instead
receives a real typed SSA call graph in all three systems, with no request
dictionary. Its eight cases exercise NCHW/NHWC layout, an interleaved unrelated
call, extra consumers on either fusion edge, bias rank and channel mismatch,
and an unmatched activation. The extension reads tensor types, defining
operations, and use lists through each framework's native APIs. Match positions
refer to the actual call sequence, not a precomputed input descriptor.
Emission oracles also require byte-identical outputs across repeated runs.
These reference checks validate task execution, not agent completion rates.

The 24-task native harness, demonstrations, and agent collector must be
completed before collecting the full trajectory matrix. The assembler consumes
those provider records:

```sh
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
`update-assembly/v1` provenance file. Matched providers identify their workload
as `compiler-pipeline/v1` and count `subject-operation-visits`.

`run_reactive.py` currently runs a metadata-propagation diagnostic over model
topologies. Its five stages construct derived dictionaries; they do not lower
operators or allocate storage. Its provider record identifies
`metadata-propagation/v1`, which the Figure 6 assembler and final release gate
reject. Raw diagnostics keep evaluator instruction counts separate from graph
visits. For this topology-preserving diagnostic, graph visits equal the
traversed operation-list size times the number of executed stages; counting
is outside the timed region. Older rows without that counter cannot be reused
as graph-visit measurements.

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

python3 artifact/run_baseline_benchmarks.py --backend onnxruntime \
  --group operators \
  --inputs .cache/artifact/benchmark-inputs \
  --operator-models .cache/artifact/operator-models \
  --output .cache/artifact/operators-ort.csv
```

The same external-baseline collector also imports ONNX through TVM Relax and
compiles it with the default LLVM CPU pipeline:

```sh
python3 artifact/run_baseline_benchmarks.py --backend tvm \
  --group operators \
  --inputs .cache/artifact/benchmark-inputs \
  --operator-models .cache/artifact/operator-models \
  --target-json '{"kind":"llvm","num-cores":1}' \
  --output .cache/artifact/operators-tvm.csv
```

Install TVM and ONNX Runtime in this collector's environment. For a source
build, set `PYTHONPATH` to its `python/` directory and `TVM_LIBRARY_PATH` to the
directory containing its shared libraries. The run record captures the TVM
source revision and dirty state, loaded library hashes, FFI version, resolved
target, and default untuned pipeline. Inputs are bound before measurement;
output copies and the common semantic oracle run after measurement. Import,
compilation, and input binding are recorded separately as preparation
diagnostics, not as responsive-update measurements. CPU execution is
synchronous, and TVM's thread pool is fixed to the manifest's thread count.

ONNX-MLIR uses the same cases and oracle through its native C ABI:

```sh
python3 artifact/run_baseline_benchmarks.py --backend onnx-mlir \
  --onnx-mlir /path/to/onnx-mlir/build/Release/bin/onnx-mlir \
  --group operators \
  --inputs .cache/artifact/benchmark-inputs \
  --operator-models .cache/artifact/operator-models \
  --output .cache/artifact/operators-onnx-mlir.csv
```

The adapter specializes input shapes to the fixed benchmark inputs and compiles
with `-O3`, parallel execution disabled, and fast math disabled. Inputs remain
resident across calls. Timing includes the native entry point, output allocation,
and release of the previous output list; output copies and numerical comparisons
are outside the timed region. The run record stores the compiler version, source
revision, compiler/runtime hashes, flags, and separate preparation timings.
Set `ONNX_MLIR_BIN` when running `test/benchmark_oracle.py` to exercise dynamic
input specialization, multiple outputs, and native buffer lifetime. Add
`onnx-mlir-llvm` explicitly to `--variants` when merging these measurements.

Use `--smoke` to validate integration before formal timing. Smoke records are
not release eligible. Run backends sequentially on an otherwise idle host.
Include TVM in the shared source-data export by explicitly selecting the four
configurations. The workload manifest and its input hashes remain unchanged:

```sh
python3 artifact/merge_benchmark_rows.py \
  --operators .cache/artifact/operators-joggle-base.csv \
    .cache/artifact/operators-joggle-opt.csv \
    .cache/artifact/operators-ort.csv .cache/artifact/operators-tvm.csv \
  --variants joggle-unoptimized joggle-optimized onnxruntime tvm-relax-llvm \
  --allow-partial --output .cache/artifact/operators-cross-system.csv
```

This operator-only export is partial relative to the full operator-and-model
population. For the complete export, provide all four model runs through
`--models` and omit `--allow-partial`. The assembler checks native sampling and
input protocols as well as CSV identities. The operator plot includes each
supported plotting configuration present in the export, including TVM, against
the shared ORT reference. The model plot still requires matched base, optimized,
and ORT runs.

Repeat the Joggle command for `joggle-unoptimized`; repeat the variants
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

The Joggle collector checkpoints complete cases. External baselines publish
their CSV and run record when the selected cases finish. Both retain failed
cases explicitly, with diagnostics in the run record or failure log.
Only steady-state execution is repeated. A smoke run uses three iterations;
the release population uses the 100 iterations frozen in the manifest.
The numerical oracle runs in a fresh process after timing and exchanges typed
arrays through a temporary archive. Candidate runtimes and the semantic ORT
session therefore do not share process-local state. Run records identify this
boundary as `correctness_execution: isolated-process`; graph optimization,
input bytes, and numerical tolerances remain fixed by the shared protocol.

The operator display uses three columns and two rows of compact panels at
single-column manuscript width, with four operators per family. Every panel
has a complete box and inward ticks on all four sides, with one shared
logarithmic scale. Paired bars extend from the ORT parity line to each
Joggle median; upper whiskers reach p95, using that subject's ORT median as
the common denominator. Hatching distinguishes the base path in grayscale.
Whiskers show timing variation, not confidence intervals. Failed and missing
cases remain explicit; the caption reports correct coverage. Export the
plotted statistics alongside the PDF:

```sh
python3 artifact/figures/figure_07_performance.py \
  .cache/artifact/figure-07-performance.csv \
  --output .cache/artifact/figure-07-performance.pdf \
  --summary .cache/artifact/figure-07-summary.csv
```

`merge_benchmark_rows.py --allow-partial` supports intermediate, hash-checked
snapshots, including operator-only data. Their merge record has
`complete: false`; the complete release still requires the full model matrix.
The operator figure uses `paper/data/figure-07-operators.csv`: 9,402 rows for
Joggle revision `5a71fe55a3be`, ORT, and default TVM. The main-text summary and
Appendix A additionally use `paper/data/onnx-mlir-operators.csv` and its
hash-bound run record. These 2,202 additional rows cover ONNX-MLIR alone;
they do not duplicate measurements in the figure source. Across the four
systems, the common correctness population is 22 operators. QLinearConv is
unsupported by both external compilers; QLinearMatMul is unsupported by TVM
and fails the numerical oracle in ONNX-MLIR. Both Joggle paths and ORT pass
all 24. The table distinguishes all-24 and common-22 geometric means.

The authoring exports in `paper/data/` contain separate operator and model
CSVs and their per-case summaries. The model export contains all 15 subjects
at Joggle revision `40fc928`, with the matched ORT run collected at `becdc24`.
Input/model hashes and measurement settings match across the three runs;
the base and optimized Joggle runs use the same compiler binary. Eleven models
pass all three paths. Four Joggle failures remain explicit in the plot and CSV.

```sh
python3 artifact/figures/figure_07_performance.py \
  paper/data/figure-07-operators.csv \
  --output paper/figures/figure-07-performance.pdf \
  --summary paper/data/figure-07-operators-summary.csv

python3 artifact/figures/figure_07_models.py \
  paper/data/figure-07-models.csv \
  --output paper/figures/figure-07-models.pdf \
  --summary paper/data/figure-07-models-summary.csv
```

The model companion uses the same boxed three-column, two-row bar layout.
Five panels group all 15 models; the sixth shows the geometric means of the
11 jointly correct models. Bars show medians and whiskers extend to p95;
the aggregate has no timing whisker. `×C` and `×N` distinguish preparation
and numerical failures. Absolute milliseconds remain in the exported summary
CSV and the main-text model discussion. Small authoring fonts are configurable; final submission typography
must be checked against the venue's figure-text requirements.

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
