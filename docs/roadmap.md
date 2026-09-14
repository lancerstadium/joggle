# Roadmap

Joggle now has a coherent core, language, module system, two model frontends,
two artifact paths, and inspectable neural-network semantics. The next phase is
not another redesign. It is to close the gaps that determine whether the
system is useful in a real co-design study and whether its claims survive
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
  selectors, ordinary configuration dictionaries, and call-site-derived C
  ABIs for generic external declarations;
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
- keep read-only candidate enumeration and ordinary function policies
  consistent as further loop transforms are added;
- keep rejection reasons aligned with the exact legality used by every edit;
- separate legality, profitability, and mechanism.

The first read-only layer is now present: `tile.depends` follows conservative
value dependence through nested blocks, and `tile.axes` projects that fact onto
the axes of an existing loop. `tile.reads` and `tile.writes` expose grouped
index values while following store and structured carried bindings. They are
validated on a small conditional grid, a five-axis generic reduction, and the
seven-axis spatial-convolution body. `tile.affine` proves exact integer affine
forms and also uses static loop ranges to prove constant integer quotients. For
example, `m / 160` becomes zero only when the represented range proves
`0 <= m < 160`; other nonlinear, truncating, unsupported, or
coefficient-overflowing expressions remain rejected. `tile.reorder` combines
those forms
with static bounds, carried-state accesses, an affine injectivity proof, and
stable state/reduction subsequences. This covers a useful reduction-preserving
interchange class without claiming general cross-iteration dependence analysis;
multiple states, dynamic bounds, non-affine accesses, and general imperfect
nests remain open. Its explicit, candidate-enumeration, read-only policy, and
configured policy forms now follow the same convention as unrolling and
fusion.

Affine and axis-role queries now receive the owning `Mod` explicitly. Their
algebraic proof follows only operators resolved to `base`, while unresolved
built-in syntax remains compatible and user overloads are conservatively
rejected. Loop legality therefore cannot inherit arithmetic laws from a shared
operator spelling. Access and range queries use their resolved structural
operators instead: custom containers retain `[]`, `[]=`, and `..` semantics
without registering a second access descriptor.

The first tensor-traffic rewrite is also present. `tile.scalarize` promotes one
or a statically bounded tile of affine output elements across a structurally
proved reduction band, leaving one load and store per carried scalar around the
reduction. It is validated on the same Conv body and on a differently ranked
generic reduction, without operator names or rank cases. The same rewrite
canonicalizes proven affine indexed operands and drops their private expanded
address calculations, reducing the generated Conv body without a C-emitter
peephole. A scalar budget bounds live accumulators; the separate read-only
`tile.scalar_cost` query reports a target-neutral source-body duplication
proxy before an edit. Static tensor-capacity and affine address-range proofs
reject padded state domains before loads or stores are hoisted. Target latency,
emitted size, and automatic profitability selection across interchange,
promotion, and tiling remain open.

`tile.fuse` now proves pointwise correspondence from affine addresses across
equal multi-axis loops or a dense multi-axis-to-linear boundary. It can forward
a private scalarized reduction through following pointwise computation without
materializing the producer tensor, and it recomputes legal pairs after every
edit. A MobileNetV2 development diagnostic reduces 155 top-level source loops
to 55, external-payload C source from 150,624 to 136,452 bytes, and planned
workspace from 2,860,032 to 2,107,392 `f32` elements while preserving the
reference output within the existing tolerance. These are structural and
correctness observations on an uncontrolled host, not latency evidence. The
next evaluation step is an independently dispatched Linux comparison against
the unchanged baseline and ONNX Runtime.

The source-only `spatial.block` example now accepts an ordered factor list and
prefers an exact factor per proved state extent in one module traversal. When
none divides the extent, `tile.peel` separates an aligned prefix from a scalar
tail before the same split, reorder, and promotion sequence. The legality proof
permits only leading state coordinates and therefore cannot separate a
reduction tail. Same-process MobileNetV2, SqueezeNet, and two-result UltraFace
diagnostics exercise exact and non-exact paths without inspecting an operator
name. The runs are unisolated and substantially increase C source, so they
motivate a controlled multi-model study rather than a default schedule. The
policy now optionally enforces a whole-invocation structural-duplication limit
using `tile.scalar_cost` before any split or reorder. Target-dependent factor
choice, packing, direct artifact-size modelling, and profitability remain
policy and mechanism gaps.

