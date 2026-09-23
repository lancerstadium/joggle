# Evaluation protocol

Section 4 follows the three claims plus end-to-end evaluation. Each experiment
has one assembled CSV and one plotting entry point. Repetitions estimate a
subject; they do not increase the number of independent tasks, patches, or
models. The populations below specify collection targets, not completed runs.

| Figure | Claim | Compared systems | Independent unit | Primary endpoint |
| --- | --- | --- | --- | --- |
| 4 | Convenient extension | Joggle, MLIR, xDSL | 24 extension tasks | agent success within budget |
| 5 | Controllable change | Joggle, MLIR, xDSL | 12 matched patches | files, lines, zones, declarations |
| 6 | Efficient update | Joggle, TVM, ONNX-MLIR; IREE adapter pending | 15 complete models | edit-to-executable time and Update/Full |
| 7 | End-to-end performance | Joggle base/opt, ONNX Runtime, TVM, ONNX-MLIR | 24 operators and 15 models | steady-state latency and correct coverage |

The original Figure 4 collection target is 2,880 trajectories; Figure 5 targets
36 matched patch rows. The five Figure 7 variants allow at most 19,500 valid
timing rows (39 subjects x 5 variants x 100 repetitions), with unsupported or
incorrect cases recorded separately. Freeze the Figure 6 edit population and
native adapters before scheduling its formal collection. Count completed
subjects from audited run records, not these targets.

## Common controls

Pin revisions, model hashes, compiler flags, hardware, affinity, thread count,
and seeds before collection. Pair inputs and edits within each subject. Retain
unsupported cases as coverage observations; exclude them from latency ratios.
Every timed output must pass its semantic or numerical oracle. Form ratios
within a subject before aggregating across subjects.

Collect native CPU benchmarks serially. Do not overlap timed executions with
compiler builds, agent inference, or another measurement process. Freeze the
candidate checkout, executable, and source mods for both Joggle variants.
Record an interrupted or contaminated collection beside its CSV as
`<name>.invalid.json`, including the cause and affected run. The assembler
rejects the entire run; recollect it into a new path. Keep its output checks as
diagnostic evidence, separate from timing evidence.

## External comparator expansion

The comparison separates full compiler-extension tasks from
operator-scheduling subsets. The following systems are assigned by task,
not pooled into a single ranking:

| Claim | Full-task comparators | Focused subsets |
| --- | --- | --- |
| Convenient | MLIR, xDSL, TVM | Exo 2 and Halide scheduling; Triton on matched GPU hardware |
| Controllable | MLIR, xDSL, TVM | Cross-stage ONNX-MLIR or IREE extension patches |
| Efficient | TVM, ONNX-MLIR, IREE | TorchInductor with original PyTorch models; Exo/Halide operator edits |
| End-to-end | ONNX Runtime, TVM, ONNX-MLIR, IREE | TorchInductor with original models; Exo/Halide operator subset |

TVM and ONNX-MLIR execution are implemented in the shared external-baseline
collector. Other new adapters remain to be implemented and validated. Existing release gates
cover the original population; they do not certify the expanded comparison.
New formal populations must be frozen before measurement, including the
task-system applicability matrix. Existing input and model specifications
remain unchanged so their bytes and hashes stay paired across systems.

For extension completion, compare full tasks separately from the scheduling
subset. Report executable success at fixed budgets and time/token completion
curves. Do not rank languages by raw token perplexity. For change footprint,
validate each functional patch before counting files, interfaces, ownership
zones, and registration sites; implementation-line counts are secondary.

The main update experiment must compile and execute complete edited models.
Distinguish model-graph edits, optimization-policy edits, and compiler-source
edits. Never compare a graph edit in one system against a toolchain rebuild in
another. Keep native caching and pass reuse enabled, and measure the same
edit-to-executable boundary. Report absolute update time, its paired full-build
ratio, and stage costs. Metadata propagation and one-shot TVM preparation
diagnostics do not establish responsive compilation performance.

Keep internal ablations subordinate to external comparisons. Main figures
show the important measurements and coverage; appendices contain per-case
detail. Repeated runs quantify measurement variation, while different tasks,
models, edit sites, and systems provide workload coverage.

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

