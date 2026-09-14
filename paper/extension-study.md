# Extension study protocol

This workspace contains four different studies that must not be collapsed into
one “extensibility” number:

1. **role breadth:** can an installed typed module contribute vocabulary,
   analysis, mutation, target choice, and artifact behavior through one public
   mechanism;
2. **module lifecycle:** can source-only and native modules be validated,
   installed, upgraded, resolved with their dependency closure, and uninstalled
   without rebuilding the host, while a staged upgrade preserves the resolved
   calls of its transitive reverse dependencies;
3. **vertical closure:** can one inference/hardware idea retain semantics,
   fallback, representation, structural policy, target binding, validation, and
   artifact behavior; and
4. **evolution continuity:** what breaks when that complete idea is revised after
   the initial implementations are frozen.

The existing four contracts cover role breadth. They do not by themselves prove
vertical closure, evolution continuity, usability, or generality. The module
lifecycle is covered by project tests but still needs clean-install measurements.
The vertical and revision studies remain submission blockers. Source lines are
never treated as developer productivity, and the existing authors are not
treated as independent participants.

## Role-breadth contracts

The system-neutral contracts and the corresponding Joggle records are listed in
[`extension-tasks.json`](extension-tasks.json):

1. replace a shared matrix implementation with an inspectable loop body;
2. define a structural cost measure and fusion-selection policy;
3. connect generic external matrix and convolution kernels;
4. add a parametric saturating integer format with two target representations.

Each contract fixes repository inputs, observable requirements, and forbidden
shortcuts before a comparison implementation begins. They are capability probes,
not four independent user studies. The first three Joggle
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

## Matched role-breadth comparison

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

The role-breadth comparison has two levels. They answer different questions and
must not be collapsed into one ranking or substituted for the revision study.

1. **System-level extension path.** ONNX-MLIR is the primary baseline for the
   complete neural-network compiler workflow: ingest the same ONNX fixture,
   introduce the task-specific implementation through its documented
   operation or accelerator path, run the same oracle, and preserve the
   generated artifact. An unsupported contract remains a measured outcome; it
   must not be replaced by a smaller standalone MLIR example.
2. **Mechanism-level controls.** TVM is retained for the implementation,
   scheduling-policy, and external-kernel tasks because those contracts map to
   documented TIR, schedule, and external-call mechanisms. RISE/Shine is a
   prospective control for typed rewriting. A standalone MLIR dialect/type
   experiment is only a component-level decomposition of the work required
   inside the ONNX-MLIR stack; it is not an end-to-end competing compiler.

The ONNX-MLIR comparison must record both the user extension and any required
changes to generated operation definitions, dialect registration, pass
registration, type conversion, build configuration, or driver wiring. This is
the relevant boundary because the official project imports ONNX, lowers it to
native artifacts, documents generated ONNX operation definitions, and exposes
an accelerator integration path for dialects and passes. The task-to-hook
protocol is recorded in
[`baselines/onnx-mlir/README.md`](baselines/onnx-mlir/README.md). The
implementation-task fixture is checked in under `fixtures/implementation` and
is executed by Joggle's normal ONNX path. Fixtures for the other contracts must
be frozen before their ONNX-MLIR implementations or measurements begin.
The policy fixture is frozen under `fixtures/policy`: it is the contract's
four length-four inputs expressed as an unchanged three-`Add` ONNX chain with
the same expected result. The numeric-format input is frozen under
`fixtures/numeric-format` as one standard opset-13 int64 Add graph plus a
digest-pinned format map assigning `sat<W>` to named nodes. This split does not
pretend that ONNX natively defines the new type: it gives every system the same
interchange graph, format policy, values, and saturating oracle.

TVM `v0.26.0` at commit
`c7b458e946bc4266915da582457476bdcd9705ae` is now pinned. Reproducible TVM
programs pass the implementation, policy, and external-kernel contracts; their
sources and build recipe are preserved under `baselines/tvm/`. The numeric-
format task has a preserved unsupported result at its first mandatory
custom-type requirement: the pinned public Python surface can neither import
the custom-datatype registration module nor construct `custom[sat]5`.
ONNX-MLIR is pinned at commit
`4a13c34aa695b228599d637cdb772c19b4b18dba`. Its implementation task now passes
end to end through the documented accelerator path: the unchanged ONNX fixture
lowers to an explicit `i-k-j` loop nest, compiles to a native shared library,
and passes the numerical oracle. The exact six-file extension, emitted IR, and
result record are preserved under `baselines/onnx-mlir/implementation/`. The
policy task also passes through a separate accelerator-scoped analysis and
transformation. The external-kernel task is unsupported at its first mandatory
case. For numeric format, a separate accelerator registers `!sat.int<W>`,
materializes nested tensor element types, emits collision-checked helpers, and
executes every frozen scalar and tensor case through a native artifact with
zero error. ONNX-MLIR exposes no second executable C or deterministic-VM target
corresponding to the frozen requirement, so that task is preserved as
unsupported at the second target rather than relabeled as a pass. An
uninterrupted clean-build repetition remains incomplete.
RISE/Shine remains provisional; standalone MLIR observations may explain
plumbing cost but cannot close RQ2.

## Threat controls

- Freeze task text before implementing comparison baselines.
- Keep task construction, implementation, and measurement revisions separate.
- Preserve the exact final patches, generated definitions, and diagnostics.
- Report unavailable requirements rather than weakening a task after starting.
- Do not infer general usability from structural extension measurements.
- Keep raw build timing and command logs, not only derived tables.