`spatial.plan` now exposes the static extents and legal scalar-duplication cost
beside each selected order, so an external policy can rank the same candidates
without reparsing printed IR. A same-process MobileNetV2 diagnostic that spent
the existing budget on the largest reduction extents did not beat the simpler
reorder policy and increased transform time, so that ranking was not retained
as behavior. Compiler optimization remarks show that the reordered inner state
loops already reach loop or SLP vectorization on the development toolchain.
The first follow-up mechanism is now `tile.canon`: it reconstructs an
exclusively used affine index from the proved form and batch-erases the old
stride-update tree. On the current MobileNetV2 reorder artifact this reduces C
source from 196,011 to 152,302 bytes, remains byte-idempotent, preserves the
stored output exactly, and improves the same-process direction relative to
reorder alone. These development-host observations justify a controlled Linux
run; they are not publication performance results. Layout reuse and state-axis
coalescing remain subsequent mechanisms rather than scalar-body duplication.

Primitive loop edits do not run a hidden whole-module cleanup. In particular,
`tile.peel` performs only the checked local replacement; a composing policy
may apply many edits and invoke `opt.dce` once at its boundary. This keeps the
single-edit API predictable and prevents repeated full-program scans on large
function bodies.

General loop-invariant motion now lives in `opt.hoist`, not in an operator or
artifact emitter. A module supplies either a short list of calls that are safe
to speculate or a read-only policy function; constants need no policy. The
edit crosses blocks only after the core checks function ownership, binding
collisions, nested-body cycles, and whole-function dominance. It is deliberately
opt-in: an initial MobileNetV2 pilot preserved generated output but did not
improve latency, so profitability and target-sensitive placement remain open
rather than becoming hidden defaults.

`tile.split` now strip-mines an explicitly selected range axis rather than
assuming the last axis. It inserts adjacent outer and inner axes, preserving
the original lexicographic order for arbitrary multi-axis loops, and represents
the tail with ordinary structured control flow. This supplies a composable
mechanism for later state tiling without a Conv, GEMM, rank, or target case.

The complementary `tile.merge` now collapses one adjacent static axis pair
into a linear range and reconstructs both original coordinates in the existing
body. It preserves lexicographic order and arbitrary carried values, rejects
dynamic, empty, and overflowing domains, and composes across more than two
axes. An exclusively address-used affine expression whose adjacent
coefficients match the range radix is rebuilt directly from the merged axis;
non-address coordinate uses retain explicit quotient and remainder. The
mechanism is target-neutral; deciding when fewer loops outweigh the remaining
coordinate reconstruction remains an explicit policy and measurement question.

Conservative integer bounds now feed an explicit `bounds.fold` edit for exact
Boolean predicates. Composing it with ordinary `opt.fold` removes statically
proved control flow, including no-padding Conv guards, while retaining dynamic
activation conditions. The affine query separately recognizes division only
when coefficient-wise exact or when the quotient is constant over a static
loop box. General correlated division, branch-sensitive range refinement, and
symbolic shape constraints remain open rather than being approximated.

The same interval facts now prove finite storage for runtime shapes assembled
through ordinary `tensor` writes. `mem.bound` records only a checked capacity
shape, and `mem.plan` consumes it without moving logical extents into `Ty` or
introducing a device model. General data-dependent output sizes without a
finite interval remain an explicit frontier.

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

Shape specialization now removes static-rank traversal from ordinary tensor
offset and broadcast helpers. Conservative cleanup runs after exposure but
does not rewrite mutable updates as algebraic expressions. It does not yet
eliminate every resulting scalar temporary or materialized broadcast, and the
measured convolution/layout gap to a production runtime remains the primary
backend limitation. Block-local dead-code indexing and batched exposure allow
the current DenseNet path to finish preparation, but its 271.92-second pilot
and approximately 29.2x generated-C latency gap remain explicit scaling and
code-quality targets rather than evidence of a mature backend.
The C emitter now constructs external-payload offsets once and reuses pure
snapshot-relative ABI, type, and naming queries. On one placed UltraFace pilot
this preserves byte-identical source while reducing a report-enabled emission
from 191.51 to 38.65 seconds. Controlled repetitions and larger-model scaling
are still required before treating this as a compiler-throughput result.
Non-empty fixed-shape tensor parameters and results now retain their exact
minimum element count in C11 function definitions while public declarations
keep the compatible C/C++ pointer ABI. A paired UltraFace diagnostic found
byte-identical outputs and performance parity with the pointer-only spelling;
the change exposes an existing type fact to downstream compilers but is not a
speedup claim.
An explicit per-entry `c.noalias` contract reaches tensor and payload pointers
in C definitions and the structured API without contaminating the portable
header. The first proof-derived path is also present: `mem.separate` recognizes
pairwise-distinct planned slots and immutable payloads at one call, while
`c.restrict` uses a single call index to strengthen a private function only
when all of its calls qualify. It intentionally rejects entry parameters,
returned or unplanned storage, implicit payload users, and repeated operands.
An UltraFace diagnostic qualifies 72 of 139 private functions; after replacing
the initial repeated module scan, the pass takes 6.05 rather than 69.13 seconds
and emits byte-identical IR. A same-process paired run indicates a useful
latency direction, but controlled repetition, alignment propagation, and a
broader storage-origin proof remain open.

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

