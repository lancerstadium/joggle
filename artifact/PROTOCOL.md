# Evaluation protocol

Section 4 uses four figures. Each figure answers one paper claim, consumes one
CSV, and is rendered by one script. Repetitions estimate a subject; they do not
increase the number of independent tasks, patches, or models.

| Figure | Claim | Compared systems | Independent unit | Primary endpoint |
| --- | --- | --- | --- | --- |
| 4 | Convenient extension | Joggle, MLIR, xDSL | 24 extension tasks | agent success within budget |
| 5 | Controllable change | Joggle, MLIR, xDSL | 12 matched patches | files, lines, zones, declarations |
| 6 | Efficient update | Joggle, MLIR, xDSL | 15 model graphs | Update/Full time and visited work |
| 7 | End-to-end performance | Joggle base/opt, ONNX Runtime | 24 operators and 15 models | steady-state latency and correct coverage |

The complete release contains 2,880 Figure 4 trajectories, 36 Figure 5 patch
rows, 108,000 matched-stage Figure 6 timing rows plus 1,050 production-lowering
stage rows, and up to 11,700 Figure 7 timing rows. Repeated rows estimate each
independent task, patch, operator, or model; they are never treated as
additional independent subjects.

## Common controls

Pin revisions, model hashes, compiler flags, hardware, affinity, thread count,
and seeds before collection. Pair inputs and edits within each subject. Retain
unsupported cases as coverage observations; exclude them from latency ratios.
Every timed output must pass its semantic or numerical oracle. Form ratios
within a subject before aggregating across subjects.

## Figure 4 · agent extension completion

The suite has four tasks in each of six extension families: definition,
analysis, rewrite, conversion, emission, and vertical extension. Two pinned
small code models drive the same deterministic coding-agent harness. For each
system, the agent receives the same semantic specification, a compact native
API card, an isolated workspace, and inspect/edit/build/test tools. Each
model/system/task condition uses zero or two disjoint demonstrations and ten
seeded runs under a 30-action and 32k-token budget, yielding 2,880 trajectories.

Executable success within budget is primary. Successful trajectories report
completion tokens, tool calls, edit attempts, and wall time. Unsuccessful runs
report the first terminal phase: parse, type, build, semantic test, or budget.
Reference-solution log-perplexity is a supplementary interface-predictability
diagnostic, not a separate experiment.

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
canonicalization, target selection, memory planning, and deterministic artifact
construction. Each stage computes state consumed by its successor; metadata
markers alone do not satisfy the contract.

For early, middle, and late edit sites, the experiment applies matched metadata
and value-type edits to an affected or unrelated location. `full` reruns all
five stages. For Joggle, `update` uses revision and dependency selection. For
MLIR and xDSL, `update` is the native complete pass path after the edit because
their public execution models do not retain per-call observations across this
pipeline. Every numerator is paired with an independent complete rerun from
the same edited input. The Cartesian matrix is 15 models x 3 systems x 3 sites
x 2 edit classes x 2 scopes x 2 policies x 100 iterations = 108,000 rows. The
release validator rejects a missing or additional cell. The primary
measurements are:

\[
\mathrm{UpdateRatio}_s=T_{\mathrm{update},s}/T_{\mathrm{full},s},\qquad
\mathrm{WorkRatio}_s=V_{\mathrm{update},s}/V_{\mathrm{full},s}.
\]

Absolute edit-to-result latency remains visible. Internal scheduler policies
are implementation diagnostics and do not enter the paper comparison.
Each adapter serializes the selected artifact slice in graph order and reports
the lowercase FNV-1a-64 digest of that canonical byte stream. Paired policies
and all three systems must produce the same digest for a case.

A calibration panel measures Joggle's production path on the same 15 models.
It reports decoding, inference and conversion, `c.prepare`, storage planning,
scalar lowering, storage placement, C emission, graph size, emitted bytes, and
correctness for a complete rebuild. Each model runs one warm-up and ten measured
rebuilds, producing seven stage rows per rebuild. These rows establish the
absolute cost represented by the normalized matched stages; they do not enter
UpdateRatio or WorkRatio. Their output digest is SHA-256 over the emitted C
header and source separated by one zero byte.

- Model index: `manifests/reactive-models.csv`
- CSV: `templates/figure-06-update.csv`
- Validator: `validate_reactive.py`
- Assembler: `merge_update_rows.py`
- Plot: `figures/figure_06_update.py`

The release gate accepts Figure 6 only when Joggle, MLIR, and xDSL providers
emit `update-provider/v1` results and the assembler creates a hash-bound
`update-assembly/v1` record.

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
