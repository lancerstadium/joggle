# Evaluation protocol

## Reporting status — 25 September 2026

The manuscript reports native reference size for Convenient (12 tasks × three
systems), complete-package footprint for Controllable, executable-ready updates
for Efficient, and operator/model execution. `paper/Makefile` is the single
entry point for the reported figures. `paper/data/extension-size.csv` counts
nonempty physical source lines, including comments and imports, and UTF-8 bytes
from all 36 admitted reference files. Shared drivers and build files are excluded.
Recollect these counts with `python3 paper/render_extensions.py --collect
local/cache/artifact/main-agents-explicit-20260925-x0f9ieig/manifest.json`.

Reference-size analysis was added after inspecting the incomplete Agent study.
It measures these implementations, not Agent performance or development time.
The original Agent endpoint below is retained: 42 complete trajectories, zero
full-task completions; 25 unstarted, four interrupted, and one blocked condition.
Appendix C reports those collection states and actual candidate diagnostics.
The remaining 30 conditions are not counted as task failures. No collector is
scheduled to resume, and no Agent advantage is inferred from this collection.

The numbered figure names below are stable artifact identifiers from the
original collection plan, not the current manuscript's automatic figure numbers.
The original targets and scoring rules remain unchanged.

Section 4 follows the three claims plus end-to-end evaluation. Each experiment
has one assembled CSV and one plotting entry point. Repetitions estimate a
subject; they do not increase the number of independent tasks, patches, or
models. The populations below specify collection targets, not completed runs.

| Figure | Claim | Compared systems | Independent unit | Primary endpoint |
| --- | --- | --- | --- | --- |
| 4 | Convenient extension | Joggle, MLIR, xDSL | 12 tasks; two per family | agent success within budget |
| 5 | Controllable change | Joggle, MLIR, xDSL | 8 package conditions: 2 integrations and 6 maintenance edits | files, lines, zones, declarations |
| 6 | Efficient update | Joggle, TVM, ONNX-MLIR | 9 edit sites in 3 complete models | edit-to-executable time and paired Full/Update |
| 7 | End-to-end performance | Joggle base/opt, ONNX Runtime, TVM, ONNX-MLIR | 24 operators and 15 models | steady-state latency and correct coverage |

Figure 4 collects 72 trajectories: 12 tasks × three systems × two models,
one zero-shot run per condition with matched sampling settings. Figure 5
collects 24 changed packages and 18 maintenance-parent controls. Figure 7
uses five configurations for operators and four for models, allowing at most
18,000 valid timing rows ((24 × 5 + 15 × 4) × 100), with unsupported or
incorrect cases recorded separately. Figure 6's manuscript dataset contains
540 policy rows: nine edit sites × three systems × ten paired repetitions ×
two policies. Its source, edit population, revisions, and cache configuration
are bound by `paper/data/figure-06-update.json` (relative to the repository
root). Count completed subjects from audited run records, not these targets.

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

## Comparator roles and optional extensions

The primary extension comparison is Joggle/MLIR/xDSL; the primary executable
comparison is Joggle/TVM/ONNX-MLIR, with ONNX Runtime additionally providing
an execution baseline. The following broader candidates describe optional
follow-up coverage, not extra members of either frozen primary population:

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
analysis, rewrite, conversion, emission, and vertical extension. Qwen3-8B and
Qwen3-14B drive the same coding-agent harness through SiliconFlow. For each
system, the agent receives the same semantic specification, a compact native
API card, an isolated workspace, and inspect/edit/build/test tools. Each
model/system/task condition uses one zero-shot run under a 30-action and
32k-generated-token budget. The execution set is the 12 tasks marked
`footprint: true` in `extension-specs.json`; no task is selected from agent
outcomes. Both models use temperature zero, thinking disabled, JSON-object
responses, run index zero, and no demonstrations. This yields 72 trajectories.
Two-demonstration diagnostics remain outside the primary dataset.

The shared interaction protocol is `explicit-json-actions/v2`. Every system
receives the same four exact action templates in its system instruction:

```json
{"action":"inspect"}
{"action":"edit","source":"<complete replacement source file>"}
{"action":"test"}
{"action":"finish"}
```

Each line is one alternative request, not a multi-action response. The candidate
file is implicit; `test` runs every public fixture. Feedback names unrecognized
keys such as `file` or `fixture` and lists the accepted fields. These examples
specify the tool interface, not solutions to compiler tasks. Dispatch and
measurement replay use the same schema check. The primary assembler requires
this protocol throughout and rejects records from the earlier implicit format.

On 25 September, the author approved this common-interface correction after
four initial trajectories repeatedly used undocumented fields. All four are
retained as protocol diagnostics, including their unsuccessful outcomes and
67 schema-rejected responses among 104 returned responses. The full 72-condition
matrix is recollected under the revised protocol, with the original condition
order, models, task contracts, API cards, budgets, fixtures, and oracles unchanged.