The SSD-MobileNet regression currently converts all 92 runtime-control Slice
sites through the shared tensor body and leaves 23 source calls.  The remaining
frontier is concentrated in dynamic broadcast/index families and nested
control flow, not a missing Slice emitter.  The next coverage step should add
shared runtime-shape and selection mechanisms for those families, then use the
same model-level frontier gate; it should not add one backend case per ONNX
operator.

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
- keep Linux/macOS builds and a Linux sanitizer configuration green in CI;
- document API and module compatibility rules;
- provide a reproducible release archive with licenses and checksums;
- keep generated files, downloaded models, and measurements out of source
  directories.

The deterministic robustness gate now mutates complete `.jog` programs,
serialized attributes, and public `Ty` constructor inputs. Every accepted value
must print, reparse, and reproduce the same structure. A separate module-loader
gate distinguishes absent optional directories from filesystem failures and
checks that cyclic source, fragment, and native paths diagnose and roll back
without exceptions. It also loads a generated 48-module graph with deep,
rejoining dependency paths, qualified same-name exports, and one transitive
generic, then proves that a missing dependency publishes no partial closure and
that the same environment can retry successfully after the dependency appears.
A fixed-seed edit gate composes 2,000 constant insertions,
operation clones and moves, single-use replacements, erasures, renames, and
metadata edits. It checks bidirectional Def-Use links, parent blocks, stale
handles, revisions, verification, and canonical round trips after every
accepted mutation, while rejected edits must preserve text and revision.
Randomized directory trees, native ABI calls, ONNX protobufs, and TFLite
FlatBuffers still need independent fuzz or property-test harnesses rather than
being claimed by these gates.

Staged upgrades now preserve more than the replaced module's public signature.
The tool computes its transitive reverse-dependency closure, reloads those
installed modules with the candidate first on the search path, and rejects any
call whose previously resolved qualified declaration would disappear or
change. The old installation remains byte-identical on failure. Compatibility
of persistent metadata and native state across revisions remains outside this
pre-1.0 contract.

Installation and upgrade validation are also closed over explicit roots. The
candidate source directory contributes only the staged module itself; sibling
directories are not searched unless the caller supplies their root with `-M`.
Regression coverage rejects both a fresh install and an upgrade that would work
only while an undeclared source checkout remains beside the candidate.

The C++ implementation now keeps immutable attributes/types, structural
handles, value-family analysis, metadata editing, tokenization, parsing,
canonical printing, and verification in separate translation units while
preserving the single public IR. The remaining large `ir.cpp` and `eval.cpp`
files still need dependency-led separation of construction/inference and
runtime values/intrinsics; file size alone is not a reason to create another
public abstraction.
The structured-operation clone contract now propagates substituted carried
state names through nested block arguments, loop/branch results, and
assignments. A repeated-fusion regression must print, reparse, compile as
strict C, and execute correctly. This closes a real-model failure without
creating a printer repair pass.

## Paper readiness

The paper is organized around four questions:

1. Can one typed module participate in vocabulary, analysis, transformation,
   choice, and artifact roles without adding a host-side extension category?
2. Can a program expose more implementation detail to one consumer without a
   mandatory whole-program transition or loss of readable identity?
3. Do independently written modules compose and evolve safely, with useful
   diagnostics, rollback, dependency-aware upgrades, and deterministic
   artifacts?
4. In the inference stress domain, can user-defined scheduling and storage
   policies improve complete artifacts without core changes, and what overhead
   does the system add?

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

1. Run the new fusion path and its unchanged baseline as independently
   dispatched, pinned Linux jobs; archive raw correctness, workspace, source,
   binary, compiler-diagnostic, and timing records.
2. Repeat the structural and numerical checks on the selected multi-model
   corpus, reporting unsupported boundaries instead of adding operator cases.
3. Update the paper's mechanism figure, claim-evidence map, tables, abstract,
   and title only from committed records.
4. Apply targeted maintenance when an experiment exposes a core contract
   failure. Defer broad `ir.cpp`/`eval.cpp` file splitting until after the
   evidence freeze unless it blocks reproducibility.
5. Run citation, anonymity, artifact, and independent-review gates before the
   submission decision. Resume broader coverage and scheduling work after that
   decision.

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
