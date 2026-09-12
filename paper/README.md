# Paper plan

This directory is the evidence workspace for a possible Joggle paper. It is not
a manuscript draft and it does not treat implemented features as validated
research contributions.

## Intended venue

The current target is the
[FSE 2027 Research Papers track](https://conf.researchr.org/track/fse-2027/fse-2027-papers).
The official deadline is October 2, 2026, Anywhere on Earth, as listed on the
[FSE 2027 dates page](https://conf.researchr.org/dates/fse-2027). Initial
submissions use the ACM `acmsmall` format and allow 18 pages of text and
figures plus 4 pages of references. The call encourages an anonymized,
curated, reproducible artifact.

The deadline is a decision point, not permission to overclaim. If the controlled
evaluation below is incomplete, the project should continue toward a later
venue.

## Working thesis

Researchers exploring neural-network software/hardware co-design should be able
to change semantic implementations, loop structure, storage, data
representation, and artifact generation without building a new compiler stack
or editing a central lowering registry.

Joggle tests one design response: keep imported calls, reusable semantics,
explicit loops, storage annotations, and target preparation in a single typed
function IR, and make each extension an ordinary distributable module function.

This is the claim to evaluate. “Small,” “easy,” “fast,” and “extensible” are not
paper claims until they have operational definitions and comparative evidence.

## Research questions

**RQ1 — Progressive representation.** Can conventional inference models be
imported, refined, converted, exposed, transformed, and emitted while remaining
in one readable function IR?

**RQ2 — Extension cost.** What code, coupling, dependencies, and core changes
are required to add a data representation, semantic implementation,
transformation policy, frontend, or artifact target?

**RQ3 — Composition and safety.** Can independently defined module functions be
composed with deterministic output, transactional failure, useful unsupported
frontiers, and no hidden pipeline state?

**RQ4 — Artifact quality.** What correctness, code-size, workspace, compile-time,
and latency results does the approach produce on resource-constrained inference
workloads, and can user-defined policies improve them without core edits?

## Candidate contributions

The paper may claim at most three contributions:

1. A function-oriented compiler workbench in which graph calls, semantic
   bodies, structured loops, storage decisions, and target preparation coexist
   in one typed IR.
2. A module boundary that uses ordinary functions for decoding, conversion,
   analysis, transformation, capability queries, and artifact generation,
   including capability-driven progressive exposure.
3. A controlled evaluation of extension cost, composition, correctness, and
   generated artifacts on conventional neural-network models.

The third contribution is currently incomplete and is the main submission
blocker.

## Evidence ledger

| Candidate claim | Current repository evidence | Missing evidence |
| --- | --- | --- |
| One IR spans graph and loop detail | Printer, verifier, semantic bodies, explicit loops, C and VM preparation tests | Model-level stage traces and comparison with multi-IR workflows |
| Extensions are normal module functions | Source modules, `local fn`, installation tests, `ir.invoke`, out-of-tree examples | Controlled implementation study with independent tasks and baselines |
| Frontends are separate from semantics | ONNX/TFLite codecs and explicit bridge modules | Broader official-model coverage and unsupported-frontier accounting |
| Targets expose only required detail | `c.accepts`, `vm.accepts`, `opt.expose`, preparation tests | A genuinely different external target or simulator study |
| Transform failure is safe | Transaction and rollback tests, ownership/liveness checks | Fault-injection matrix and diagnostic assessment |
| Storage and scheduling are replaceable | `mem` and `tile` modules, policy callbacks | Multi-axis legality, meaningful workload policies, performance results |
| C artifacts are usable | Compiled examples, independent weight payload, numerical checks | Standardized model suite, accuracy table, latency distribution, workspace and binary comparisons |
| VM execution is deterministic | Stable image format, output and step-count tests | Defined use case and overhead comparison |

## Current engineering observations

These numbers are useful for debugging and experiment design. They are not yet
paper results because the protocol, baselines, and repetition plan are not
frozen.

- The compiled MNIST path has matched its stored reference with maximum absolute
  error about `1.907e-05`.
- A MobileNetV2 path has matched its reference with maximum absolute error about
  `2.098e-05` after regeneration from the pinned original ONNX model.
- Externalizing the MobileNetV2 weight payload reduced generated C source from
  about 56.9 MB to 246 KB, with a separate payload of about 14.2 MB.
- On an unisolated Apple M4 pilot, strict generated C had a median MobileNetV2
  latency of 204.630 ms, fast-math/native C 112.958 ms, and single-thread ONNX
  Runtime 1.26.0 5.881 ms. The large gap is a blocking generated-code-quality
  result, not a favorable performance claim.
- Official SqueezeNet 1.1 and QDQ SqueezeNet 1.0 models now decode, refine,
  convert, expose, emit external-weight C, and compile. They have not yet been
  counted as numerically executed models because reference application inputs
  and outputs were not used in this pilot.
- A deterministic VM execution of the exposed MobileNetV2 program reported
  95,592,386,975 steps.
- A structural fusion experiment reduced loops from 374 to 328 and tensor
  constructions from 155 to 109, but the broad fused variant was slower in the
  local pilot. The selected variant also did not establish a meaningful speedup.
- A user policy expanded 72 statically bounded MobileNetV2 loops with trip
  counts at most three. Generated C grew by about 26%, while the 20-run median
  changed by only -0.22%, within the unisolated pilot's noise. Small-loop
  unrolling is therefore not the next generated-code priority.
- Mixed-stage specialization instead removed 328 rank-traversal loops and all
  492 dynamic compound-list indices from MobileNetV2. External-weight C shrank
  from 244,239 to 226,855 bytes. The official 1,000 outputs retained maximum
  absolute error `2.0980835e-05`; a matched 20-run pilot changed the median from
  204.549 to 201.240 ms (-1.62%). This is promising mechanism evidence, not a
  broad performance claim.

Raw pilot records belong in [data/](data/). Negative results must remain visible:
they show that structural simplification is not a substitute for a cost model
or target-aware measurement.

## Evaluation design

### Study A: extension effort

Implement representative tasks in Joggle and selected comparison systems using
their documented extension paths:

- one parametric scalar or packed data representation;
- one neural-network semantic implementation with an inspectable body;
- one loop or fusion policy;
- one artifact target or deterministic simulator.

Record changed files, source lines, generated code, core modifications,
dependencies, clean build time, implementation time, and failure diagnostics.
Task specifications and stopping rules must be fixed before measurement.
Repository size or line count alone is not a usability result.

### Study B: model coverage and correctness

Use checksum-pinned, licensed models from authoritative ONNX and TFLite sources.
The initial suite should cover at least image classification, an audio or
sequence workload, and a model with partial or dynamic shape computation.

For every model, report these stages independently:

1. decode;
2. verify and refine types;
3. convert to shared semantics;
4. expose to the selected target boundary;
5. emit and compile;
6. execute and compare with a reference runtime.

Report maximum absolute and relative error, task-level accuracy where
applicable, unsupported calls, compilation time, and artifact size. A decoded
model is not counted as an executable model.

### Study C: artifact quality

Compare unmodified Joggle output, user-defined policies, and appropriate
reference runtimes or compilers on named hardware. Record:

- end-to-end and kernel latency with warm-up and repeated trials;
- median, dispersion, and run count;
- peak or statically planned workspace;
- source, object, executable, and weight-payload size;
- host compilation time and generated compiler diagnostics;
- numerical agreement and task accuracy;
- deterministic hashes for IR and artifacts.

Use generated C as a transparent experimental baseline, not as a claim to
replace a production runtime. Inspect generated loops and compiler reports when
a transformation loses performance.

### Study D: safety and composition

Construct module sequences that succeed, reject an unsupported frontier, fail
mid-transaction, load malformed input, upgrade incompatibly, and retain unknown
metadata. Verify rollback, deterministic diagnostics, and stable output hashes.
Repeat the sequences through both the CLI and embedding API.

## Baseline selection

Comparisons should answer a specific question:

- ONNX Runtime or TensorFlow Lite for reference correctness and deployment
  context;
- TVM or IREE for established compiler workflows;
- ONNX-MLIR for an ONNX-to-compiled-artifact workflow;
- TileLang only when comparing user control over generated kernels;
- a small direct C implementation when isolating abstraction overhead.

The paper must not claim that all of these systems solve the same problem.
Versions, configurations, target flags, and unavailable features must be
recorded.

## Related-work audit

Before drafting prose, build a claim-oriented matrix from primary papers and
official documentation for MLIR, TVM/Relax/TIR, IREE, ONNX-MLIR, TileLang,
Lift/Rise, and representative edge-inference compilers. For each system record:

- its user-facing extension unit;
- the representations crossed by a new operation or target;
- how semantics, legality, scheduling, and code generation are separated;
- which steps require generated code, native registration, or core changes;
- its intended deployment scope and evaluation subjects.

The purpose is to identify the narrow difference that the evaluation actually
tests, not to declare every adjacent system a competitor. Bibliographic
metadata and quotations must be verified against the primary source before
they enter the manuscript.

## Planned paper structure

1. **Introduction:** the co-design iteration problem, thesis, and measured
   contributions.
2. **Motivating study:** one change spanning data representation, semantic
   implementation, scheduling, and artifact generation.
3. **Design:** one IR, structural types, function resolution, modules,
   transactions, and progressive exposure.
4. **Implementation:** core/runtime boundary, codecs, bridges, semantic
   libraries, analyses, transforms, and targets.
5. **Evaluation:** research questions, subjects, baselines, protocols, results,
   and negative findings.
6. **Related work:** compiler infrastructures, tensor compilers, scheduling
   languages, deployment compilers, and extensible systems.
7. **Limitations and threats:** coverage, manual policy, C quality, dynamic
   shapes, measurement bias, and external validity.
8. **Conclusion:** only conclusions supported by the evidence ledger.

## Submission blockers

- Freeze the comparison systems and version-pinned experimental protocol.
- Complete the extension study instead of inferring ease of use from examples.
- Complete the claim-oriented related-work matrix with verified primary
  citations.
- Run a representative model suite through execution and accuracy checks.
- Improve or honestly bound loop legality and generated-code quality.
- Store raw timing, memory, accuracy, build, and artifact-size data.
- Generate every table and figure from those raw records.
- Create a clean, anonymized artifact and reproduce it on a second machine.
- Run an internal claim-to-evidence and citation audit.

## Research integrity

The manuscript will distinguish design intent, implemented capability, pilot
observation, and controlled result. Every factual claim about related work
requires a verified primary citation. Every empirical claim requires a script,
raw record, environment description, and derivation.

The final package must include data and artifact availability, author
contributions, funding, conflicts of interest, limitations, ethics where
applicable, and a truthful AI-assistance disclosure consistent with the venue
policy.