## Figure 6 · edit-to-executable updates

The primary experiment starts with a compiled and checked model, applies one
specified edit, and ends with a replacement executable whose output passes the
same numerical oracle as a complete rebuild of the edited model. Report warm
update time, full-build time, their paired ratio, and backend coverage. First
construction of reusable state is a separate cold-build measurement.

Use the same 15 model inputs and hashes as Figure 7. Match each edit's semantics,
location, and optimization policy across systems. Keep model-graph edits,
optimization-policy edits, and compiler-source edits in separate panels. Native
caches remain enabled; every adapter records what it retained and invalidated.
An update must produce the edited executable: reusing an unchanged artifact or
re-running preparation on an already lowered graph does not implement this
workload.

The external production adapters target TVM and ONNX-MLIR, with IREE added
after adapter validation. The current metadata and matched-stage providers use
different endpoints. Their records remain separate from the main comparison.

The existing external collector also exposes `--worker update --model BEFORE
--edited-model AFTER` and `--worker rebuild --model AFTER` for native adapter
validation. Both require the existing input index, case ID, specification, and
backend arguments. The update worker retains the original checked executable
while compiling the replacement; the rebuild worker starts in a fresh process.
TVM's native process caches are left untouched. ONNX-MLIR invokes a fresh native
compiler subprocess for each build. Neither adapter claims subgraph reuse.

These workers emit `production-update-sample/v1` JSON, not Figure 6 CSV rows.
They record source/input hashes, output digests, initial validation, and separate
ready/validation times. The supplied edited ONNX bytes and oracle outputs are
prepared before the measured build boundary. Consequently these adapter samples
do not yet include edit application, a prescribed edit population, or paired
repetitions. The production collector must add those components and the Joggle
resident update path before these samples can support the main comparison.

### Supporting matched-stage diagnostic

Fifteen pinned ONNX models are decoded into a neutral typed graph that retains
operation kinds, values, types, and def-use edges. An adapter materializes this
graph in each system. Every system runs the same five logical stages: analysis,
canonicalization, target selection, memory planning, and deterministic artifact
construction. Each stage computes state consumed by its successor; metadata
markers alone do not satisfy the contract.

For early, middle, and late edit sites, the experiment applies matched metadata
and value-type edits to an affected or unrelated location. `full` reruns all
five stages. For Joggle, `update` uses revision and dependency selection. For
MLIR and xDSL, the adapter must document native invalidation and reuse behavior
rather than assume that every edit requires a complete pass rerun. Every
numerator is paired with an independent complete rerun from
the same edited input. The Cartesian matrix is 15 models x 3 systems x 3 sites
x 2 edit classes x 2 scopes x 2 policies x 100 iterations = 108,000 rows. The
release validator rejects a missing or additional cell. The primary
measurements are:

\[
\mathrm{UpdateRatio}_s=T_{\mathrm{update},s}/T_{\mathrm{full},s},\qquad
\mathrm{WorkRatio}_s=V_{\mathrm{update},s}/V_{\mathrm{full},s}.
\]

Record absolute edit-to-artifact latency for this diagnostic. Internal scheduler
policies are implementation diagnostics and do not enter the main comparison.
Each adapter serializes the selected artifact slice in graph order and reports
the lowercase FNV-1a-64 digest of that canonical byte stream. Paired policies
and all three systems must produce the same digest for a case.

A separate collector measures Joggle's production path on the same 15 models.
The collector specializes `main` from the fixed input types in
`benchmark-cases.json` before conversion; `opt.signature` binds named shape
parameters and refines anonymous extents in one checked operation. It reports
decoding, specialization plus inference and conversion, `c.prepare`, storage
planning, scalar lowering, storage placement, C emission, graph size, emitted
bytes, and correctness for a complete rebuild. Each model runs one warm-up and
ten measured rebuilds, producing seven stage rows per rebuild. These rows
establish the absolute cost represented by the normalized matched stages; they
do not enter UpdateRatio or WorkRatio. Their output digest is SHA-256 over the
emitted C header and source separated by one zero byte.

