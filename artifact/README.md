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
`ana-numeric-range`, `ana-fusion-match`, `emit-storage-plan`,
`emit-target-capability`, `emit-graph-manifest`, `rew-add-zero`, and
`rew-redundant-cast`, `rew-transpose-pair`, `con-instruction-select`, and
`con-gelu-expand`, `con-quant-expand`, `con-layout-legalize`, and
`rew-conv-bias-relu`, and `emit-kernel-wrapper`.
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

`emit-graph-manifest` walks actual native SSA operations, tensor types, and
operation attributes. Eight fixtures cover linear and shared-branch graphs,
repeated operands/returns, multiple results, dynamic dimensions, rank-zero
tensors, ordered mixed attributes, and identity graphs. The expected manifests
are stored only in the oracle contract; native fixtures contain no request
dictionary or expected output. Every case checks the full manifest and repeats
emission to check byte stability.

`rew-add-zero` requires a native in-place rewrite. Its `rewrite-starter.*`
files expose `transform` rather than `analyze`. The three reference extensions
modify a common small tensor dialect represented by typed SSA calls, with
scalar splat attributes and explicit floating-point `no_signed_zeros` flags.
Nine cases cover both operand sides, scalar splats, shared constants, rank-zero
tensors, nonzero constants, signed zero, and shape-changing broadcasts.

The candidate process emits IR, not an answer dictionary. A second process
parses that IR and extracts types, operands, constants, and returns. The oracle
compares the full resulting graph and evaluates the original and rewritten
graphs on five input sets. Strict floating cases compare output bits; explicit
no-signed-zeros cases compare numerical values. No-op starters pass the four
preservation cases but fail all five required rewrites and the complete task.
Reports retain transformed IR, independent observations, and numerical bits.
NumPy evaluates these small graphs; xDSL is the common post-IR observer for
MLIR and xDSL output. Run the oracle with the pinned experiment interpreter,
or supply its path with `--xdsl-python` when using the MLIR provider.

`rew-redundant-cast` uses the same mutation and independent observation path.
Ten fixtures cover identity, signed integer and finite floating-point widening
round trips, shared intermediate users, narrowing, float/integer conversions,
shape changes, and single widening casts. Input sets include signed zero,
fractional values, and integer wraparound boundaries. Required rewrites and
required preservation each account for five cases; a no-op does not pass the
task. Reference results validate these oracles, not an agent's success rate.

`rew-transpose-pair` cancels inverse permutations on native SSA calls. Typed
callee names encode the element type and source/result shapes so all three
systems use the same monomorphic signatures; `perm` attributes carry the
actual permutations. Eleven cases cover rank-four and self-inverse pairs,
shared intermediates, rank-zero and empty tensors, integers, non-inverse pairs
including equal dimensions, and malformed permutations. Valid results are
independently parsed and checked structurally and bitwise on five input sets.
Malformed permutations must produce the specified diagnostic and no output
IR. A no-op starter fails all required eliminations and rejection cases.

`con-instruction-select` retargets native matrix-product calls to a declared
tiled target and attaches the selected tile counts. Ten cases cover single
and multiple tiles, repeated result users, each ragged dimension, non-f16
inputs, zero extents, and batched products. The observer checks the complete
post-conversion graph, preserved metadata, and tile order; the numerical oracle
checks f32-accumulation semantics on five input sets. This task measures compiler
extension correctness, not GPU instruction throughput.

`con-gelu-expand` replaces a native GELU call with arithmetic and erf calls.
Seven cases cover f32/f64 tensors, scalars, repeated output users, empty tensors,
and rejected integer/half inputs. The oracle accepts equivalent target graphs
without prescribing node order or parenthesization. It requires GELU removal,
checks output types, and compares the independently observed graph against
binary64 GELU semantics on seven input sets using the task's dtype-specific
tolerances. An unchanged GELU call or an arithmetically incorrect expansion
fails. Constant values remain explicit IR attributes, not driver-side answers.

`con-quant-expand` replaces native qadd with dequantize, floating add, and
quantize operations. Ten cases cover mixed scales, ties-to-even, odd output
zero points, independent input zero points, both saturation limits, repeated
outputs, scalars, and nonpositive scales. Target graphs are checked on five
deterministic input pairs and the case's explicit values. The latter also have
hand-specified integer answers in the task contract. Integer outputs must match
exactly. Zero points are added after rounding, and source data are not embedded
in the native IR presented to the candidate.

`con-layout-legalize` transforms NHWC convolution into NCHW convolution with
explicit input, weight, and output transposes. Seven cases cover non-square
shapes, asymmetric padding, strides, dilation, pointwise and single-channel
convolution, repeated users, and channel-mismatch rejection. The observer
checks the emitted graph and bitwise output equivalence on six input sets,
including a fixed-seed nonperiodic set that distinguishes spatial permutations.
The spatial reference is also regression-tested against ONNX Runtime on all
six valid shapes and against a hand-calculated cross-correlation example.

