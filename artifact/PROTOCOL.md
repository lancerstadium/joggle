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
two tasks per family reused by Figure 5. Task implementations, prompts, and
reference solutions live outside the model context until the manifest is
sealed; demonstration-bank features must use different operation names,
semantics, and oracle fixtures.

The primary endpoint is task-level executable pass@1. pass@5 and pass@10 show
sample-budget sensitivity. Reference-solution log-perplexity is secondary and
is compared only within one model and tokenizer. The oracle records the first
failed phase: parse, type, build, or semantic test.

- CSV: `templates/figure-04-extension.csv`
- Plot: `figures/figure_04_extension.py`
- Required pairing: model, task, and demonstration count across systems

## Figure 5: change footprint

Twelve tasks are selected before implementation, two per family. Each patch
starts at a pinned clean revision, passes the shared oracle, and is reduced to
a fixed point by hunk-level delta debugging. Zone maps and exclusion rules are
frozen before counts are collected.

Absolute paired counts are primary because a system can require zero registry
or build edits. The four coordinates are implementation files, changed
implementation lines, ownership zones, and registry/build declarations. Test
changes and dependency fan-out remain visible but separate.

- CSV: `templates/figure-05-footprint.csv`
- Plot: `figures/figure_05_footprint.py`
- Required pairing: task across systems

## Figures 6 and 7: reactive updates

Figure 6 uses the 15 pinned non-heavy ONNX subjects from `test/models.cmake`.
Figure 7 uses generated graphs with total operations 1K, 10K, 100K, and 1M and
affected cones of 1, 8, 64, and 512 operations. Both figures compare Full,
Reactive, Whole-mod, and No-plan-cache under one edit and one verified output
digest. A suffix policy is not a separate condition because every edit enters
before the first stage; its conservative suffix is the Full policy.

The primary endpoint is edit-to-result latency. Selection, evaluation, and
verification times explain it; executed stages, evaluated compiler-function
operations, observations, and plan counters validate the mechanism. Figure 6
and Figure 7 receive separate exported CSVs even when they originate from the
same raw run.

- Model CSV: `templates/figure-06-model-update.csv`
- Model plot: `figures/figure_06_model_update.py`
- Scaling CSV: `templates/figure-07-scaling.csv`
- Scaling plot: `figures/figure_07_scaling.py`
- Required pairing: subject, edit, site, iteration, and seed across policies

## Figures 8 and 9: generated artifacts

Figure 8 covers elementwise chains, reductions, matrix multiplication,
convolution, quantize/dequantize paths, and fusion opportunities at fixed
shapes and dtypes. Figure 9 covers every pinned model, retaining unsupported
cases as coverage rows. The three variants are unoptimized Joggle, optimized
Joggle, and one pinned CPU reference with a fixed thread policy.

Preparation time and steady-state latency are separate. Operator rows also
record code size; model rows record peak memory and artifact size. Fixed inputs
and dtype-specific tolerances define numerical equivalence.

- Operator CSV: `templates/figure-08-operators.csv`
- Operator plot: `figures/figure_08_operators.py`
- Model CSV: `templates/figure-09-models.csv`
- Model plot: `figures/figure_09_models.py`
- Required pairing: operator/shape/dtype or model/input/seed across variants

## Release gate

A paper figure is ready only when its CSV has passed schema validation, every
expected pairing is complete, every aggregated timing row passed its oracle,
the plotting script reproduces both PDF and PNG outputs, and the caption names
the independent unit and interval construction.
