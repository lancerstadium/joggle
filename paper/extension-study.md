# Extension study protocol

This study tests whether Joggle's function-and-module boundary supports bounded
neural-network co-design changes without modifying a central compiler registry.
It does not equate source lines with usability and does not treat the existing
authors as independent participants.

## Tasks

The frozen Joggle-side tasks are listed in
[`extension-tasks.json`](extension-tasks.json):

1. replace a shared matrix implementation with an inspectable loop body;
2. define a structural cost measure and fusion-selection policy;
3. connect generic external matrix and convolution kernels;
4. add a parametric saturating integer format with two target representations.

The first three are out-of-tree extensions. The numeric format is explicitly
classified as a bundled optional module because it also exercises the native
module ABI; it must not be reported as a zero-build-system-change task.

## Mechanically collected observations

`measure_extensions.py` validates every declared module and task test, then
records source files, nonblank non-comment source lines, bytes, declared module
dependencies, public functions, and a digest over the exact source paths and
bytes. These quantities describe the footprint of the checked-in
implementation. They do not measure comprehension,
implementation time, difficulty, or correctness beyond the named tests.

From a configured sanitizer build with the optional `sat` module:

```sh
python3 paper/measure_extensions.py \
  --manifest paper/extension-tasks.json \
  --repo . \
  --tool build-san/joggle \
  --build build-san \
  --module-path examples \
  --module-path build-san/modules \
  --output paper/data/extension-footprint-pilot.csv
```

The committed CSV is a reproducibility check and pilot description, not the
answer to RQ2.

## Controlled comparison required for RQ2

Each comparison system receives a task specification containing only the
required behavior, input model, expected artifact, numerical oracle, and stop
conditions. Its implementation must follow the system's documented extension
path. Before starting, record the system revision, dependencies, build mode,
developer experience, and any unavailable feature.

For every task collect wall-clock implementation time with interruption logs,
files and source lines added or changed, generated definitions, native
registrations, build dependencies, clean build time, test commands, failed
attempts, and final diagnostics. A second developer must reproduce the result
from the task specification and artifacts. The paper reports each dimension
separately; it does not collapse them into an unvalidated effort score.

The intended matched comparisons are:

- implementation task: TVM TIR schedule or tensor implementation and
  RISE/Shine rewrite path;
- policy task: TVM scheduling/cost-model interface;
- external-kernel task: TVM BYOC or IREE/MLIR documented plugin path;
- numeric-format task: an MLIR type/dialect extension with the minimum target
  representation required by the same oracle.

These assignments remain provisional until exact versions and task feasibility
are checked. ONNX-MLIR is more appropriate for a separate frontend-semantics
task than for all four extension tasks.

## Threat controls

- Freeze task text before implementing comparison baselines.
- Keep training and measured runs separate.
- Preserve unsuccessful attempts and diagnostics.
- Report author familiarity and learning material used.
- Do not infer general usability from one expert or one task.
- Do not claim independence for repeated work by the same developer.
- Keep raw timing and command logs, not only derived tables.
