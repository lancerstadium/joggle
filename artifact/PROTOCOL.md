# Evaluation protocol

This document freezes the measurement contract behind Section 4. It separates
raw observations from figure-ready files: every paper figure consumes exactly
one CSV through one plotting script, while runners may retain richer logs for
audit and recovery.

## Evidence map

| Claim | Independent unit | Primary endpoint | Main control | Figure |
| --- | --- | --- | --- | --- |
| One language makes extensions predictable | task | executable pass@1 | same specification, model, demonstrations, and token budget | 4 |
| Mods bound repository change | task | source files, lines, zones, declarations | paired semantic task and passing oracle | 5 |
| Reactive execution limits update work | model or generated graph | edit-to-result latency | identical edit and output digest across policies | 6, 7 |
| Optimization preserves artifact quality | operator or model | steady-state latency | identical input and numerical oracle | 8, 9 |

Repeated generations and timings are observations within an independent unit;
they are never treated as independent tasks, graphs, operators, or models.

## Global rules

1. Pin source revisions, model hashes, toolchains, build flags, hardware, CPU
   governor, affinity, thread count, and random seeds before the reported run.
2. Keep every support and correctness outcome. A failing row contributes to
   coverage but not to a latency, speedup, or memory aggregate.
3. Randomize condition order within each block. Run ten warm-ups and 100 timed
   iterations for latency experiments unless the manifest states otherwise.
4. Form a ratio only within a matched subject, then use a geometric mean across
   subjects. Bootstrap the independent unit with 10,000 seeded resamples.
5. Preserve raw CSV, stdout, stderr, the run manifest, and the exact command.
   Figure scripts only read frozen CSV and never execute the system under test.

## Figure 4: extension completion

The suite contains 24 held-out tasks: four tasks in each of definition,
analysis, rewrite, conversion, emission, and vertical-extension families.
Joggle, MLIR, and xDSL receive a semantically equivalent specification and a
compact system-specific API card. Two pinned 1--3B code models run with 0, 1,
2, and 4 demonstrations selected from a disjoint bank. Each condition draws 50
samples.

`manifests/extension-tasks.csv` freezes the held-out task identities and the
two tasks per family reused by Figure 5. `manifests/extension-specs.json`
freezes their system-neutral semantics: valid and invalid fixtures, observable
results, comparison rule, and ordered oracle phases. All three systems solve
these contracts with idiomatic APIs; an adapter may change syntax but may not
add, remove, or reinterpret a fixture. Task implementations, prompts, and
reference solutions live outside the model context until the manifest is
sealed; demonstration-bank features must use different operation names,
semantics, and oracle fixtures.

The primary endpoint is task-level executable pass@1. pass@5 and pass@10 show
sample-budget sensitivity. Reference-solution log-perplexity is secondary and
is compared only within one model and tokenizer. The oracle records the first
failed phase: parse, type, build, or semantic test.

Every raw row pins the semantic-contract, API-card, prompt, and output hashes.
The demonstration IDs, sample seed, temperature, nucleus threshold, API-card
token ceiling, and continuation budget are explicit. Release validation
requires the same demonstrations and sampling controls for a paired model/task
condition across all three systems, while retaining each card's actual token
count.

Before prompts are generated, `validate_extension_specs.py` checks that the
contract contains exactly four tasks per family, exactly two preselected
footprint tasks per family, the frozen role sequences, at least two positive
fixtures per task, and ordered failure phases. The same validated contract is
the semantic oracle for the paired Figure 5 patches.

- CSV: `templates/figure-04-extension.csv`
- Task index: `manifests/extension-tasks.csv`
- Semantic contract: `manifests/extension-specs.json`
- Contract schema: `schemas/extension-specs.schema.json`
- Request materializer: `prepare_extension_requests.py`
- Pinned local inference and scoring: `run_extension_transformers.py`
- Oracle evaluator: `evaluate_extension_outputs.py`
- Validating assembler: `assemble_extension_rows.py`
- Plot: `figures/figure_04_extension.py`
- Required pairing: model, task, and demonstration count across systems

## Figure 5: change footprint

Twelve tasks are selected before implementation, two per family. Each patch
starts at a pinned clean revision, passes the shared oracle, and is reduced to
a fixed point by hunk-level delta debugging. Zone maps and exclusion rules are
frozen before counts are collected.

Absolute paired counts are primary because a system can require zero registry
or build edits. The four coordinates are implementation files, changed
implementation lines, ownership zones, and changed registry/build declaration
lines. Test changes and dependency fan-out remain visible but separate.