Inference, conversion, and preparation run as one in-process sequence.
Per-function timing splits that sequence into the reported stages; process
startup, loading, initial verification, transaction setup, and serialization
are charged to inference and conversion. Scalar lowering, storage planning
(including no-alias analysis), and placement use materialized boundaries.
Their timings include subprocess overhead and graph loading and writing.
Production timings describe this collector's complete wall time, with the
frontier check, summary query, and host C compilation excluded.

- Model index: `manifests/reactive-models.csv`
- CSV: `templates/figure-06-update.csv`
- Validator: `validate_reactive.py`
- Assembler: `merge_update_rows.py`
- Plot: `figures/figure_06_update.py`

The existing matched-stage gate accepts its diagnostic only when Joggle, MLIR,
and xDSL providers
emit `update-provider/v1` results and the assembler creates a hash-bound
`update-assembly/v1` record. Matched providers additionally identify
`workload: compiler-pipeline/v1` and
`visited_ops_unit: subject-operation-visits`. Evaluator instruction counts
and the existing `metadata-propagation/v1` diagnostic are not Figure 6
compiler-work measurements. The assembler and release gate enforce this
distinction; a provider declaration still requires source-level verification
of its stages and counters. This gate certifies the matched-stage schema, not
the production edit-to-executable experiment. Extend the provider contract and
gate together before assembling the main Figure 6 dataset.

## Figure 7 · end-to-end performance

The suite contains 24 operator graphs across six families and 15 pinned models.
The five variants are Joggle with required lowering only, Joggle with the
frozen optimization pipeline, single-thread ONNX Runtime CPU EP, TVM
Relax/LLVM, and ONNX-MLIR/LLVM. Backend revisions and compilation options are
bound to their run records. All consume byte-identical inputs. Each supported
pair runs ten warm-ups and 100 timed
steady-state executions. Short operators use a fixed batch; the CSV reports
per-call latency and preserves the batch size.

Correctness and timing use separate ONNX Runtime sessions. The semantic oracle
executes the submitted graph with `ORT_DISABLE_ALL`; the performance baseline
retains `ORT_ENABLE_ALL`. Both Joggle and the optimized ONNX Runtime baseline
are checked against the same semantic output and the unchanged per-case
tolerances, outside timed regions. Run records bind this policy as
`onnx-graph-semantics/v1`. Optimized baseline outputs are not assumed correct.
This distinction preserves the arithmetic of QDQ graphs: fusing floating-point
Conv into QLinearConv can introduce int32 bias overflow that the source graph
does not contain. Incorrect outputs remain explicit coverage failures rather
than latency ratios. Historical collections retain their original oracle
policy. New Joggle runs pin this policy, source-mod hashes, and measurement
configuration in a `.checkpoint.json` sidecar before publishing any case
rows. Resuming requires an identical sidecar; legacy checkpoints without one
remain unchanged and require a new output path. The final run record and both
merge paths reject mixed oracle policies.

The Joggle collector applies the same manifest-derived `opt.signature` step as
the production calibration before either required or optimized lowering.

Correct coverage is reported over the complete population. Unsupported pairs
remain explicit and do not enter latency ratios. Preparation time, peak memory,
and artifact bytes are outside the current claim and are not collected by the
release path.

- Cases and measurement controls: `manifests/benchmark-cases.json`
- Collector schemas: `templates/benchmark-operators.csv`, `benchmark-models.csv`
- Figure CSV: `templates/figure-07-performance.csv`
- Collectors: `run_joggle_benchmarks.py`, `run_baseline_benchmarks.py`
- Assembler: `merge_benchmark_rows.py`
- Plot: `figures/figure_07_performance.py`

## Release gate

`check_release.py` implements the original Figures 4--7 contracts, verifies
provenance hashes, renders PDF and PNG outputs, and writes
`evaluation-release/v2`. Update its comparison population and Figure 6 endpoint
contract alongside the new collectors before using it for the expanded
release. The complete artifact release requires the updated gate to succeed
from a clean, pinned collection. During
drafting, an independently audited snapshot may supply a measured subset (such
as the complete operator suite); its hash-bound merge record retains
`complete: false`, and the figure caption identifies the population actually
shown. It is not a complete Figure 7 release.