The hosted protocol uses model IDs `Qwen/Qwen3-8B` and `Qwen/Qwen3-14B` at
`https://api.siliconflow.cn/v1`, with complete conversation history and at most
4,096 generated tokens per request (also bounded by the remaining trajectory
budget). Both models have a 131,072-token service window; recorded prompt plus
completion counts must fit that window. No history is dropped by the harness.
The seed field is the condition identifier 1701; it is not sent as a provider
sampling seed. Service errors are recorded separately from candidate failures.
Only explicit HTTP 429/502/503/504 responses are retried, at most twice.
HTTP 429 waits 60/120 seconds for the minute-level quota to recover; other
retryable statuses wait 2/4 seconds. Ambiguous transport timeouts are not
retried automatically. This transport correction follows the first observed
TPM rejection; it changes no prompts, candidates, sampling controls, or budgets.

An exhausted explicit 429 can be continued with `--resume` at the same output
directory. The runner verifies the native identity, task/API sources, candidate,
complete conversation, and exact unserved request, then uses only the remaining
action/token budget. Successfully returned requests are never regenerated.
The interrupted record and oracle remain beside the resulting trajectory;
the merger checks that its prior responses and messages are unchanged. Wall
time sums active collection segments, including in-process retry waits but
excluding manual recovery downtime and final held-out validation. Service
rate-limit guidance: https://docs.siliconflow.cn/docs/userguide/faqs/rate-limit-and-upgradation.

On 25 September, after six completed primary trajectories and a read timeout
before condition seven's first response, the author approved a uniform
transport-continuation amendment. `--resume` also accepts a recorded
`TimeoutError` when no response was received. This rule applies to every model,
system, and task; completed outcomes and received responses are never rerun.
The archived interruption, identical request, complete history, candidate, and
remaining action/token budget are checked before continuation. Unreceived
generation and usage are unknown, not zero: reported token costs count returned
responses and are not a billing estimate. At most three external continuations
per condition are attempted by this collection; a further interruption pauses
the queue for review. Invalid received responses are not transport failures.
Task contracts, API cards, native tools, sampling parameters, and scoring are
unchanged. Earlier complete records remain primary observations.

The CSV's `model_revision` is `hosted-alias:<model ID>`, not a weight digest.
Trajectories retain catalog entries, returned model IDs, UTC timestamps,
response/trace IDs, fingerprints when supplied, native responses and usage.
An empty fingerprint remains empty. Local Ollama collection is a separate
supported protocol, with its original 32,768-token overflow check; no local
trajectories are included in the hosted study. Credentials are read from
`SILICONFLOW_API_KEY` or macOS Keychain service `joggle-siliconflow-evaluation`
(account `joggle`), never from a repository file. Isolated native tools receive
a fresh environment without provider credentials. API contract:
https://docs.siliconflow.cn/docs/api/chat-completions-post.

Executable success within budget is primary. Successful trajectories report
completion tokens, tool calls, edit attempts, and wall time. Unsuccessful runs
report build, execution, observation, or semantic failure, or the exhausted
action, token, or context budget. The runner does not infer parsing and typing
phases from diagnostic text; only successful end-to-end execution establishes
all three success gates. Infrastructure errors are recorded separately and
are rejected by the primary assembler. Reference-likelihood fields remain
empty when the provider does not expose scoring; they are not required for
executable-success measurements.

- Contract: `manifests/extension-specs.json`
- Task index: `manifests/extension-tasks.csv`
- CSV: `templates/figure-04-extension.csv`
- Plot: `figures/figure_04_extension.py`

## Figure 5 · change footprint

The native package matrix is run through `run_package_task.py`; its shared
case definitions and independent arithmetic oracles are in `package_cases.py`.
For each maintenance case, run both the changed package and `--parent` with
the same fixture bytes. Collect the complete 24-condition matrix with
`collect_footprint.py --package-runs <directory> --output <csv>`.
The package collector verifies all changed packages, failing positive parent
controls, fixture and source hashes, and recomputed patch counts. These counts
describe the observed implementations; they are not hunk-minimized estimates.
The original Git-patch collector remains available through `--cases`.

Amended 24 September 2026, with author approval, before collecting package
footprints. Single-file Agent tasks remain the Figure 4 population; their file
counts do not measure cross-stage package ownership. The Figure 5 unit is a
complete native extension package, including its public entry points and
registration/build declarations. Start with signed low-bit arithmetic and
quantized convolution fusion, using the admitted semantic references and
independent numerical oracles as implementation seeds.

