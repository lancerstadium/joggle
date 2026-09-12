# Roadmap

Joggle now has a coherent core, language, module system, two model frontends,
two artifact paths, and inspectable neural-network semantics. The next phase is
not another redesign. It is to close the gaps that determine whether the
workbench is useful in a real co-design study and whether its claims survive
evaluation.

## Current baseline

The repository currently provides:

- one `Mod/Fn/Blk/Op/Val/Ty/Attr` IR and one `.jog` source format;
- structural generics, overload resolution, compile-time functions, ordinary
  control flow, and open metadata;
- source-only modules with dependency closure, visibility, installation,
  upgrade, and optional native bindings;
- transactional `read`, `run`, `query`, and `emit` workflows;
- ONNX and TFLite codecs separated from explicit semantic conversion;
- reusable tensor, neural-network, quantization, and scalar-math functions;
- capability-driven exposure instead of a central lowering table;
- signature-matched selection of inspectable or external implementations,
  including inspectable candidate sets, per-candidate predicates, whole-set
  selectors, and call-site-derived C ABIs for generic external declarations;
- safe IR editing, cleanup, range analysis, measurements, static storage
  planning, and conservative loop transformations;
- configurable C generation, generic external-call ABI derivation, and a
  deterministic VM;
- an out-of-tree custom kernel and a parametric number format spanning semantic
  and target modules.

The default tests protect these contracts. They do not establish broad model
coverage, performance, or superiority over another compiler.

## Engineering priorities

### Scheduling and dependence

The loop API must move from isolated demonstrations to dependable research use:

- make dependence checks precise for nested and multi-axis loops;
- define legality for interchange, fusion, splitting, and unrolling;
- preserve reductions and loop-carried values under composition;
- extend the current read-only unroll/fusion candidate enumeration and ordinary
  function policies to interchange and other loop transforms;
- extend the current unroll/fusion rejection reasons to split, interchange,
  and future loop transforms;
- separate legality, profitability, and mechanism.

The goal is not an automatic scheduler. It is a small, inspectable substrate on
which a researcher can implement and compare scheduling policies.

### Generated-code quality

The C path must produce a stable baseline suitable for experiments:

- canonicalize loops and remove avoidable temporaries after exposure;
- propagate alias, alignment, constness, and storage facts from modules;
- complete static workspace planning for realistic tensor lifetimes;
- keep weights in a deterministic external payload when requested;
- make ABI and scalar representation configuration explicit;
- preserve source names by default and use short role-based names only for
  anonymous generated values;
- compare source, compiler diagnostics, binary size, workspace, and latency
  against defined baselines.

Every generated artifact used in evaluation must be checked against reference
outputs with documented tolerances.

### Neural-network coverage

Coverage should be measured by end-to-end models, not operator counts:

- pin authoritative ONNX and TFLite models with checksums and licenses;
- record decode, type refinement, semantic conversion, exposure, emission,
  compilation, and execution status separately;
- add missing semantics when a selected model or extension study needs them;
- cover common activations, broadcasting, partial shapes, quantization, and
  multi-output or control-flow cases;
- report unsupported frontiers without format-specific target cases.

Model artifacts must live under the repository's managed data workflow, not in
temporary directories.

### Extension quality

The central usability claim needs controlled evidence:

- implement a new data representation without changing the core;
- implement a semantic operation and inspectable body;
- implement a transformation with a replaceable policy;
- implement a genuinely different artifact target or simulator;
- measure touched files, core changes, source volume, dependencies, build time,
  and diagnostic quality;
- repeat representative tasks in selected comparison systems using their
  recommended extension mechanisms.

Examples must show the source module, invocation, transformed IR, emitted
artifact, and verification command.

### Reliability and packaging

Before a public artifact release:

- fuzz the parser, attribute decoder, module loader, and binary codecs;
- test malformed modules, incompatible upgrades, rollback, stale handles, and
  deterministic output;
- run sanitizers and supported-platform CI;
- document API and module compatibility rules;
- provide a reproducible release archive with licenses and checksums;
- keep generated files, downloaded models, and measurements out of source
  directories.

## Paper readiness

The paper is organized around four questions:

1. Can conventional inference workloads remain in one readable function IR
   while progressively exposing only the detail required by an experiment?
2. Does the function-and-module boundary reduce the work and coupling needed to
   add a format, semantic implementation, transform, or target?
3. Do independently written modules compose safely, with useful diagnostics
   and deterministic artifacts?
4. Can user-defined scheduling and storage policies improve edge-inference
   artifacts without core changes, and what overhead does the workbench add?

A submission is ready only when:

- every claimed capability has a stored reproducer;
- numerical results cover more than toy inputs;
- performance tables include baselines, warm-up, repetitions, dispersion, and
  hardware/software configuration;
- negative results and unsupported models are reported;
- comparison tasks are fair and version-pinned;
- the artifact rebuilds from a clean checkout without network-dependent build
  steps;
- paper figures and tables are generated from committed raw data;
- the limitations section matches the measured boundary.

The working evidence plan and outline are in
[paper/README.md](../paper/README.md).

## Near-term sequence

The intended submission deadline is close, so work is ordered by evidence
value:

1. Freeze public terminology, documentation responsibilities, and exact paper
   claims.
2. Finish the reproducible experiment harness and extension study before
   adding convenience APIs.
3. Run model correctness, generated-code, and controlled extension
   measurements; archive raw outputs immediately.
4. Decide whether the evidence supports submission. If it does, freeze the
   artifact and write from the evidence map. If it does not, continue the
   engineering study rather than inflate claims.
5. After the submission decision, resume broader operator coverage and
   scheduling features.

## Compatibility policy

Joggle remains pre-1.0. The seven public IR concepts, single textual format,
function-oriented extension model, and transactional invocation are intended
to remain stable. Individual module APIs may still change when evaluation
reveals an unnecessary concept or an unsafe contract.

Any incompatible change must update its implementation, tests, examples, and
the relevant reference document in one commit. Development-phase names and
version suffixes do not belong in public module or function names.

Generated C declarations and definitions are versioned together as artifacts.
The source-derived default symbol policy may change before 1.0; explicit `c`
bindings are the compatibility mechanism for externally fixed ABI names. The
project does not emit legacy aliases because that would turn historical naming
choices into permanent target behavior.
