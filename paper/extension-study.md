# Extension study protocol

This study tests whether Joggle's function-and-module boundary supports bounded
neural-network co-design changes without modifying a central compiler registry.
It does not equate source lines with usability and does not treat the existing
authors as independent participants.

## Frozen task contracts

The system-neutral contracts and the corresponding Joggle records are listed in
[`extension-tasks.json`](extension-tasks.json):

1. replace a shared matrix implementation with an inspectable loop body;
2. define a structural cost measure and fusion-selection policy;
3. connect generic external matrix and convolution kernels;
4. add a parametric saturating integer format with two target representations.

Each contract fixes repository inputs, observable requirements, and forbidden
shortcuts before a comparison implementation begins. The first three Joggle
records are out-of-tree extensions. The numeric format is explicitly
classified as a bundled optional module because it also exercises the native
module ABI; it must not be reported as a zero-build-system-change task.
Supplied fixtures, C kernels, and harnesses are task inputs and are not counted
as implementation source for any system.

## Mechanically collected observations

`measure_extensions.py` validates every declared module and task test, then
records source files, nonblank non-comment source lines, bytes, declared module
dependencies, public functions, and a digest over the exact source paths and
bytes. These quantities describe the footprint of the checked-in
implementation. They do not measure comprehension,
developer productivity, difficulty, or correctness beyond the named tests.

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

## Matched comparison required for RQ2

Each comparison system receives the same manifest contract of repository
inputs, observable requirements, and forbidden shortcuts. Its implementation
must follow the system's documented extension path. Before starting, record the
system revision, dependencies, build mode, documented extension path, and any
unavailable requirement.

For every task collect files and source lines added or changed, framework-core
and build-system edits, generated definitions, native registrations, new build
and runtime dependencies, clean build time, oracle commands, and the diagnostic
for every unavailable requirement. Preserve the final patch and build record so
another machine can reproduce them from the contract. The paper reports each
dimension separately; it does not translate them into developer time,
productivity, or a synthetic ease-of-use score.

The intended matched comparisons are:

- implementation task: TVM TIR schedule or tensor implementation and
  RISE/Shine rewrite path;
- policy task: TVM scheduling/cost-model interface;
- external-kernel task: TVM BYOC or IREE/MLIR documented plugin path;
- numeric-format task: an MLIR type/dialect extension with the minimum target
  representation required by the same oracle.

TVM `v0.26.0` at commit
`c7b458e946bc4266915da582457476bdcd9705ae` is now pinned. Reproducible TVM
programs pass the implementation, policy, and external-kernel contracts; their
sources and build recipe are preserved under `baselines/tvm/`. The numeric-
format contract remains unimplemented, so there is no complete TVM result and
no cross-system conclusion yet. RISE/Shine and MLIR assignments remain
provisional until the same feasibility check is complete. ONNX-MLIR is more
appropriate for a separate frontend-semantics task than for all four extension
tasks.

## Threat controls

- Freeze task text before implementing comparison baselines.
- Keep task construction, implementation, and measurement revisions separate.
- Preserve the exact final patches, generated definitions, and diagnostics.
- Report unavailable requirements rather than weakening a task after starting.
- Do not infer general usability from structural extension measurements.
- Keep raw build timing and command logs, not only derived tables.
