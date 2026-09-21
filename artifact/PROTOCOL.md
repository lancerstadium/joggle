# Evaluation protocol

Section 4 uses four figures. Each figure answers one paper claim, consumes one
CSV, and is rendered by one script. Repetitions estimate a subject; they do not
increase the number of independent tasks, patches, or models.

| Figure | Claim | Compared systems | Independent unit | Primary endpoint |
| --- | --- | --- | --- | --- |
| 4 | Convenient extension | Joggle, MLIR, xDSL | 24 extension tasks | executable pass@1 |
| 5 | Controllable change | Joggle, MLIR, xDSL | 12 matched patches | files, lines, zones, declarations |
| 6 | Efficient update | Joggle, MLIR, xDSL | 15 model graphs | Update/Full time and visited work |
| 7 | End-to-end performance | Joggle base/opt, ONNX Runtime | 24 operators and 15 models | steady-state latency and correct coverage |

The complete release contains 29,376 Figure 4 rows, 36 Figure 5 patch rows,
108,000 Figure 6 timing rows, and up to 11,700 Figure 7 timing rows. Repeated
rows estimate each independent task, patch, operator, or model; they are never
treated as additional independent subjects.

## Common controls

Pin revisions, model hashes, compiler flags, hardware, affinity, thread count,
and seeds before collection. Pair inputs and edits within each subject. Retain
unsupported cases as coverage observations; exclude them from latency ratios.
Every timed output must pass its semantic or numerical oracle. Form ratios
within a subject before aggregating across subjects.

## Figure 4 · extension completion

The suite has four tasks in each of six extension families: definition,
analysis, rewrite, conversion, emission, and vertical extension. Two pinned
1--3B code models receive an equivalent semantic specification and a compact,
system-specific API card. Each model/system/task condition uses 0, 1, 2, and 4
demonstrations and produces 50 samples.

Executable pass@1 is primary. pass@5 and pass@10 show sample-budget
sensitivity; reference-solution log-perplexity is secondary. The oracle reports
the first failed phase: parse, type, build, or semantic test.

- Contract: `manifests/extension-specs.json`
- Task index: `manifests/extension-tasks.csv`
- CSV: `templates/figure-04-extension.csv`
- Plot: `figures/figure_04_extension.py`

## Figure 5 · change footprint

Twelve tasks are selected before implementation, two per extension family.
Each system starts from a pinned clean revision. A passing patch is reduced to
a hunk-level fixed point, then measured under a frozen ownership-zone policy.
The four primary coordinates are changed implementation files, changed
implementation lines, ownership zones, and registry/build declarations.

Counts come from Git diffs rather than author logs. The collector binds the
base revision, final patch, minimization trace, oracle output, and policy by
hash.

- Case template: `templates/footprint-cases.csv`
- Policy template: `templates/footprint-policy.json`
- CSV: `templates/figure-05-footprint.csv`
- Plot: `figures/figure_05_footprint.py`

## Figure 6 · cross-system updates

Fifteen pinned ONNX models are decoded into a neutral typed graph that retains
operation kinds, values, types, and def-use edges. An adapter materializes this
graph in each system. Every system runs the same five logical stages: analysis,
canonicalization, target selection, memory planning, and artifact-manifest
construction.

For early, middle, and late edit sites, the experiment applies matched metadata
and value-type edits to an affected or unrelated location. `full` reruns all
five stages. `update` uses the public incremental mechanism available in the
system. The Cartesian matrix is 15 models x 3 systems x 3 sites x 2 edit
classes x 2 scopes x 2 policies x 100 iterations = 108,000 rows. The release
validator rejects a missing or additional cell. The primary measurements are:

\[
\mathrm{UpdateRatio}_s=T_{\mathrm{update},s}/T_{\mathrm{full},s},\qquad
\mathrm{WorkRatio}_s=V_{\mathrm{update},s}/V_{\mathrm{full},s}.
\]

Absolute edit-to-result latency remains visible. Internal scheduler policies
are implementation diagnostics and do not enter the paper comparison.

- Model index: `manifests/reactive-models.csv`
- CSV: `templates/figure-06-update.csv`
- Validator: `validate_reactive.py`
- Assembler: `merge_update_rows.py`
- Plot: `figures/figure_06_update.py`

The Joggle adapter exists in `reactive.cpp` and `run_reactive.py`. MLIR and xDSL
adapters remain to be implemented; Figure 6 is not release-ready until all
three adapters emit `update-provider/v1` results and the assembler creates a
hash-bound `update-assembly/v1` record.

## Figure 7 · end-to-end performance

The suite contains 24 operator graphs across six families and 15 pinned models.
The variants are Joggle with required lowering only, Joggle with the frozen
optimization pipeline, and single-thread ONNX Runtime CPU EP. All consume
byte-identical inputs. Each supported pair runs ten warm-ups and 100 timed
steady-state executions. Short operators use a fixed batch; the CSV reports
per-call latency and preserves the batch size.

Correct coverage is reported over the complete population. Unsupported pairs
remain explicit and do not enter latency ratios. Preparation time, peak memory,
and artifact bytes are outside the current claim and are not collected by the
release path.

- Cases and measurement controls: `manifests/benchmark-cases.json`
- Collector schemas: `templates/benchmark-operators.csv`, `benchmark-models.csv`
- Figure CSV: `templates/figure-07-performance.csv`
- Collectors: `run_joggle_benchmarks.py`, `run_onnxruntime_benchmarks.py`
- Assembler: `merge_benchmark_rows.py`
- Plot: `figures/figure_07_performance.py`

## Release gate

`check_release.py` validates Figures 4--7, verifies provenance hashes, renders
PDF and PNG outputs, and writes `evaluation-release/v2`. A figure enters the
paper only after this gate succeeds from a clean, pinned collection.
