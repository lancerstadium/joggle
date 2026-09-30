# Operator and model execution

[Evaluation entry point](../README.md). Run all commands from the repository root.

Run each backend for operators and models. The Joggle base and optimized paths
differ only in optional optimization stages; required conversion, memory
planning, and emission remain in both.

```sh
python3 artifact/run_joggle_benchmarks.py \
  --group operators --variant joggle-optimized \
  --inputs local/cache/artifact/benchmark-inputs \
  --operator-models local/cache/artifact/operator-models \
  --joggle build/joggle --builtin-mods build/modules \
  --output local/cache/artifact/operators-joggle-opt.csv

python3 artifact/run_baseline_benchmarks.py --backend onnxruntime \
  --group operators \
  --inputs local/cache/artifact/benchmark-inputs \
  --operator-models local/cache/artifact/operator-models \
  --output local/cache/artifact/operators-ort.csv
```

The same external-baseline collector also imports ONNX through TVM Relax and
compiles it with the default LLVM CPU pipeline:

```sh
python3 artifact/run_baseline_benchmarks.py --backend tvm \
  --group operators \
  --inputs local/cache/artifact/benchmark-inputs \
  --operator-models local/cache/artifact/operator-models \
  --target-json '{"kind":"llvm","num-cores":1}' \
  --output local/cache/artifact/operators-tvm.csv
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
  --inputs local/cache/artifact/benchmark-inputs \
  --operator-models local/cache/artifact/operator-models \
  --output local/cache/artifact/operators-onnx-mlir.csv
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
  --operators local/cache/artifact/operators-joggle-base.csv \
    local/cache/artifact/operators-joggle-opt.csv \
    local/cache/artifact/operators-ort.csv local/cache/artifact/operators-tvm.csv \
  --variants joggle-unoptimized joggle-optimized onnxruntime tvm-relax-llvm \
  --allow-partial --output local/cache/artifact/operators-cross-system.csv
```

This operator-only export is partial relative to the full operator-and-model
population. For the complete export, provide all four model runs through
`--models` and omit `--allow-partial`. The assembler checks native sampling and
input protocols as well as CSV identities. The operator plot includes each
supported plotting configuration present in the export, including TVM, against
the shared ORT reference. The model plot still requires matched base, optimized,
and ORT runs.

Repeat the Joggle command for `joggle-unoptimized`; repeat the variants
with `--group models --model-root local/cache/onnx-zoo`. Then assemble the one figure
file:

```sh
python3 artifact/merge_benchmark_rows.py \
  --operators \
    local/cache/artifact/operators-joggle-base.csv \
    local/cache/artifact/operators-joggle-opt.csv \
    local/cache/artifact/operators-ort.csv \
  --models \
    local/cache/artifact/models-joggle-base.csv \
    local/cache/artifact/models-joggle-opt.csv \
    local/cache/artifact/models-ort.csv \
  --output local/cache/artifact/figure-07-performance.csv

python3 artifact/validate_figure.py 7 \
  local/cache/artifact/figure-07-performance.csv
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
  local/cache/artifact/figure-07-performance.csv \
  --output local/cache/artifact/figure-07-performance.pdf \
  --summary local/cache/artifact/figure-07-summary.csv
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

Both plotting scripts use a 3.33 × 2.25 inch author-review canvas for six panels,
with the same default 5.5 pt font and boxed three-column, two-row layout.
The shared series encodings cover Joggle base/opt, TVM, and ONNX-MLIR;
ORT supplies the reference line. A cross-system source may omit the base
ablation, but it must retain the complete model population for every included
system. The aggregate uses the intersection of correct cases across all
included systems. The summary JSON records that population explicitly.

For an audited combined operator/model export, produce companion previews
without changing the manuscript's selected measurement snapshot:

```sh
python3 artifact/figures/figure_07_performance.py PATH/combined.csv \
  --kind operator --output PATH/preview/operators.pdf \
  --summary PATH/preview/operators.csv
python3 artifact/figures/figure_07_models.py PATH/combined.csv \
  --output PATH/preview/models.pdf --summary PATH/preview/models.csv
```

The source CSV and its merge record retain individual samples, compiler
identities, and input hashes. Exported summary CSVs retain absolute medians,
p95, normalized values, sample counts, and failure reasons. A cross marks an
invalid candidate; a dash marks a correct candidate without a valid ORT
reference (or an empty aggregate); a question mark marks an absent entry.
None is encoded as a zero latency. Axis limits include both medians and p95,
including values below parity. Whiskers show timing spread, not confidence
intervals. Change the manuscript's figures, captions, and numeric results
together when selecting a new measurement snapshot.

The existing manuscript snapshot has the following model population:
Five panels group all 15 models; the sixth shows the geometric means of the
eight jointly correct models. Bars show medians and whiskers extend to p95;
the aggregate has no timing whisker. Failure reasons remain in the summary
CSV. Absolute milliseconds remain in the exported summary
CSV and the main-text model discussion. Small authoring fonts are configurable; final submission typography
must be checked against the venue's figure-text requirements.