`manifests/package-changes.json` fixes eight paired conditions: initial
integration for each feature and three independent maintenance changes per
feature. `manifests/package-sources.csv` fixes the deployable source boundary
for all six system/feature packages. Each maintenance change starts from the
original admitted package; later changes do not inherit earlier patches.

Measure initial integration and subsequent behavior changes separately.
Initial integration starts from an otherwise runnable host. Maintenance starts
from its oracle-passing package. Both baseline frameworks use out-of-tree
plugins; do not require in-tree compiler edits when native plugin APIs suffice.
Package-local analysis, transformation, and emission files share one ownership
zone. Apply the same zone rule to graph-level mods, MLIR plugins, and xDSL
packages. Count changed implementation files, implementation lines, ownership
zones, and registry/build declarations. Record tests separately; exclude shared
measurement infrastructure from feature patches and report its setup once.

Before measurement, freeze each matched behavior-change contract, its positive
and negative fixtures, each package layout, and the ownership policy. A
maintenance patch must pass the new oracle while its parent fails at least one
new positive fixture. Preserve the old regression suite wherever the behavior
contract is unchanged. Verify package discovery from a clean consumer, graph
transformation, emitted-code execution, and the final source tree. Do not
infer dependencies from directory depth or assign each compiler role a zone.

Implementation status: all six native system/feature packages pass admission
(36 semantic cases and 3,582 runtime probes). The first maintenance condition,
`lowbit-symmetric`, passes in all three systems. Each patch changes one
implementation line, stays in one package, and leaves registration unchanged;
reverting its sole hunk restores a parent that fails the changed numerical
contract. These equal counts are retained. The full eight-condition matrix
remains open; the first paired rows are not a completed Figure 5 dataset.
No package footprint row can be released from the single-file Agent reference
results. Admission records are in
`local/cache/artifact/package-admission-uFSMwu/summary.json`; the first paired
maintenance records and reversal checks are in
`local/cache/artifact/lowbit-symmetric-5oTpsX/`.

The minimizer exports an
isolated tracked-file snapshot below `local/cache/artifact/minimization` and uses a
private Git index. It does not create another worktree or change the author's
checkout/index. It first requires the unmodified baseline to fail the oracle,
then checks the original patch and each hunk deletion to a fixed point. Keep
source/oracle/policy/patch hashes with the final passing check.

Counts come from Git diffs rather than author logs. The collector binds the
base revision, final patch, minimization trace, oracle output, and policy by
hash.

- Case template: `templates/footprint-cases.csv`
- Package contracts: `manifests/package-changes.json`
- Package source boundary: `manifests/package-sources.csv`
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
--edit-json EDIT` and `--worker rebuild --model BEFORE --edit-json EDIT` for native adapter
validation. Both require the existing input index, case ID, specification, and
backend arguments. The update worker retains the original checked executable
while compiling the replacement; the rebuild worker starts in a fresh process.
TVM's native process caches are left untouched. ONNX-MLIR invokes a fresh native
compiler subprocess for each build. Neither adapter claims subgraph reuse.

These workers emit `production-update-sample/v1` JSON, not Figure 6 CSV rows.
They record source/input hashes, output digests, initial validation, and separate
edit/ready/validation times. An `onnx-node-edit/v1` specification binds a node
index, domain, and before/after operator names to the source model SHA-256.
Both paths apply and validate that edit inside the measured boundary. Oracle
outputs are prepared separately using the same deterministic edit. These samples
do not yet define a prescribed edit population or paired repetitions.
The production collector must add the population and paired repetitions before
these samples can support the main comparison.

`joggle-artifact-reactive --compile-sequence MODULES OUTDIR SOURCE...` supplies
the resident Joggle lowering endpoint. Each source is a fresh, specialized Jog
graph; the environment retains loaded mods and evaluator plans across sources.
The endpoint runs the unoptimized production passes and emits C, its header,
ABI metadata, and parse/lower/emit timings per source. It refuses to overwrite
an existing output directory. ONNX decoding, source edit application, entry
specialization, host C compilation, and numerical validation are outside this
endpoint and must be included by the end-to-end collector. This path measures
resident-plan reuse, not restoration of source operations or subgraph reuse.

The same worker now accepts `--backend joggle` with `--joggle`,
`--joggle-server`, and `--builtin-mods`. It uses the native server's line-based
request protocol to validate the first executable before requesting the second.
Each measured build includes ONNX edit application, decoding and specialization,
resident lowering and emission, shared-library compilation, input/output binding,
execution, and comparison with separately generated reference tensors. Resident
environment setup is recorded separately. The numerical check uses the same
tolerances and oracle as the external workers. This worker adds the complete
Joggle path, but a prescribed population, paired repetitions, and release
provenance are still required before assembling Figure 6.

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