`rew-conv-bias-relu` performs native Conv/BiasAdd/ReLU fusion with complete
attribute and output-use preservation. Nine cases cover NCHW/NHWC, stride,
asymmetric padding, dilation, repeated final outputs, shared intermediates,
an incompatible activation, and a shape-compatible but incorrect bias axis.
The latter four must remain unchanged. Both the exact observed graph and
bitwise f32 outputs on six input sets are checked. Fused semantics preserve
the original convolution accumulation, then bias addition, then ReLU; this
task measures extension correctness, not fused-kernel throughput.

`emit-kernel-wrapper` emits a C99 source artifact from the kernel name, without
seeing runtime vectors. Seven cases exercise ReLU and affine transforms with
empty, singleton, and non-vector-multiple lengths. The oracle compiles each
artifact with a separately generated fixed driver and the recorded `--cc`
compiler (`-O2 -fno-fast-math -ffp-contract=off`). It checks exact f32 output
bits in separate-buffer and in-place modes, input preservation, guard writes,
and null-pointer handling at zero count. Repeated emission must be identical.
Compiler/build/run diagnostics remain available in public agent feedback.

### Local agent execution

`run_extension_agent.py` runs an installed Ollama model with four actions:
`inspect`, `edit`, `test`, and `finish`. Each action and response is saved with
token counts, candidate hashes, tool feedback, and the final patch. The public
test action exposes the first positive and first negative fixture (when present).
Final scoring executes the complete task after the trajectory ends; its results
are not returned to the model.

```sh
python3 artifact/run_extension_agent.py \
  --model qwen3.5:9b --system Joggle --task ana-fusion-match \
  --seed 1701 --run 0 \
  --joggle build/joggle --builtin-mods build/modules \
  --output .cache/artifact/agent-joggle-fusion-1701
```

Native candidates run under macOS Seatbelt: toolchain files are read-only,
writes are confined to the trial directory, network access is denied, and
credentials are removed from the child environment. MLIR additionally needs
`--sandbox-read /path/to/llvm-project` for its build-tree headers and libraries.
The agent has no shell action. `--allow-dirty` marks an integration run against
uncommitted harness changes; otherwise the collector requires a clean checkout.

Each request disables history truncation and generation-time context shifting
with Ollama's `truncate: false` and `shift: false`. Responses must report valid
token counts within the requested generation and context limits. A structured
native context-size rejection ends the trajectory at its budget boundary; it
does not execute an action or retry with shortened history. Records retain the
request hash, overflow token counts, context policy, and server version. Other
provider errors remain infrastructure failures. Before formal collection,
verify these controls on every pinned model/server pair; token counts alone
cannot establish that an older server honored the flags.
Public and final oracle reports must cover their assigned fixture populations.
Provider, oracle, and identity-check exceptions are retained in the trajectory
with `stop_reason: agent_error`; final-check failures still produce the CSV and
patch record, with unknown compiler phases left empty.

The current collector emits integration records with `release_eligible: false`.
It records successful native execution but does not infer a failed compiler's
parse/type/build phase from its exit code. Unmeasured phase fields and reference
likelihoods remain empty. These records cannot enter the release assembler.
The full matrix additionally requires the remaining 8 native task harnesses,
phase-specific instrumentation, and the frozen demonstration sets. The
assembler consumes complete provider records:

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

The production comparison uses Joggle, TVM, and ONNX-MLIR. Each worker applies
a hash-bound ONNX node edit, builds a replacement executable, and checks its
outputs against an isolated semantic oracle. `update` first builds the original
model in the same worker; `rebuild` builds only the edited model in a fresh
worker. Both start their measured interval before applying the edit and stop
after numerical validation. Oracle generation is outside that interval.

```sh
cmake --build build --target joggle-artifact-reactive -j4
python3 artifact/run_baseline_benchmarks.py \
  --worker update --backend joggle \
  --inputs PATH/inputs --case-id CASE \
  --model PATH/model.onnx --edit-json PATH/edit.json
```

Repeat with `--worker rebuild`, or select `--backend tvm` /
`--backend onnx-mlir` with the corresponding compiler configuration. The JSON
sample records edit/input/output hashes, absolute wall time, executable-ready
time, validation time, backend stages, and retained state. Joggle currently
retains its environment and evaluator plans but parses a fresh source graph.
Its entry signature is derived from the same fixed inputs as the end-to-end
collector. Building first refreshes the copied mod files in `build/modules`.

Each sample fingerprints the native compiler, collector sources, and protocol
before and after measurement. Joggle also fingerprints the resident server,
loaded mod files, and host C compiler. A changed fingerprint rejects the
sample. Fingerprinting is outside the timed interval. A stable sample is a
measurement record, not a completed repeated model/edit population.

`merge_update_rows.py`, `validate_reactive.py`, and `figure_06_update.py`
still consume the earlier stage-counter provider schema; they do not yet
assemble `production-update-sample/v1`. Do not pass production samples through
that schema or mix them with metadata diagnostics. The production population,
repeated sampling, and Figure 6 assembly remain to be connected.

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

Both plotting scripts use a 3.35 × 2.34 inch authoring canvas for six panels,
with the same default 6 pt font and boxed three-column, two-row layout.
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
11 jointly correct models. Bars show medians and whiskers extend to p95;
the aggregate has no timing whisker. Failure reasons remain in the summary
CSV. Absolute milliseconds remain in the exported summary
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