The collector derives every count from `git diff` and a frozen per-system
policy. Cross-zone edges use the policy dependency graph; for Joggle these
edges correspond to declared `use` relationships. The counted diff must equal
the final patch hash from `minimize_patch.py`; the case also pins the final
oracle output and the fixed-point hunk-deletion log by SHA-256.

- CSV: `templates/figure-05-footprint.csv`
- Plot: `figures/figure_05_footprint.py`
- Required pairing: task across systems

## Figures 6 and 7: reactive updates

Figure 6 uses the 15 pinned non-heavy ONNX subjects from `test/models.cmake`.
Figure 7 uses generated graphs with total operations 1K, 10K, 100K, and 1M and
affected cones of 1, 8, 64, and 512 operations. Its scaling plot fixes the edit
to affected operation metadata; Figure 6 retains all edit classes and scopes.
Both figures compare Full, Reactive, Whole-mod, and No-plan-cache under one edit
and one verified output
digest. A suffix policy is not a separate condition because every edit enters
before the first stage; its conservative suffix is the Full policy.

The primary endpoint is edit-to-result latency. Selection, evaluation, and
verification times explain it; executed stages, evaluated compiler-function
operations, observations, and plan counters validate the mechanism. Figure 6
and Figure 7 receive separate exported CSVs even when they originate from the
same raw run.

All policies begin after the same full-function initialization. Full scans the
complete function after each edit. Reactive calls retain a recorded root and
visit its affected cone when their dependencies change; Whole-mod and
No-plan-cache alter only observation precision and plan persistence.

- Model CSV: `templates/figure-06-model-update.csv`
- Model manifest: `manifests/reactive-models.csv`
- Model plot: `figures/figure_06_model_update.py`
- Scaling CSV: `templates/figure-07-scaling.csv`
- Scaling plot: `figures/figure_07_scaling.py`
- Required pairing: subject, edit, site, iteration, and seed across policies

## Figures 8 and 9: generated artifacts

`manifests/benchmark-cases.json` freezes 24 operator graphs---four each for
elementwise, reduction, matrix multiplication, convolution, quantization, and
fusion---plus deterministic inputs for all 15 pinned models. Figure 9 retains
every model/variant pair: an unsupported pair contributes one coverage row
rather than disappearing from the accepted intersection.

The three variants are Joggle without optional optimization passes, Joggle
with the frozen optimization pipeline, and a pinned single-thread ONNX Runtime
CPU Execution Provider with full graph optimization. All variants consume
byte-identical inputs. Required conversion, legalization, memory planning, and
emission remain in both Joggle paths; only optional optimization passes differ.
The optimized path freezes dead-code elimination, implementation selection for
floating-point matrix multiplication, range and constant folding, affine
canonicalization, locality-guided loop order, and scalar promotion as an exact
stage list in the case manifest.

Each supported pair has 30 fresh-process preparation observations, ten
fresh-process memory observations, and, after ten warm-ups, 100 steady-state
executions. Execution rows record latency, output digests, and absolute and
relative error. Memory rows execute the same checked workload and record peak
resident-set growth. Preparation rows record the path from model bytes to
callable state; this includes emission and host compilation for Joggle and
session construction for ONNX Runtime.
Execution timing surrounds one backend call over resident buffers. Memory is
the peak resident-set increase above an idle backend worker after its libraries
load. Generated artifact size covers model-specific object code and constants
and is compared only between the two Joggle variants; shared runtime libraries
have no equivalent per-model boundary and remain blank for the reference.
Dtype- and case-specific tolerances define numerical equivalence.
The manifest also fixes calls per timing sample. Short operators execute a
shared fixed batch and report elapsed time divided by its call count; model
samples contain one call. The batch count is retained in every execution row.
A collector records its stage timeout and preserves a timeout as an explicit
coverage outcome.

- Operator CSV: `templates/figure-08-operators.csv`
- Operator plot: `figures/figure_08_operators.py`
- Model CSV: `templates/figure-09-models.csv`
- Model plot: `figures/figure_09_models.py`
- Case manifest: `manifests/benchmark-cases.json`
- Input generator: `generate_benchmark_inputs.py`
- Operator-model generator: `generate_operator_models.py`
- Reference collector: `run_onnxruntime_benchmarks.py`
- Joggle collector: `run_joggle_benchmarks.py`
- Validating merger: `merge_benchmark_rows.py`
- Required pairing: frozen case ID, input digest, iteration, and seed across variants

## Release gate

A paper figure is ready only when its CSV has passed schema validation, every
expected pairing is complete, every aggregated timing row passed its oracle,
the plotting script reproduces both PDF and PNG outputs, and the caption names
the independent unit and interval construction. `check_release.py` enforces
this gate for Figures 4--9 and writes one hash-bound release manifest.
