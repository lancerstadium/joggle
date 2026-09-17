# Research and submission plan

Frozen: 2026-09-15. This is the single active plan for the EuroSys submission.
The engineering backlog remains in [the roadmap](../docs/internals/roadmap.md), verified
source comparisons in [the literature map](studies/literature-map.md), and experimental
procedures in their existing study documents. Change this file in place; do
not create parallel dated plans.

## Position

**Joggle: Malleable Compilation with a Progressive Intermediate Representation**

The title is fixed unless the project owner explicitly reopens it.

Joggle makes computations and compiler behavior inspectable, editable programs.
Modules let a researcher progressively specialize implementation details and
reuse existing analysis, transformation, and execution facilities. Neural
inference and hardware co-design are demanding applications, not restrictions
on the compiler's scope.

The primary research question is whether a working compiler algorithm can be
retained, derived, internally revised, and independently executed as a typed
program artifact. A secondary comparative question is whether this boundary
reduces revision and rebuild work relative to another system's natural route.
The current paper may answer the first without claiming the second. Naming
every action a function is insufficient: the function body must actually be
available to the relevant edits and execution.

## Submission thesis freeze

**One-sentence thesis.** Joggle makes compiler implementations malleable by
representing source-defined compiler algorithms as derivable typed functions:
a researcher can copy an algorithm and its private helper closure, revise an
internal decision, and execute the derived definition on a separate subject
while retaining the original and explicit module, validation, and rollback
boundaries.

This is the paper's only primary claim. Progressive IR is the common substrate,
not a separate mixed-level novelty claim. Modules, replacement validation,
query isolation, and rollback make derivation usable; they are supporting
mechanisms rather than competing headlines. Consumer-directed exposure shows
how the same representation introduces subject detail on demand. Precise
dependency-sensitive recomputation remains a design direction and is not part
of the claimed implementation.

The novelty boundary is intentionally narrow. Transform IR and Exo 2 already
make transformation strategies programmable; Relax and MLIR already support
mixed representations; modular compiler frameworks already compose language
and compiler definitions. Joggle asks a different systems question: when the
desired change lies inside a source-defined compiler algorithm, can its
implementation body itself be retained, derived, edited, and independently
executed with the same structural facilities used for computation? Do not
claim that competitors cannot encode the workflow. Compare the natural route
and measure the boundary.

The evidence hierarchy is fixed:

1. **Decisive mechanism evidence:** the storage-planner derivation changes a
   real internal decision, preserves the original, reaches a numerically checked
   artifact, and is compared with the same source-level change. The external
   TVM natural-route control was executed on September 16 on one model and one
   host ([record](experiments/derive-prepare.md#external-natural-route-control));
   it measures a rebuild and feedback boundary, not developer effort.
2. **Supporting systems evidence:** private-closure capture, separate compiler
   and subject modules, replacement checks, failure rollback, stale-body tests,
   and selective exposure establish the operational boundary.
3. **Breadth and limitations:** extension tasks and pinned models show reach;
   compilation costs, timeouts, unsupported paths, and slower scalar-C runtime
   show the current boundary. They are not aggregate performance claims.

A reviewer should be able to answer four questions in order: what object becomes
newly editable; how the original and derived definitions remain isolated; how
the change reaches a checked artifact; and what it costs or fails to handle.
Any paragraph, figure, or experiment that does not answer one of these questions
is supporting material at best and should not receive equal rhetorical weight.

## Argument to preserve

Approved main line: **研究者同时改变计算本身和实现计算的方法。定制应能深入实现，
扩展应能独立组合，更新代价应尽量随实际依赖变化。**

**Opening discipline:** Abstract and Introduction must first establish the
systems background: AI workloads expose the coupling of computation, numeric
representation, and hardware; development moves between portable prototypes
and specialized implementations, with execution feedback informing revisions.
Explain why compiler algorithms become part of this exploration before naming
compiler customization problems. Then discuss existing support, the three
obstacles, Joggle, and evidence. Do not open with an isolated dtype example or
the statement that compiler customization is difficult. The detailed format
scenario belongs in Motivation. AI motivates the demand, not an edge-only or
AI-only definition of Joggle's scope.

| Memorable obstacle | Concrete situation | Design response | Evidence needed |
| --- | --- | --- | --- |
| **能调用，难改造** / Restricted customization | A layout or analysis procedure is mostly reusable, but the desired algorithm change exceeds its configuration interface | Unified metaprogramming: inspect, derive, edit, and execute source-defined functions across roles | Retained implementation, extra integration, original/derived behavior, derivation and execution cost |
| **单点实现，跨层接线** / Fragmented integration | A working kernel still needs model selection, data conversion, fallback, and a matching artifact interface | Modular management: compose related definitions and check their dependencies and replacements | Complete callable artifact, independent composition, revision/rebuild scope, failure location |
| **局部改动，成片重做** / Redundant work | A packing or policy edit invalidates coarse caches or triggers scans unrelated to the changed definition | Reactive updates: consumer-directed exposure followed by dependency-sensitive recomputation | Affected versus visited IR, invalidated/recomputed queries, feedback time, memory, agreement with a fresh run |

These phrases describe testable obstacles, not universal claims that competing
systems cannot customize, compose, or cache. The English prose should explain
the scenario before introducing each mechanism. Do not replace the three
obstacles with generic claims of flexibility, modularity, or lightweight code.

Use one running scenario: a block-scaled representation moves from a numerical
prototype to a target implementation and then undergoes repeated revision.
Changing a packing traversal while preserving decoding differs from changing
scale selection and therefore numerical meaning. New hardware is a deployment
variation; an agent is a possible author of candidate revisions. Neither is a
separate headline contribution or an implemented advantage by association.

The [literature map](studies/literature-map.md) records the source
basis. Microscaling motivates the scenario; Joggle does not yet implement an
MX deployment path. The existing Conv/layout/ABI evolution protocol is a
bounded experimental instance, not microscaling evidence. Do not rename or
alter its frozen tasks to make the scenario appear implemented.

## Design philosophy

Agreed framing: **统一元编程（表达）＋模块化管理（组织）＋响应式更新（演化）**。
渐进式 IR 将三者连接起来。This framing guides subsequent discussion; the
title remains unchanged.

1. **Unified metaprogramming / 统一元编程.** Computations and source-defined
   compiler procedures share function, type, control-flow, and editing facilities.
   A function can inspect, derive, edit, and invoke another function. This applies
   to analyses, selection, transformations, and generation, not only passes.
2. **Modular management / 模块化管理.** Modules organize related definitions
   and their dependencies for composition, distribution, and revision. The design
   connects semantic definitions with the procedures that implement and manipulate
   them; it does not introduce a separate extension system for each role.
3. **Reactive updates / 响应式更新.** The design aims to update implementation
   details where consumer requirements or dependencies demand them. Current
   evidence covers capability-directed local exposure. Automatic dependency
   propagation, precise invalidation, and incremental recomputation remain
   research work; local exposure alone does not establish these capabilities.

These are the expression, organization, and evolution aspects of the design,
not three individually established novelty or performance claims. Unified
metaprogramming supplies the primary mechanism; modular management and current
consumer-directed exposure are supporting boundaries. Precise reactive
recomputation is future work. Their combined benefit must be grounded in the
mechanisms and evidence below.

## Concrete mechanisms and boundaries

1. **Progressive function representation.** Expose detail locally inside the
   existing `Fn/Blk/Op/Val` model; keep other functions abstract where appropriate.
   Record which enclosing identities survive an edit and which operation/value
   handles become invalid. Mixed levels alone do not distinguish Joggle from
   Relax or MLIR.
2. **Derivable compiler functions.** Inspect, copy, specialize, edit, and invoke
   source-defined functions using existing facilities. The objects include
   analyses, selectors, representation/packing procedures, transformations, and
   code generators, not only passes. Derivation must leave the original
   definition intact and preserve explicit dependency and execution boundaries.

Module loading implements the organizational boundary. Types, verification,
and rollback support safe use of these mechanisms; they do not introduce a
new taxonomy of roles. Function derivation is a concrete mechanism under
unified metaprogramming, not a competing third headline.
No `MetaPass` class, parallel compiler IR, generated wrapper layer, or second
DSL is planned. Native code is an explicit opaque boundary.

These components are not mandatory pipeline stages. Derivation edits
compiler definitions; module resolution supplies visible dependencies; selective
exposure edits the subject only when its consumer needs a body. The storage
case runs after shared preparation and must not be described as evidence of
reactive recomputation. The MobileNetV2 trace separately demonstrates mixed
subject representations. Explain their common representation without treating
these distinct observations as a combined performance or usability result.

The motivating example `f3(f2)` is one case of derivation, not an unrestricted
self-modifying compiler. First derive an inactive copy, then execute that copy.
Optimizing a procedure's body is different from invoking it to optimize its
input. Neither implies that arbitrary procedures can safely optimize themselves.

## Status and evidence obligations

| Item | Inspected implementation or preserved evidence | Still required |
| --- | --- | --- |
| Progressive representation | Common IR, function cloning, structural edits, semantic exposure, C/VM paths; [MobileNetV2 trace](experiments/progressive-trace.md) retains the enclosing function while selected calls and exposed loops coexist | External replay and numerical artifact validation of the selected path; current trace establishes typed IR legality only |
| Function execution | Typed invocation with/without a subject; separate code/model `Mod` values; copied generator edited; private lexical helper closure captured; real `c.prepare` algorithm edited and source-isolated on a typed-model regression and [four complete-model IR inputs](experiments/derive-prepare.md) across three network architectures | General rewrite conditions, matched mechanism control, and isolated cost beyond this pilot |
| Reuse across roles | Source modules implement analyses, policies, transformations and generation | Demonstrate independent composition, not merely several unrelated examples |
| Vertical evolution | [S0 preflight](studies/evolution-study.md) has six hash-matched, callable macOS artifacts; the initial-domain audit now separates host-core ABI work from registration/build wiring | Matched Linux S0 freeze, then unchanged-task S1/S2 implementations, adjacent-stage patches, and named revisited contracts |
| Derivation evidence | `c.prepare` isolation pilot remains a no-speedup result; the [storage-planner case](experiments/derive-prepare.md#complete-model-storage-check) reduces declared slots on UltraFace by 4.49%, with a matching source-patch control and tested name-independent structural editing | A second host and more inputs; the [second derivation case](experiments/derive-prepare.md#second-derivation-case-guard-folding-in-c-preparation) retargets the cleanup call inside `c.prepare`'s private helper, removes 18 provably true guards with bit-identical outputs and no latency change; the executed [TVM natural-route control](experiments/derive-prepare.md#external-natural-route-control) shows the same rule needs a one-unit compiler rebuild (8 s incremental) and raises TVM's planned storage by 1.06% under the same oracle, while Joggle's route needs no rebuild but plans about sixteen times slower; neither result is inference acceleration, plan quality across systems, or measured maintenance savings |
| Complete artifacts | Versioned ONNX/TFLite numerical paths and negative runtime/scaling records in the study files | Matched end-to-end measurements with environment and failure boundaries |
| Usability and agents | Text modules and shared APIs exist | No measured user productivity or agent success advantage; neither is a current result |

Automatic effect inference, cross-run dependency-aware caching, traversal fusion,
JIT specialization, and agent-assisted authoring are research directions, not
implemented claims. Existing per-evaluation caching is not an incremental
compiler. Valid IR does not prove semantic equivalence.

## Work order and acceptance

**Current priority, September 17:** the TVM natural-route control (Table 2),
the same-host runtime comparison, the interpreter profile, and the second
derivation case (guard folding in `c.prepare`) are integrated. Remaining work
is adversarial review, figure typography, reproduction from a clean checkout,
and disclosures; do not generalize one-model results. The registration abstract is a
provisional single-number version. It contains only the verified coverage fact
of fourteen pinned ONNX models; single-model storage and latency numbers remain
in Evaluation. Do not add a performance number until a matched multi-model
result exists. Audit Method against implementation, then finalize the smallest
discriminating experiment and its display. Do not start another broad model
campaign or change the frozen evolution tasks. Registration remains an author
approval obligation.

### Handoff contract

The next agent must read this plan, `submission/README.md`,
`submission/CHECKLIST.md`, `experiments/derive-prepare.md`, and
`evolution-study.md` before editing. These files are the current source of
truth; older notes and generated previews are not alternative plans.

The title is fixed as **Joggle: Malleable Compilation with a Progressive
Intermediate Representation**. The abstract uses one quantitative fact only.
The paper is a compiler-infrastructure study of malleable compilation, not a
claim that the current scalar C backend beats production runtimes. The three
implemented evidence threads are editable compiler functions, typed module and
derivation boundaries, and consumer-directed exposure with validation and
rollback. Dependency-sensitive reactive recomputation is proposed, not an
implemented result.

The next agent may improve method prose, complete one predeclared external
mechanism comparison, and repair inconsistent terminology. It must not invent
aggregate speedups, convert a single-model result into an average, add a new
headline mechanism, rewrite the frozen S0/S1/S2 tasks, or launch an unbounded
model campaign. Every new number needs a raw record, denominator, unit, host,
revision, and evidence locator.

### P0: freeze the argument and establish a decisive mechanism result

- [x] Preserve the title and consolidate competing plans here.
- [x] Distinguish the agreed design philosophy, concrete mechanisms, supporting
      checks, and future work.
- [x] Close the existing invocation path for whole-module analyses and emitters
      without adding a role-specific execution API.
- [x] Exercise clone → local edit → invoke on an inactive source-defined
      function. Verify unchanged original behavior, dependencies, typed results,
      and rollback on failure; cover more than transformation functions.
- [ ] Execute the existing cross-system composition/evolution study. Keep its
      original tasks and reveal neither a measured advantage nor an unsupported
      limitation without a runnable record.
- [ ] Use one real analysis, selection, packing, or generation procedure to test
      derivation. Prefer an existing bottleneck; do not manufacture a toy speedup.
      Record derivation cost, repeated execution cost, IR growth, and output
      correctness. Byte equality applies only when the intended edit preserves
      representation; a storage-policy change requires numerical and memory-safety
      checks instead. The [source-defined prototype](experiments/derive-prepare.md#memory-planner-derivation)
      changes slot selection inside `mem.plan(Mod, Fn)` while retaining analysis
      and emission interfaces. The prerequisite view/backedge fixes and executable
      regression checks are recorded separately from the derivation. A subsequent
      UltraFace check preserves reference accuracy and reduces emitted static-slot
      declarations by 4.49%, with bit-identical outputs; this is one model/input,
      not peak memory or an external win. The pinned TVM interface audit is source
      evidence only. The local source-patch control now produces the same model
      plan and passes the same oracle. Both routes inherit repairs on regeneration,
      retain stale private bodies without regeneration. The structural recipe
      now follows def-use and branch-carried operands, tolerates one-variable
      and nine-variable renames, and preserves the checked model plan. The
      unchanged source patch needs adjustment for those renames. Five decision
      mutations are explicitly rejected; this is bounded robustness evidence,
      not measured maintenance effort or arbitrary rewrite correctness. Keep
      the control, corrected rename fixtures, and negative cases. Remaining model coverage,
      derivation cost, dependency-aware refresh, and a runnable external
      customization control are still required.
      If no legal reusable optimization is available, report the
      boundary and do not promise a new optimizer before submission.
- [ ] Establish the closest mechanism control using MLIR Transform or xDSL.
      Compare TVM/Relax and ONNX-MLIR on the existing artifact study, not on an
      artificially Joggle-shaped API. AnyDSL is a staging/optimization
      counterexample; do not call an unimplemented port a measured baseline.
      For the storage-planner derivation, use the pinned TVM/Relax planner's
      natural source-level route as the selected external control: apply the
      same predeclared ranking change inside `TokenAllocatorMixed`, retain TVM's
      existing eligibility and rewrite pipeline, and measure load/build,
      compiler rebuild, repeated compile feedback, correctness, and planned
      storage under one matched fixture. TVM already uses capacity-indexed
      selection, so this comparison tests the customization boundary rather
      than algorithm novelty. Executed September 16: one translation unit and
      relink in 8.0 s, planned storage +1.06% (14 to 9 storages), same oracle
      passed; the Joggle route needs no rebuild and reproduces the recorded
      artifacts ([record](experiments/derive-prepare.md#external-natural-route-control)).
      Report it as a route comparison on one model. ONNX-MLIR at `4a13c34` is
      source-audited only: storage is upstream bufferization with one
      per-operation reuse flag, so the route shape matches TVM's; a runnable
      ONNX-MLIR, Transform, or xDSL control remains undone and is not required
      for submission.
- [ ] Write Motivation and Design around one actual trace and the nearest
      counterexamples. Finish the prose argument before enlarging experiments.

**September 18 gate:** choose the principal measured consequence: independent
composition/revision or useful derivation of a real compiler procedure. At
least one needs a substantive external comparison, beyond shorter syntax.
Keep the other as a bounded capability or future work if its evidence is absent.
This gate freezes scope; it does not authorize changing a protocol to improve
results.

### P1: artifact usefulness and cost

- Reuse existing complete models and matched Linux scripts. Record numerical
  error, inference latency, compilation time, compiler peak RSS, runtime
  workspace, executable size, and separate weight size.
- Preserve thread counts, inputs, flags, revisions, timing boundaries, run
  ordering, dispersion, and available host/load information. GitHub Linux runs
  remain useful; identify shared-host interference instead of silently treating
  them as isolated-machine measurements. Never form ratios across different jobs.
- Profile only a blocking compiler bottleneck. Bound an XCiT preparation attempt
  to one working day; retain timeout and growth data if unresolved.
- Keep existing negative ORT/LiteRT gaps visible. Generic body transformations
  and optional external primitives are legitimate implementation routes, but
  results must identify which route was used.
- Validate a clean anonymous installation and reproducible commands. Refactor
  core code only where correctness, scaling, or these measurements require it.

### Deferred

No new frontend/backend family, broad LLM coverage, Windows campaign, full JIT,
self-hosting rewrite, or blanket parser decomposition on this submission path.
A research prototype may be useful before it matches production runtime speed;
that usefulness still needs concrete evidence.

## Formalization to write, not new public classes

Describe a program as a set of typed function definitions with explicit resolved
dependencies. An exposure step edits a selected body; unaffected definitions
retain their identity. A derivation produces a fresh definition with a specified
substitution/edit, leaving the source definition unchanged.

State separately: type preservation; handle validity; dependency resolution;
read-only versus mutating execution; failure rollback; semantic preservation
conditions for each particular rewrite. Identify checks implemented by the host,
module preconditions, and properties tested only by an oracle. Do not claim a
general equivalence proof, termination guarantee, or whole-program dependency
inference from these checks.

For optimization of compiler behavior, include its observed edits and failures,
not only its returned scalar, in the equivalence condition. Inactive-copy
derivation avoids changing an executing body but does not itself solve caching,
side effects, recursion, or native boundaries.

## Paper and figures

Approximate technical-page allocation: introduction 1; motivation 1.5; design 3;
implementation 1; evaluation 4; related work and limitations 1.5. References are
additional. The allocation is a writing budget, not a requirement to fill space.

1. The Introduction uses six general panels in three paired columns
   (`fig:overview`). Red crosses mark specific obstacles and green checks mark
   implemented facilities. Aligned objects expose body-editing, composition,
   and exposure contrasts; both rows carry comparable graphical detail.
   Only the narrow central strip uses a MobileNetV2 example. Clean single-stroke
   connectors and noun-phrase labels replace sweeping lines and paragraph labels.
   Dependency-aware recomputation stays dashed and planned without a green check.
   These are design contrasts, not a measured competitor comparison.
   [Asset and generation record](figures/overview.md).
2. Mechanism exposition starts with the actual shared Fn representation, then
   derives a compiler function over a separate subject Mod, resolves its module
   dependencies, and explains consumer-directed exposure. Embed short real IR
   fragments and mark copied, replaced, retained, and opaque objects explicitly.
   Preserve the real MobileNetV2 mixed-state trace. Future dependency-driven
   recomputation belongs in a visibly labeled design proposal, not a completed
   mechanism diagram.
   The derivation figure now uses a three-panel compiler edit and a separate
   subject-execution lane, with literal API excerpts typeset in LaTeX.
   [Source mapping and generation record](figures/derivation.md) distinguish
   abbreviated control flow from actual source. The single-column
   [module close-up](figures/binding.md) shows the overload-binding regression
   rather than repeating installation icons: exports remain while resolution
   changes, so the staged candidate is rejected.
   Figure 2 now uses an asymmetric architecture view rather than a three-card
   trace: selected imports, compiler definitions, separate subject, and core
   services. A small MLIR Transform inset compares implementation boundaries
   without claiming exclusive mixed-level or DAG support. Layout follows the
   relationship being explained, not a requirement that every figure have
   three panels. [Source mapping and prompts](figures/architecture.md).
3. A dense native-LaTeX cross-system table reports the decisive experiment:
   preserved behaviors, changes/rebuild scope, costs, and explicit frontiers.
   No pass inventory or unsupported yes/no superiority matrix.
4. Aligned complete-model panels show latency, compile cost, memory, and
   artifact footprint. Separate compiler RSS from runtime workspace; do not
   stack alternative artifact formats as though all are deployed together.
   The current Figure 5 reconstructs only admitted runtime data: nine historical
   two-system comparisons with 20 process observations each. Complete-model
   examples and transformation cohorts occupy separate panels. A matched
   multi-metric suite is still missing; do not label the historical-cohort
   picture as that suite. [Data and reproduction](figures/models.md).

The historical latency graphic is now demoted from the main paper. Its raw
trials, negative runtime gaps, and reproduction asset remain preserved and are
summarized in text. The released page is reserved for the decisive external
mechanism comparison or a measured end-to-end invocation-cost breakdown, not
another coverage figure.
5. The Axon-style wide LaTeX speedup table is supporting evidence only. Each
   cell is an actual baseline/Joggle ratio for that shape and environment.
   Unsupported, timeout, and unmeasured are distinct. Do not replace it with
   a raster heatmap or repeat one measurement across columns.

The abstract should state problem, gap, mechanisms, one verified coverage
number, and boundary concisely. Related Work explains why competing designs make sense,
what they already establish, and the specific remaining question. Source lines
are descriptive, not a productivity metric. No invented numbers, implied user
study, universal speed claim, or claimed agent advantage.

### Method presentation

The presentation order is **representation → derivation → composition → update**.
The primary mechanism and its supporting boundaries share one substrate; they
are not separate plugin subsystems or equal contribution claims.
Describe each through an actual input, the operation performed, the output and
the property checked. Keep native procedures opaque and compiler code separate
from the application artifact. Avoid an API catalogue as the method section.

- **Derivation:** distinguish invoking a function from editing its definition.
  Use resolved Fn identities and structural matches, not a generated helper name
  or the ordinal occurrence of a call, in the intended reusable interface.
  Specify which helper dependencies are captured and which remain referenced.
  The existing second-exposure deletion is an invocation/isolation pilot, not a
  useful optimization or a productivity result.
- **Composition:** show at least two separately authored modules and their
  resolved dependencies. Define what replacement checks establish and what
  still requires semantic validation. Name lookup also depends on candidate
  sets: adding an overload can invalidate an earlier choice or failed lookup.
- **Update, proposed:** distinguish object generation (stale-handle detection)
  from content revisions. First consider function-level changes, observed query
  reads and edits recorded by existing mutation operations. Revisit affected
  work through a queue. Global reads and native operations require conservative
  invalidation. Preserve failure recovery; do not claim a local dependency
  bound for arbitrary procedures or count all cache hits as valid reuse.

For a query q, a dependency record R(q) and an edit set Δ offer an explanatory
model: a recorded intersection requires invalidation; no intersection permits
reuse only if dependency observation is complete, including lookup/capability
dependencies and relevant external inputs. This is a proposed design condition,
not a property established by current Mod-revision caches. Compare a revision
run against a fresh run at the revised definition, including observable edits,
failures and numerical results where applicable.

### Evaluation design tied to the argument

This is the presentation and design backlog, not a new measurement record or
permission to bypass the existing Linux freeze. Do not run another broad model
campaign before the argument and method text are coherent.

#### Submission evaluation contract

The evaluation has one primary experiment and three supporting questions. Do
not present four parallel studies or give model latency equal rhetorical weight.

| Priority | Reviewer question | Required comparison and evidence | Current status |
| --- | --- | --- | --- |
| Primary | Can a real compiler algorithm body be revised while retaining its implementation and original definition? | Joggle structural derivation versus the same Joggle source patch and the pinned TVM natural source route; identical policy obligation and artifact oracle | Joggle routes complete; executable TVM route missing |
| Boundary | Are derived code and subject state isolated, validated, and recoverable on failure? | Private closure, original/derived calls, separate subject, rejected mutations, sequence rollback, stale-body behavior | Implemented regressions; cost not isolated |
| Consequence | Does the edit reach a useful artifact without changing downstream consumers? | Identical prepared subject, unchanged placement/emission, numerical oracle, declared slot capacity reported separately from RSS/latency | One UltraFace input; 4.49% declared-slot instance |
| Cost | What does malleability cost? | End-to-end load/clone/edit/verify/invoke/artifact validation time and peak RSS; failures retained | Source boundaries known; matched measurements missing |

Breadth evidence follows these questions: module replacement tests show a
supporting composition boundary; fourteen pinned ONNX models show exercised
program scope; runtime comparisons and XCiT timeout show that the scalar backend
and interpreter are not performance contributions. The frozen evolution study
is valuable only if S1/S2 complete in time. S0 alone is preflight and must not
compete with the primary experiment for the main narrative.

**Minimum defensible EuroSys package.** Finish the TVM natural-route control
and measure end-to-end Joggle mechanism cost. Present one mechanism table with
rows for retained structure, original availability, rebuild scope, feedback
time, correctness, artifact consequence, and observed failure boundary. Keep
the UltraFace result as the concrete consequence. If the external route cannot
be completed, state that the evaluation establishes a new executable boundary
inside Joggle but not lower customization cost; do not replace the missing
comparison with more model coverage or inference benchmarks.

| Question | Task and appropriate control | Measurements and display |
| --- | --- | --- |
| Does customization reach reusable algorithm bodies? | Change an existing analysis/selection/generation algorithm beyond a supplied parameter; compare the same output obligation using native MLIR Transform/xDSL facilities or the appropriate TVM path | Exact edited/retained structure, integration and host rebuild work, correctness, derivation and execution cost; code inset plus native-LaTeX comparison table |
| Can independently developed definitions form a usable extension? | Retain the frozen Conv/layout/ABI evolution task and portable fallback; compare Joggle, TVM and ONNX-MLIR; add composition/replacement checks only under an explicit protocol addendum | Callable artifacts, adjacent revision patches, dependent-resolution outcomes, build/load cost and fault stage; wide revision table, no binary ecosystem ranking |
| Does local change produce local recompilation work? | After implementation, replay unrelated edits, callee changes, new overloads, capability changes and conservative/global cases; compare fresh execution, current coarse caching and dependency-aware updating, plus a matched external mechanism | Visited/affected IR, query recomputation, compile-only and full feedback time, peak RSS and fresh-run agreement; wide small multiples by change type and affected fraction |
| Are the resulting programs useful? | Reuse pinned complete-model numerical and runtime records with matched ORT/LiteRT or compiler baselines as appropriate | Separate latency, compilation time, compiler RSS, runtime workspace, code and weight size; aligned panels. Keep unsupported/timeout cases and performance deficits visible |

Freeze revision cases before timing, including cases where most dependencies
change and tracking overhead may lose. Report cold initialization separately
from repeated edits. Compilation, validation, runtime execution, and candidate
generation are distinct feedback components. Source lines are descriptive;
claims about human effort need controlled authoring evidence. Agent success
needs its own fixed task/model/tool/budget protocol and is currently deferred.

#### Smallest discriminating mechanism study

The existing storage case establishes an algorithmic consequence, not a unique
advantage of derivation: the source-patch control obtains the same plan. The
next comparison must hold the intended algorithm and correctness obligations
fixed while varying how the researcher installs that change.

1. **Choose a strong control before measurement.** First check whether an
   existing parameter or callback expresses the change. If it does, retain
   that route rather than forcing a body rewrite. Otherwise compare the
   structural recipe with a maintained source edit and the external system's
   native extension route. A failed literal text patch is a diagnostic control,
   not sufficient evidence against AST edits, xDSL, or MLIR Transform.
   The [Transform extension tutorial](https://mlir.llvm.org/docs/Tutorials/transform/Ch2/)
   documents extension registration and implementation; inspect the chosen
   algorithm's actual boundary instead of assigning that cost to every edit.
2. **Separate setup from revision.** Record the initial matcher, extension,
   build and registration work, then changes required for each subsequent
   revision. Use the existing rename and semantic-change tests as retrospective
   checks. Predeclare any new refactor cases before executing them, including
   a control-flow refactor that may break structural matching. Preserve failures
   and repair patches. Do not label these cases a developer-productivity study.
3. **Make dependency behavior observable.** Keep copied private helpers and
   referenced public definitions separate. Test old derivations and freshly
   regenerated derivations against the same revised dependencies. Record
   unchanged behavior, detected incompatibility, and silent stale behavior as
   distinct outcomes. Derivation does not automatically propagate source fixes.
4. **Measure complete cost.** Separate load/parse, closure discovery and copy,
   matching/editing, compiler-module validation, subject invocation and artifact
   validation. Within invocation report store copying, input verification,
   algorithm execution and output verification when instrumented. Record total
   wall time and peak RSS independently, including failed attempts. Existing
   per-function timers exclude the initial store copy and input verification;
   they cannot substitute for the total. Compare tracing consistently enabled
   or disabled. Source inspection establishes these boundaries, not their cost.
5. **Use an artifact oracle independent of the edit route.** Original and
   modified policies run on identical prepared inputs; each route implementing
   the modified policy must satisfy the same storage-safety and numerical
   checks. Report static slot capacity separately from RSS and runtime latency.
   Keep the frozen S0/S1/S2 study separate from this protocol refinement.

Present the revision outcomes in one native LaTeX table, grouped by revision
and customization route, with initial setup separate from subsequent repairs.
If timing supports a cost breakdown, use aligned panels for total latency and
its measured components. Do not fabricate a full breakdown by subtracting
incompatible timers. Retain the complete-model plot as artifact context, not
as proof that derivation lowers customization cost. Final figure selection
follows the result rather than a preset number of panels.

### Current writing checkpoint

Abstract, Introduction and Motivation now follow the approved scenario and
three obstacles. The Introduction establishes joint-design background and
iterative development before introducing compiler customization. Figure 1
pairs each obstacle with its design response, without presenting these as
three sequential stages. Method now explains how derivation, resolution, and
selective exposure cooperate: revise the algorithm, retain its dependencies,
and control detail in the separate subject it transforms. The real MobileNetV2
trace and C-preparation derivation illustrate different parts of this path;
neither is a complete implementation of the motivating packing scenario.

Evaluation now leads with internal algorithm derivation and its controls,
then composition, complete artifacts, and compilation work. The old model-count
table and source-line-count presentation are removed. Related Work
compares implementation access, module boundaries, and mixed representations
before discussing scheduling and deployment. Discussion connects the common
representation to its semantic obligations and execution costs. The title,
frozen protocols, and raw measurements are unchanged; the architecture and
runtime displays have been rebuilt from their respective source records.
The [core claim check](studies/literature-map.md) now records five
primary-source comparisons and the corresponding Related Work corrections;
it does not certify the rest of the references or establish exhaustive novelty.

The argument check leaves three decision-critical evidence obligations:

- A useful algorithm-body change with a stated precondition and an external
  control. The inactive-call pilot establishes access and isolation, not reuse
  savings or an optimization benefit.
- Composition under successive changes to one extension. Four independent
  capability tasks do not establish lower revision effort, and source counts
  cannot substitute for that measurement.
- Dependency-sensitive recomputation, if claimed as a result. Current local
  exposure and revision caches are insufficient. Keep it visibly proposed
  unless fresh-run agreement and affected-work measurements exist.

Next, finish the implementation-to-method audit and the control selection above,
then the smallest useful external revision comparison. Audit final-size figure
typography against that account. Do not repeat completed architecture drawing
as a substitute for the missing comparison. Finalize the abstract only after
these claim boundaries and measured outcomes stabilize.
Do not expand model coverage to compensate for these missing mechanism results.
This is a scoped author-side argument check, not an external review or a
submission-readiness certification. No additional experiments were performed.

## Calendar and release gate

Official [EuroSys 2027 dates](https://2027.eurosys.org/cfp.html):
title and abstract September 17 AoE (approximately September 18, 19:59
Beijing); full paper September 24 AoE (approximately September 25, 19:59
Beijing). Twelve technical pages plus references. Confirm the live HotCRP
timestamp before relying on the conversion.

| Date, September 2026 | Deliverable |
| --- | --- |
| 15 | Consolidated plan, claim boundaries, first invocation/derivation checks |
| 16 | Motivation/Design draft and bounded decisive experiment |
| 17 | Evidence-bounded abstract and registration metadata for author approval |
| 18 | Mechanism/evidence gate; freeze experimental scope |
| 19–20 | Necessary matched comparisons and data-derived tables/panels |
| 21 | Complete readable PDF with no promised main result |
| 22 | Adversarial argument, source, and evidence review |
| 23 | Revision and reproduction |
| 24 | Candidate final PDF, anonymity, references, and disclosure checks |
| 25 | Submission buffer, not planned feature work |

The manuscript's Motivation, Design, and compiler-function mechanism have been
partly synchronized with this freeze. Its main evaluation remains limited by
the existing unmatched external records and must not be treated as a completed
cross-system result.
[The submission checklist](submission/CHECKLIST.md) tracks delivery, not another
research plan. Registration/submission remains an author action; this plan does
not mean either has occurred. If decisive evidence is missing, retain the useful
tool and continue the study rather than present an unsupported EuroSys claim.

### Cycle decision, September 17: continue the study

On September 17, 2026 the author decided not to register or submit the current
manuscript to the EuroSys 2027 fall deadline, and not to reformat it for FSE
2027. Neither cycle's dates are active constraints, and the dated table above is
retained only as a record of the schedule that was under consideration. The
stop rule in the paragraph above is the reason: composition under successive
revision and matched cost beyond one model and host are still open, so the tool
is retained and the study continues instead of an unsupported systems claim
being submitted. This is a decision about this cycle, not a freeze of the venue.

The active queue becomes the polish and evidence work already listed as open,
in this order:

1. Close the argument gap between the motivating packing scenario and the
   evaluated cases (storage planning and C preparation), or state the
   relationship so a reviewer does not have to reconcile it.
2. Make the question-to-study-to-artifact mapping explicit, so Q1 to Q4, the
   tables, and the raw records can be checked without reconstructing the design.
3. Complete the claim-to-source and bibliography-field audit.
4. Finalize figure typography and the remaining delivery items in the checklist.

The title, the frozen protocols, every raw measurement, and every negative
result remain unchanged by this decision. No new headline claim, aggregate
performance number, or implemented-recomputation statement is introduced.

## Execution record

September 15: consolidated the previous argument/evidence/review notes into
this plan and the literature map; retained raw experiments and source records.
Added whole-module invocation to the existing API, with analysis and generation
regressions, result/arity rejection, query isolation, and failure rollback.
Validation: `cmake --build build -j4` and
`ctest --test-dir build --output-on-failure -j2` passed all 41 tests in the
current local configuration after the direct-handle isolation change; heavy
model options are disabled. These are implementation checks, not new model
timings or external comparison results.

The copied-generator regression exposed stale executable-structure caches:
edited IR printed correctly but invocation still used cached operands/body
structure. Internal execution-cache keys now include the source store revision.
Old snapshots remain valid for active references and are released at evaluation
end. This is a correctness fix, not fine-grained invalidation or a performance
result; retention cost across many code revisions remains to be measured.

Separating a derived compiler definition from the model exposed another real
boundary: copying a compiler-only `Fn` into the model made `c.prepare` examine
it as model code and reject its calls. A direct `Fn`-handle execution overload
now lets a verified compiler-definition `Mod` supply the function while a
different `Mod` receives the edit or query. Regression checks cover a changed
emitter result, an original emitter that remains unchanged, read-only rejection
of a mutator, a successful edited target, and C preparation without copying
compiler code into the artifact. This is isolation/correctness evidence, not a
measured advantage over another compiler or a useful optimized procedure.

The S0 cross-system study has functional macOS preflight records for Joggle,
TVM, and ONNX-MLIR. Its protocol still requires a matched Linux artifact and
measurement audit before the stage can be frozen. This host's GitHub CLI is
not authenticated, so a Linux workflow has not been dispatched here. Preserve
the preflight and do not substitute macOS timing ratios for the required Linux
record.

The manuscript now distinguishes installed source functions, a separate
compiler-definition `Mod`, and the target model, with a native TikZ trace of
the tested boundary. `latexmk` produced a 13-page PDF, with references starting
on page 11 and no LaTeX errors or horizontal overfull boxes; the first page,
mechanism figure, and table page were visually inspected. The extension-surface
table remains preliminary, and the matched evolution/derivation evidence is
still the submission-critical gap. This PDF is a reviewable draft, not a
submission-ready argument.

September 15 later: a derivation probe of the actual `opt.expose` procedure
found that its public wrapper calls private helpers such as `expose_with` and
`expand_with`. Qualifying those helper calls back to their source module fails
the destination module's visibility check. `Mod::clone` now plans the resolved
transitive private-call closure, copies it as private functions with the
requested function, rebinds private edges, and rolls back the whole clone on
failure. Cyclic private-helper closures are explicitly rejected. No new public
IR class or procedure role was added.

The same path copied the real `c.prepare` function and its private helper
closure into a separate compiler-definition module. A structural edit replaced
its second `opt.expose` call with `false`, then ordinary `opt.basic` was invoked
over that code module. On one simple typed-model regression, the derived and
untouched procedures produced byte-identical prepared IR and generated C
source; the original remained callable, and no copied compiler function entered
the model. An attempt to apply the existing `opt.basic` function to this
derived code produced no additional edit, so it is not a meta-optimization
result. This is
an editable real-algorithm path under one observed input, **not** a general
equivalence proof, useful speedup, or external comparison. Compilation and all
41 local tests pass after the change; heavy model options are disabled.

The next research result remains independent composition/evolution and a
bounded larger-model cost/legality test of this real compiler-procedure edit.
Do not substitute the simple regression for either decisive experiment.

September 16: because the abstract deadline precedes the full-paper deadline,
the abstract was reduced to one quantitative fact, coverage of fourteen pinned
ONNX models. The single-instance storage and latency numbers remain in the
technical evaluation. The paper must not report a mean or aggregate speedup
until a matched multi-model suite exists; the current per-cohort values are
medians over repeated process runs. Submission material and the handoff
contract were synchronized with this decision.

September 16 method audit: source inspection confirms that read-only queries
copy and verify the complete subject store, direct `Fn` queries use that same
copy-and-check path without the persistent name cache, and mutating sequences
keep a complete store backup for in-memory rollback. Sequence timers begin
after the backup and input verification. Cross-store derivation discovers the
resolved private-helper closure, snapshots the destination store, then copies
the helpers and requested function; failure restores that snapshot. Named-query
caching remains keyed by environment identity/epoch and whole-subject revision,
function, and arguments. These observations sharpen Method and its cost boundary;
they are not latency measurements or dependency-sensitive recompilation.

September 17 register pass: the manuscript stated the same boundaries up to
seven times (unimplemented dependency-sensitive recomputation appeared in the
abstract, introduction, two motivation subsections, the method, the evaluation,
the discussion, and the conclusion; "not developer effort" appeared six times).
Each boundary is now stated once, at the place a reviewer checks it: the
recomputation scope in Section 3.4 and the evaluation preamble for what no
result measures. No negative result, timeout, or trailing performance number
was removed, and no case number became an aggregate. The changes are register
only: positive findings now lead their sentences and defensive "not X" clauses
were converted to positive scope statements or deleted as duplicates. The
abstract was rewritten in the same register at 254 words with `fourteen` still
its only quantitative fact.

Four `@misc` references (IREE, microxcaling, ncnn, ONNX-MLIR docs) lacked a
year field and printed as "n.d."; they now carry `year = {2026}` matching their
recorded access dates. The remaining BibTeX warnings are empty address,
publisher, and page-number fields, which ACM-Reference-Format reports for
preprints and for proceedings entries without page ranges.

Figure 1's lower-right panel is labelled "Reactive updates" in the raster
asset. The term had been removed from the body text in this pass, leaving the
panel undefined; the caption now defines it as the broader goal and names
consumer-directed exposure as the implemented part. The raster mechanism
figures still need a final label-size correction, which requires regenerating
the assets.

September 17 prose audit. Four defects were found by reading the manuscript in
reviewer order rather than by editing sentences in place.

1. The abstract's second half was five unconnected facts. It is rewritten on a
   mechanism-evidence-cost spine at 232 words, still with `fourteen` as its only
   quantitative fact, still reporting that the scalar C trails a production
   runtime and that TVM plans faster natively.
2. The introduction expanded all three obstacles across three paragraphs using
   the same packing example that Sections 2.2 and 2.3 then reuse almost
   verbatim. The introduction now names them in one paragraph and defers to
   Section 2. Separately, the claim that derivation copies a function and its
   closure, runs on a separate subject, and leaves the original callable
   appeared four times within two pages; two restatements were removed.
3. Two factual defects. Section 2.2 listed four systems (PackedFunc, MLIR
   Transform, Exo 2, AnyDSL) and concluded "a different operation from all
   three"; it now says four and names the four operations. Section 2.1 still
   said "the three obstacles", contradicting the one-obstacle-two-pressures
   hierarchy adopted earlier.
4. Section 2.4 referred to "the Conv--Add--Relu evolution protocol used
   throughout", but that phrase occurred exactly once in the manuscript and
   pointed at a study demoted from the paper. The dangling sentence is removed
   and the fixed Conv--Add--Relu chain is now named in the evaluation's
   conditions paragraph, where a reviewer checks inputs.

A structural weakness remains and is disclosed rather than fixed: the
motivation runs on a block-scaled packing scenario while the derivation studies
evaluate storage planning and C preparation. Section 5.1 now states explicitly
that storage planning instantiates the same problem shape, but the motivating
scenario is still not the evaluated one.

September 17 locality result. Investigating the "generated code is too slow"
objection found a measurement defect rather than a design limit: the same-host
runtime comparison used `build-study/derivation/ultraface/original/model.dylib`,
the storage-study artifact, which is ordinary `c.prepare` output. The repository
already contained a source-defined loop-locality policy in `examples/locality`,
and it had never been applied to that subject. Applying it takes 1.29 s, needs
no host rebuild, moves the output-width axis innermost in 18 convolution nests,
and leaves the output checksum and the 3.58e-7 oracle error unchanged.

On the same balanced 20-process protocol the medians are 42.87 ms ordinary,
23.85 ms after the policy, 37.44 ms for TVM's default C target, and 2.87 ms for
one-thread ONNX Runtime. That is 1.80x over the ordinary artifact, 1.57x past
TVM's unscheduled lowering, and a reduction of the runtime gap from 14.96x to
8.32x. Section 5.4 now leads with Table 4 and states where the remaining gap
lives; the discussion reports loop reordering as measured rather than promised.

This is an applied policy, not a derivation case, and the manuscript says so.
The remaining 8.32x is packing, vectorized microkernels, epilogue fusion, and
threading, none of which is attempted here.

Model-coverage audit, same date. The four non-passing models are three distinct
situations and the manuscript should not present them as one failure class.
SSD-MobileNetV1's 386 residual source calls come from revision 2f55701 while
the other rows were rerun at 5e15c29; its frontier is dominated by NonZero,
Slice, NonMaxSuppression, If, and Loop, so data-dependent output shapes are the
real blocker. TinyYOLOv3 keeps 219 unknown results through a dynamic
Shape/Slice/Tile/Resize chain and Loop subgraphs. Both are architectural gaps in
a static-shape C emitter, not bugs, and are not addressable before the deadline.
XCiT is different: at 5e15c29 its conversion is clean with zero source calls,
and the only blocker is the recorded C-preparation timeout, which is the same
compile-time cost the interpreter profile explains. EfficientNet-Lite4 INT8 also
converts cleanly with zero source calls but has no row in the coverage ledger at
all, so its artifact gate was never attempted; running it is the cheapest
remaining coverage work.

September 17 multi-model locality campaign. The single-model locality result
was correctly challenged as too narrow. Extending it exposed that the stored
fixtures could not answer the question at all, so the study was rebuilt rather
than patched.

Fixture audit. `build-matrix/squeezenet1.1-7/prepared.jog` and
`resnet18-v1-7/prepared.jog` come from an older pipeline whose convolutions are
split into separate output and reduction nests; `locality.apply` does not fire
on them. `squeezenet1.0-13-qdq/prepared.jog` depends on a `spatial` module that
is now an empty directory. `tinyyolov2-8/prepared.jog` is zero bytes.
`build-study/mobilenet-block/canonical.jog` already carries the reordered axis
order, so it would have understated the policy. Measuring across these would
have compared fixture ages rather than policies.

Unified pipeline. Every model is now rebuilt from its pinned ONNX file through
one chain at one revision: `onnx.read`, `onnx.nn.convert opt.basic`, an optional
`opt.instantiate` for symbolic batch axes, then `c.prepare`. ResNet18 and
TinyYOLOv2 need the instantiate step; without it `c.prepare` stops at
`opt.specialize requires finite static ranges`. Scripts are in
`experiments/locality/`.

Build unification. `build/` was Release with ONNX off while `build-zoo/` had
ONNX on with no optimization setting at all, which meant the only build that
could import models was also the only build that was not Release. `build/` is
now a single Release build carrying the ONNX native module, and all compiler
timings in this study use it.

Harness. The timing subject derives each artifact's ABI from its own `c.api`
descriptor, so the ten-model study contains no model-specific signature. This
doubles as a direct exercise of the manuscript's claim that the C path derives
declarations, buffer shapes, and byte counts from one signature model.

Scope discipline. The policy is applied, not derived. The manuscript must not
present it as a third derivation case; it is evidence that a source-defined
compiler procedure delivers code quality without a host rebuild. Inputs are
seeded, so the oracle establishes agreement with ONNX Runtime on one recorded
input per model, not task accuracy.

September 17 venue change and ten-model result.

Venue. FSE 2027 (conf.researchr.org/track/fse-2027/fse-2027-papers): submission
Friday 2 October 2026 AoE, 18 pages of text and figures plus 4 for references,
heavy double-anonymous review held through all phases, artifact evaluation
offered separately, and a mandatory Data Availability statement. Three
consequences for this manuscript. The page budget grows by six pages over the
EuroSys layout, so material previously cut for space can return. The framing
should lead with the software-engineering claim, reuse and maintainability of
compiler implementations, rather than with systems performance. A Data
Availability statement must be added, and anonymity now has to survive the
whole review, including self-citations phrased in the first person.

Ten-model locality measurement. Apple M4, one thread, 20 balanced fresh
processes per variant per model, each process validated against that model's own
ONNX Runtime reference, load 1.57 before and 2.62 after. Medians in ms, ordinary
then after locality.apply: mnist 1.268/0.392, squeezenet1.1 178.813/94.333,
squeezenet1.0-qdq 180.204/92.062, ultraface 40.340/27.401, mobilenetv2
211.297/74.852, shufflenet-v2 79.887/32.156, resnet18 1219.477/714.346,
tinyyolov2 2296.638/1436.596, googlenet 1273.467/562.894, densenet
1992.885/799.084. Speedups span 1.47x to 3.23x with a median of 2.11x. Every
model keeps a bit-identical output checksum across the two variants and every
emitted weights file is byte-identical, so the policy changes run time and not
results. Models are reported separately and never pooled. Data is in
data/locality-matrix.{csv,json} and experiments/locality/.

Test-infrastructure cleanup, first step. 27 of 33 test/*.cmake files repeated
the same -D guard, and the suite carried 474 hand-written FATAL_ERROR blocks
around 264 result checks, so failures reported inconsistent context.
test/joggle_test.cmake now provides joggle_require, joggle_workspace,
joggle_run, joggle_expect, joggle_expect_same, and joggle_expect_different.
test/mem.cmake is converted as the pilot and drops from 138 to 68 lines; the
full suite still passes 59 of 59.

That conversion also exposed a CMake trap worth recording. A multi-value keyword
holds its values as a CMake list, and a list cannot carry a regex containing a
bracket expression such as `[^]]+`: CMake silently merges it with the following
element, so the assertion tests a pattern nobody wrote and still reports a
plausible-looking failure. joggle_expect therefore takes one pattern per call
through single-value keywords, which also gives each assertion its own label.

September 17 build and test hygiene, plus the TVM column.

Test infrastructure. All 33 test/*.cmake scripts now use test/joggle_test.cmake.
The 27 hand-written -D guard blocks are gone, 474 FATAL_ERROR blocks fall to
262, and the script bodies drop from 4,892 to 3,702 lines against a 175-line
helper, a 20 percent net reduction with the suite still passing 59 of 59. The
three worst files shrink most: locality 713 to 378, c 781 to 585, tile 807 to
537. Conversion was mechanical and every recognised idiom was verified by
running the suite rather than by reading the regex.

CMakeLists. A joggle_script_test(name script DEFINES ... LABELS ... TIMEOUT ...)
function replaces 27 open-coded registrations that each repeated
-DTOOL=$<TARGET_FILE:joggle-tool> and -DMODULES=...; the file drops from 1,149
to 1,052 lines and `ctest -N` lists exactly the same tests before and after.

TVM column. export_tvm.py now derives the input name and shape from the model
instead of hard-coding UltraFace, and converts the opset only when TVM's Relax
frontend rejects the pinned one. Across the ten models only UltraFace needed
that conversion, and its converted digest is recorded. subject_tvm.py measures
the exported library on the study's own input under the same one-thread
protocol, and matrix_driver.py now measures base, locality, tvm, and ort
columns, alternating all of them across trials. Only the two Joggle artifacts
are required to agree bit for bit; TVM and ONNX Runtime compute the same
function in a different order and are checked against the reference within
tolerance instead.

September 17 build and module organisation.

Build directories. Eight configured trees existed, 5.3 GB in total, with
overlapping options and two of them, build-check and build-zoo, carrying an
empty CMAKE_BUILD_TYPE, which compiles without optimization. Because ONNX was
only enabled in build-zoo, the one tree that could import a model was also the
one tree that was unoptimized. The root cause was in the documentation: the
README and tutorial invocations that enable an optional module omitted
CMAKE_BUILD_TYPE, so following them produced exactly that tree. All seven such
invocations now pass Release, the README states the single-directory policy and
warns that an empty build type silently misreports timing, and the FlatBuffers
prefix that previously existed only inside two stale CMake caches is documented
as CMAKE_PREFIX_PATH. `build` is now one Release tree with ONNX, TFLite, and SAT
enabled, and it runs 62 tests rather than 59. model-study.md points at it
instead of build-zoo. The redundant trees are still on disk and are not deleted
here: build-app is named by 17 provenance records.

Module layout. Fragments under lib/ are loaded automatically and sorted, and
resolution is module-wide across them, so splitting a module is textual
regrouping rather than a scope change; lib/ fragments already called local
functions defined in module.jog before this change. Six modules are now split
by role or operator family: c 2527 lines into module.jog plus ten fragments,
onnx.nn into eleven, tile into twelve, tensor into seven, opt into eight, and nn
into seven. Every step was verified by rebuilding and running the suite, which
stays at 62 of 62.

The splitter needed one correction worth recording. Top-level declarations
cannot be found by brace counting, because the C emitter's string literals carry
unbalanced braces, and a naive identifier pattern silently misses operator
declarations such as `fn []=` and `fn +`, folding their bodies into whichever
neighbouring block precedes them. Declarations are recognised by column instead,
with an explicit operator alternation.

Splitting tensor also changed observable behaviour: `module info` now lists each
lib/ fragment as a source. test/module.cmake asserted the single-source form, so
the assertion was updated to accept module.jog followed by sorted fragments
rather than weakened.

The largest remaining single file is modules/onnx.nn/lib/convert.jog at 2,532
lines, a pre-existing fragment that this pass did not touch.

September 17 repository boundary pass. Work on the paper was paused to fix the
repository it depends on, because the source tree, the generated trees, and the
record store had stopped being distinguishable.

Build trees. Fifteen trees totalling 20.5 GB were on disk while the README
stated a single-directory policy. Eight were reclaimed: build-core,
build-optional, build-check, build-zoo, build-tflite, build-tflite-linux,
build-fixtures, and build-evidence, together with build/evidence. Two of them
were provably redundant rather than merely unreferenced: build-tflite-linux
held the Linux TFLite records already promoted to `paper/data/tflite-linux/`,
and the four `measure-systems/` record sets were outputs of
`test/measure_systems.py`, which writes manifest, record, rows, and three
negative-test manifests as its own fixture. build-deps was retained: the
canonical and TFLite CMake caches both take their FlatBuffers prefix from
build-deps/flatbuffers-install, so it is an active dependency and not a stale
tree. This supersedes the earlier statement that the redundant trees are still
on disk. Six trees remain: build, build-app, build-deps, build-matrix,
build-study, and build-xcit.

Promotion before reclamation. The earlier pass would have deleted by reference
count, which is unsafe. Two checks changed the outcome. build-tflite-linux was
unreferenced but held the only promoted copy of a Linux record. More seriously,
`build/audit-current` is referenced by no tracked file yet holds audit records
(`ranked-pair.csv`, `policy-script.csv`, `mobilenet-plan.json`, and others)
that appear nowhere under `paper/`, and `build/evidence/5dac130/` holds an
artifact run whose revision appears in no tracked record while
`block-artifact-pilot.csv` stops at `7cc089a` and `e096fcc`. Those trees were
therefore kept, not reclaimed. What was promoted before reclamation is recorded
under `paper/data/mobilenetv2-policy/artifact-5dac130/`, the accept and reject
policy oracles and two probe IRs under `paper/baselines/onnx-mlir/`, and the
XCiT oracle tensors under `paper/data/xcit/`. That last location is deliberate:
`paper/fixtures/` is owned and hash-checked by `paper/fixtures/generate.py`,
which does not know this model, and `joggle_onnx_model` consumes only the
`.onnx` file, so the tensors belong to the record store rather than the
generated fixture store. The `.log`
and `.stderr` files in that evidence store were all zero bytes, so no failure or
timeout record was lost. This remains an audit of records by digest, which
proves duplication but cannot prove that a renamed or aggregated record is
absent; the remaining large trees are unaudited for that reason.

Module split defect. Loading `onnx.nn` failed with
`convert_slice.jog:1:2: error: duplicate attribute 'phase'`, which took the
ONNX codec, zoo, and matched-implementation tests down with it. The cause was
the split itself: `convert_shape.jog` ended with a dangling attribute list for
`onnx.ReduceMean/Sum/Min/Max` whose rule body, `convert_mean`, had been placed
in `convert_unary.jog` without its attribute. A scan for fragments whose last
line is a closing attribute bracket found that file and no other. The list was
moved back in front of `convert_mean`. The 2,532-line figure quoted above for
`convert.jog` is also stale: after the split the largest single `.jog` file is
681 lines (`modules/tile/lib/scalarize.jog`), and `onnx.nn` is 21 files.

Boundary rule. README now states the three-way split explicitly, names the
remaining trees and their roles, and records two rules that this pass learned
the hard way: a script whose default output root is inside `build/` is a bug in
that script, and nothing under a `build*` tree is deleted because it looks
unreferenced. `paper/scripts/operator_suite.py` still defaults its output into
`build/operator-study/`, and `paper/studies/operator-study.md` documents those paths;
that relocation needs the documented commands updated with it and is the next
step of this pass.

September 17 layout check. The README rules are now checked by
`test/layout.py`, registered as CTest test `layout` with label `lint`; the suite
is 63 tests. Each rule is classed `enforce` or `report`. `enforce` means a
violation fails the suite; `report` means the rule is stated but the tree does
not satisfy it yet, so the findings are printed as a migration queue. The
intended end state is every rule enforced under `--strict`.

Two rules pass. No tracked file sits under a generated tree, and every
repository path quoted in the documentation now resolves. The second rule found
three real defects and one false positive, which is why it was worth writing.
`docs/tutorial.md` and `examples/ikj/README.md` both told a reader to build a
tree named `build-dev`, a name that appears nowhere in the README build policy,
which sanctions `build` plus an optional `build-debug`; both now use `build`,
and the documented ikj pipeline was rerun end to end (strict C99, exit zero) to
confirm the corrected commands work. `docs/tutorial.md` also pointed at
`build/examples/mobilenet`, which is a conditional target's output rather than a
stale path, so the check was changed to judge a generated tree by its declared
root instead of by its contents.

Five findings remain, and they are the migration queue.

1. `paper/scripts/operator_suite.py:369` defaults its output to
   `build/operator-study/fixtures`. This is the root cause of the largest
   in-tree mess: a study script writing into the development build tree. Fixing
   it means moving the default to `build-study/` and updating the commands
   documented in `paper/studies/operator-study.md`.
2. `build-xcit` is on disk with no declared role. Its records are the renamed
   working copies of the XCiT frontier and spatial studies, which is why it was
   not deleted in the reclaim pass; it needs promotion and then retirement.
3. 627 untracked `.json` and `.csv` files sit under generated trees. Some are
   test scratch that regenerates, and some are unpromoted records. Separating
   the two is the record-level audit that the digest pass could not finish.
4. `paper/experiments/{exposure,guard,pilot,reuse}/` have no README, so four
   studies have no statement of their own inputs, commands, and boundaries.
5. Eighteen measurement scripts and seven study documents are flat in `paper/`.
   This is the structural finding behind the file-organization complaint: a
   study's document, code, records, and figure sources have no common home, so
   locality, for example, is spread across seven paths.

Items 1 to 3 are prerequisites for reclaiming the remaining large trees. Items 4
and 5 are the reorganization itself, and the record-store decision that governs
item 5 was deliberately deferred until this list existed.

September 17 product/research boundary. The layout pass exposed a structural
defect larger than any directory name: the shipped project depended on the
research workspace. `CMakeLists.txt` read `paper/` in nine places, defining two
executables whose sources are `paper/experiments/*.cpp`, registering five checks
that run `paper/`-hosted scripts, and feeding one product test from
`paper/fixtures/implementation`. A clone therefore could not build and test
without the paper. That is the line between a project and a working directory.

The fix is a switch rather than a move. `JOGGLE_RESEARCH`, default off, gates
all nine references; the default configuration now registers 57 tests none of
which name `paper/`, and `-DJOGGLE_RESEARCH=ON` restores all 63. Nothing under
`src/`, `test/`, or `tool/` reads the research workspace in either mode, and
`README.md` states the boundary where a newcomer looks for it: in the layout
section and in the build section. The switch is a first step, not the end state:
the two gated targets and the five gated checks belong in a `paper/` subproject
of their own, which needs `joggle_script_test` to stop assuming its script lives
in `test/` relative to the current source directory.

The layout checker was reimplemented as `test/layout.cmake` in the repository's
existing script-test idiom rather than as a new Python file. The first version
added a nineteenth flat script to a directory whose flatness this same pass
reported as a defect, and the repository already registers 29 `.cmake` script
tests through `joggle_script_test`; the CMake version also drops the Python
requirement from the check. It reports no line numbers, because splitting a file
into a CMake list drops empty elements and a line number short by the number of
blank lines above the match is worse than no line number.

Two working-discipline corrections from this pass. The digest index used to
compare stranded records against the tracked store was written under
`build-study/`, not `/tmp`, and the two files that did land in `/tmp` were
removed; `build-study/` is the sanctioned location for working results. The
twelve scratch and index files left in `build-study/` by the audit were also
removed once their findings were recorded here.

September 17 test labels and the extensions rename.

Labels. Every test now carries exactly one kind label, assigned in one place at
the end of the test section through a new `joggle_label` helper that skips a
test whose option is off instead of erroring. Before this, 48 of 63 tests had no
label at all, so `ctest -L` could select nothing. The everyday loop drops from
240 seconds to 20 with `ctest -LE model`, and `ctest -L unit` runs in 2.35
seconds. The existing `onnx-zoo` labels are preserved because `docs/tutorial.md`
documents `ctest -L onnx-zoo`.

`examples/` split. The directory was load-bearing under a name that described
something else. Five entries were complete module packages that the extension
tests load through a second module root, and `make_harness.py` is a shared test
tool that the core `c-execution` test needs. They moved to `extensions/` and
`test/tools/`; `examples/` now holds only the opt-in ONNX application example,
which the default configuration neither builds nor tests. The CMake variable
`EXAMPLES` became `EXTENSIONS`, and the six test names, their guard messages, and
the label followed, because a directory named `extensions/` holding tests named
`*-example` is the same defect one level down.

Record consequence, and a caught defect. `paper/scripts/measure_extensions.py` hashes
each source's repository-relative path together with its bytes, so the rename
invalidated the recorded `source_sha256` in
`paper/data/extension-footprint-pilot.csv`. The path fields in
`paper/tasks/extension-tasks.json`, `paper/baselines/tvm/record.json`, and
`paper/baselines/onnx-mlir/external-kernel/result.json` were updated, no
measurement value was touched, and the collector was rerun. Comparing the
regenerated record against a copy taken first showed columns 1 to 10 identical,
with exactly the three tasks whose sources moved carrying a new digest and
numeric-format unchanged.

The first regeneration was wrong, and the comparison is the only reason that was
noticed. The manifest's `tests` field still named `ikj-example`, `cost-example`,
and `edge-example`, so three tasks returned `validation=fail` where they had been
`pass`; every count and byte total was unaffected, which is precisely why a
digest-only check would have missed it. The manifest test names were corrected
and the collector rerun until the first ten columns matched the original exactly.
Any future rename of a test or a measured path must update the manifest's
`tests` field in the same change.

September 17 test consolidation and documentation structure.

Test redundancy. Eight script tests (`resize_c`, `qlinear_c`, `topk_c`, `nms_c`,
`nonzero_c`, `range_c`, `dynamic_slice_c`, `dynamic_c`) ran the same pipeline:
`c.prepare`, `mem.plan`, require an empty C frontier, `emit c.source`, compile
strictly, run, require zero. They differed only in the model, the harness, and
one to three assertions. The pipeline now lives once in `test/c_pipeline.cmake`
and each test sets `NAME` plus its own checks and includes it. 568 lines become
162, a 72 percent reduction, with no test, no test name, and no registration
changed.

The first attempt failed `qlinear-c-execution`, and the reason is worth
recording. `qlinear_c.cmake` has a conversion stage the others do not: it runs
`onnx.nn.infer onnx.nn.convert` before preparation. It was missed because the
redundancy was surveyed with a keyword grep for the assertions that seemed
likely, and a keyword grep cannot see a missing pipeline stage. The reliable
method is to extract each file's sequence of tool invocations and compare
sequences, which immediately shows `qlinear` alone carrying a conversion step.
The driver gained an optional `CONVERT`, and the `c` label passes 42 of 42.

Documentation structure. Five flat files became three directories by audience.
`docs/guide/` holds the tutorial as one file per existing chapter plus an index;
`docs/reference/` holds the language and the split-out bundled module catalogue;
`docs/internals/` holds the design, the module lifecycle, and the roadmap.
`docs/README.md` is the entry point and carries a table mapping each walkthrough
to the tests that prove it. That table records a real gap rather than intent:
the third and fourth chapters describe a `localize` module and an example fusion
policy that no test exercises and that do not exist outside the guide, so
nothing detects it if those mechanisms change.

Documentation links are now checked. `test/layout.cmake` gained
`doc-link-missing`, which resolves every markdown link target relative to the
linking file rather than to the repository root. That is the rule that breaks
when a document moves into a subdirectory, and it is exactly what the
restructure did to every `../` in the moved files. The check was written twice:
the first version extracted links with `string(REGEX MATCHALL)`, whose result is
a semicolon list, and a semicolon inside the document merged fragments and
reported targets nobody had written. It now walks the text with `string(FIND)`.
A negative test confirmed the check is not vacuous: an injected broken link is
reported, and removing it returns the check to `ok`.

A rename also breaks what a search of the obvious file types misses. The
`extensions` rename left three GitHub workflows and
`paper/scripts/prepare_operator_case.py` calling `examples/onnx/make_harness.py`, which
had moved to `test/tools/`, and one workflow trigger watching
`examples/locality/**`, which had moved to `extensions/`. An earlier residual
reference search covered `*.md`, `*.cmake`, and `CMakeLists.txt` only, so none of
them appeared. Search every file type, not only the ones the last few changes
touched.

September 17 build-tree consolidation. Six trees and 20.5 GB remained after the
first reclaim pass, and the reason is worth stating: most were not build trees.
They were study data that had no other home, sitting in directories named
`build-*`, because earlier work gave each study its own tree and let scripts
default their output into the development tree.

What each one actually held. `build/` carried 2.3 GB of which 2.25 GB was study
scratch: `operator-study` (813 MB, 284 records that appear nowhere under
`paper/data/`), `audit-current` (604 MB, unpromoted audit records),
`reorder-noalias-smoke` (436 MB, no records), `audit-san` (215 MB, harness
self-test output only), and four intermediate IR dumps (222 MB). `build-matrix`
was an earlier locality campaign whose fixtures this plan had already recorded as
invalid and superseded. `build-xcit` was an XCiT working directory whose records
the promoted `model-frontier-pilot.csv` already carries. `build-app` is not a
local tree at all: the GitHub workflows create it inside the runner, so the
`build-app` paths recorded under `paper/data/` are CI provenance, must not be
rewritten, and no local copy is needed.

Actions. The study scratch moved from `build/` to `build-study/`, which took that
tree from 2.3 GB to 50 MB and left it a genuine build tree. Records from the
reclaimed trees were preserved first, under `build-study/legacy/<tree>/`, at a
total of 796 KB, before the artifact bulk was removed. The study input the
locality and derivation commands consume moved to
`build-study/inputs/ultraface-rfb-320/`, and the three documented commands that
name it were updated. `build-matrix`, `build-xcit`, and `build-app` were removed.
The top level now has three trees, 20.5 GB became 11 GB, and the
`undeclared-build-tree` and `script-output-root` checks both read `ok` for the
first time.

The root cause was fixed rather than documented. `paper/scripts/operator_suite.py`
defaulted its output to `build/operator-study/fixtures`, which is how a study
came to write into the development tree in the first place; it now defaults to
`build-study/operator-study/fixtures`, and `paper/studies/operator-study.md` names the
same paths. The `script-output-root` check passes because there is no longer a
paper script pointing at `build/`.

Two smaller corrections from the same pass. The consolidation briefly broke
`paper/experiments/locality/README.md`, which named the reclaimed `build-matrix`
while explaining why that campaign's fixtures were rejected; it now states the
rejection without pointing at a tree that no longer exists. And the README
paragraph claiming `build-app` must not be moved was simply wrong, because it is
a CI tree; it now says so.

What remains is `build-study` at 11 GB, mostly the current locality campaign's
generated artifacts. Its records are promoted and its contents are reproducible
from the pinned models and modules, so it can be pruned whenever the regeneration
time is worth more than the disk; that is a trade rather than a defect, and it is
the only place study output is allowed to live.

September 17 comparison contract and the run-time win.

The evaluation compared three systems on three different axes and never said
which system answered which question, so a reader could not tell why MLIR
Transform, TVM, and ONNX-MLIR all appeared or where the holes were. Section 5 now
opens with a comparison contract table that states, per question, the system, the
axis, and what is actually measured. It records three gaps in the paper's own
voice rather than leaving them implied: MLIR Transform is a bounded textual
comparison with no measured data, the extension tasks are measured on three tasks
per system with only two in common, and ONNX-MLIR has no run time at all.

Whether ONNX-MLIR or MLIR is the right comparison was the open question, and the
answer is that they answer different ones. ONNX-MLIR's natural route to a
compiler change is a C++ accelerator class, which adds a component; MLIR
Transform's is replacing a `TransformOpInterface::apply` body, which is the same
object of study as a derived compiler function. MLIR Transform is therefore the
fairer mechanism comparison and ONNX-MLIR the right end-to-end and run-time one,
and neither substitutes for the other. The repository holds no MLIR build, so
MLIR Transform stays a bounded textual comparison until one exists.

ONNX Runtime was mislabelled as a peer. `gen_reference.py` builds every stored
reference with `ort.InferenceSession`, so the zero error in its row is a
tautology rather than a check, and its 2.87 ms bounds a hand-tuned production
runtime rather than a compiler. It is kept, not deleted: it is the only source of
the references every other system is checked against, and removing the row that
exposes the 8.32x gap would hide a negative result. Its role is now stated in the
table note.

The run-time paragraph was rewritten to lead with the advantage rather than the
deficit. On the same host, same prepared subject, same one-thread 20-trial
protocol, and with the output checksum unchanged, Joggle's ordinary preparation is
1.15x slower than TVM's default C-target lowering, and one source-defined policy
reverses that and passes it at 23.85 against 37.44 ms, or 1.57x faster. The two
qualifications that keep the claim honest stay in the note: the TVM row is
unscheduled and without LLVM, and ONNX Runtime is a ceiling.

Inserting the contract table shifted the numbering of the three existing tables
by one. The displays list in `submission/README.md`, the delivery notes in
`submission/CHECKLIST.md`, and the two present-tense references in this plan were
updated to match. The build is 15 pages with no overfull boxes, and pages 8 and 10
were inspected after rendering: Table 1 is complete and readable, and the ONNX
Runtime note sits under the runtime table as intended. Adding the
refactor-survival matrix later the same day shifted the three tables after it by
one again, and the same references were updated to their final numbers.

September 17 abstract rewrite and the survival matrix.

The author rejected the abstract as long and defensive, and the numbers agreed:
242 words, twelve sentences, and six of them carrying a negation or a hedge, with
the final paragraph enumerating weaknesses. It is now 211 words, nine sentences,
and three hedged ones, and it opens the evidence paragraph with the mechanism's
structural effect before the measurements. The author also asked for the
quantifiable advantage to fit in one sentence, so the abstract carries it: on one
host under a one-thread paired protocol, a source-defined locality policy applied
without a rebuild makes the artifact 1.57x faster than TVM's default C-target
lowering, with a bit-identical checksum and per-model gains of 1.47x to 3.23x in
a ten-model campaign. The fourteen-model coverage fact moves to Evaluation, where
per-model status belongs, and the `fourteen`-as-the-only-number policy recorded
on September 16 is superseded; `submission/README.md` and
`submission/CHECKLIST.md` were updated so they no longer assert it.

The refactor-survival result was prose only, which is what the author meant by
insufficient quantitative display. It is now Table 2: four unrelated source edits
against the same planner change, showing that a private-helper rename survives
both routes, a capacity-binding rename applies but is rejected at the module
boundary, and renaming nine local bindings or rewriting the selection guard stops
the patch from applying while the IR derivation still reproduces the validated
plan. The data was already recorded in the external control record under
`experiments/derive-prepare.md`; no new measurement was taken.

A lesson from the same pass: every table inserted before the existing ones
renumbers them, and three documents cite table numbers. Two renumbering rounds
cost more than the tables did. Cite labels, not numbers, in prose that will be
maintained, and treat the numbers in `submission/README.md` as generated output
that must be rechecked whenever a display is added.

September 17 comparison-project landscape. Asked which other well-known systems
belong in the evaluation, the answer is governed by one rule: a system is added
only for an axis it can carry on the same contract as everyone else, and each
addition is paid for in build time.

Already present. TVM carries run time, extension surface, and the natural-route
rebuild boundary. ONNX-MLIR carries extension surface and preserved unsupported
boundaries, and lacks run time. ONNX Runtime carries the stored references and a
production-runtime ceiling, and is not a compiler peer.

The one substitution that was on the table is wrong. ONNX-MLIR is not
interchangeable with MLIR, because their natural routes to a compiler change are
different objects: ONNX-MLIR asks the researcher to add a C++ accelerator class,
which adds a component, while MLIR Transform asks for a
`TransformOpInterface::apply` body, which is the same object of study as a
derived compiler function. MLIR Transform is therefore the fairer mechanism
comparison and ONNX-MLIR the right end-to-end and run-time one. The repository has
no MLIR build, so MLIR Transform stays a bounded textual comparison until one
exists; adding that build is the highest-value missing data point because it
targets the paper's central claim rather than its breadth.

Candidates considered and their disposition. IREE is a real ONNX-capable
compiler, but its default path runs in its own runtime, so it joins the
deployment band with ONNX Runtime rather than the bare-lowering band with the TVM
C target, and its build is the most expensive of the candidates; it goes last.
XLA and TorchInductor are large builds over HLO and FX rather than ONNX, so they
would add a frontend-conversion variable to every number. Glow is ONNX-capable
and cheaper than IREE, and is the best remaining end-to-end candidate if ONNX-MLIR
turns out to be the only one that can be built here. TensorRT is closed and not
extensible, so it cannot carry the mechanism axis at all. Triton is a kernel
language without an ONNX path. Axon was examined and rejected: its input is a
NumPy-like tensor program rather than ONNX, its target is an AWS Trainium kernel
through the NKI and Neuron toolchain, and the reading card already records that
the two systems are not at the same performance maturity. Comparing run time with
it would produce a number that is unfair to Axon and unconvincing to a reviewer,
so it stays in Related Work as the synthesis and superoptimization contrast,
which is where the literature map already places it.

Order of work this implies: ONNX-MLIR run time, then the extension tasks made
symmetric across four tasks for every system, then MLIR Transform if an MLIR
build is available, then the ten-model matrix extended to TVM, then IREE.

September 17 ONNX-MLIR run time, and a negative result that changes the claim.

The biggest hole is closed. A built ONNX-MLIR 0.4.2 exists outside the repository
at `~/Documents/Item/joggle-study/onnx-mlir`, at the pinned revision
`4a13c34aa695b228599d637cdb772c19b4b18dba` with LLVM
`1053047a4be7d1fece3adaf5e7597f838058c947`, as a native arm64 macOS build. That
is the same revision every ONNX-MLIR record already cites, so no new pin was
introduced. `subject_onnxmlir.py` runs it through ONNX-MLIR's own PyRuntime, and
the library is built with `-O3`.

Two environment facts had to be handled rather than assumed. ONNX-MLIR's
command-line default is `-O0`, so a default-flags column would have compared
unoptimized code against optimized code; the record states the `-O3` flag and the
`-O0` default. And one interpreter cannot run every subject: TVM and ONNX Runtime
live in the study venv at Python 3.11, while PyRuntime is a cpython-314
extension. The driver now carries a per-variant interpreter, which is what let
all five systems run in one job.

A real regression was found and fixed in the process. `subject_tvm.py` had
stopped working: the installed TVM returns `tvm_ffi.container.Array` where it
used to return a list, so the subject's `isinstance(produced, (list, tuple))`
test failed, the whole container was treated as one result, and validation died
with an object array. The check is now structural, and the TVM column measures
again. The compiled TVM library is byte-identical to the one in the record
(`6eb39f88...`), so this was a runtime-API change, not a rebuild.

The five-way result, 20 balanced fresh processes per system in one job, one
thread, load 2.20 before and 2.35 after, every process validated:

| system | median | MAD |
| --- | --- | --- |
| ONNX Runtime, one thread | 3.043 ms | 0.178 |
| ONNX-MLIR, `-O3` | 12.793 ms | 0.256 |
| Joggle after `locality.apply` | 27.772 ms | 0.294 |
| TVM default C target | 39.555 ms | 0.205 |
| Joggle ordinary preparation | 40.844 ms | 0.278 |

Both Joggle rows share one checksum, so the policy still changes no result.

The paper's claim changes accordingly, and the change is a loss. Joggle's
ordinary artifact is within 3% of TVM's unscheduled lowering, and the locality
policy passes it at 1.42x, down from the 1.57x that two separate jobs had
suggested. More importantly, ONNX-MLIR's LLVM-backed `-O3` is 2.17x faster than
the policy-improved Joggle artifact and 3.09x faster than TVM. An optimizing
backend beats both scalar emitters, so the paper now says in the abstract, in the
run-time paragraph, and in the contract table that the advance is in what becomes
editable and reproducible, not in code quality. The abstract's headline fell from
1.57x to 1.42x and gained the 2.17x bound in the same sentence. Deleting the
ONNX-MLIR row was never an option: it is the strongest evidence against the
paper's implicit performance story, and a reviewer who rebuilt ONNX-MLIR would
find it.

The superseded record is not deleted. `paper/data/ultraface-locality-same-host.*`
remains as the four-way job from earlier the same day, and the new five-way job is
`paper/data/ultraface-five-system-same-host.*`. The two disagree because they are
different jobs on a shared laptop, which is why the paper now reports one job's
numbers throughout.

One cost is recorded rather than hidden. The added comparison tables and the
ONNX-MLIR paragraph pushed technical content to end on page 13, past the
twelve-page limit the checklist tracks. The build is 16 pages with no overfull
boxes. Trimming the contract table was not enough to recover the page, and the
work was not continued further because the twelve-page constraint belongs to a
venue this cycle is not submitting to; it is an open delivery item, not a
resolved one.

Separately, the `extensions` rename had left eleven study `build.json` records
pointing at a directory that no longer exists, which broke their recorded
reproduction commands. The path fields were updated and the measurement fields
verified unchanged by hashing the `seconds` values before and after.

September 17 layout checks brought to a defensible state, and two false leads.

The comparison symmetry question turned out to be half wrong. Table 4 is already
symmetric: all four frozen tasks are populated for all three systems. An earlier
reading of the rendered page suggested blank cells, but the cells are long and
wrap, and the table is complete. The real asymmetry is only in the machine
readable records: `extension-tvm-pilot.csv` carries implementation, policy, and
external-kernel, while `extension-onnx-mlir-pilot.csv` carries implementation,
policy, and numeric-format, so the two share two of four tasks. Both systems do
have records for the missing tasks, as `tvm/numeric-format/result.json` and
`onnx-mlir/external-kernel/result.json`, and both are `unsupported`. Separately,
the ONNX-MLIR table has no generator at all: no script in the repository writes
it, so it cannot be regenerated. Wiring both systems through one record collector
is the remaining fix and is not done.

The same pass produced a third instance of one mistake. `paper/experiments/exposure/`
looked unreferenced, and a search for `experiments/exposure` found nothing, but
the module is fully documented in `derive-prepare.md` under its relative link
`exposure/module.jog`, with its SHA-256 and reproduction commands. The lesson has
now repeated three times in this session: the reference search was narrower than
the referencing convention. Search relative links and untracked files, not just
absolute tracked paths.

Four study READMEs were written, for `reuse/`, `guard/`, `exposure/`, and
`pilot/`, and the `study-home` check now passes. Each states its files, its role,
and where the authoritative record lives rather than restating numbers.
`pilot/` is documented as what it is: superseded schema-1 manifests that nothing
cites, kept as the only description of the earlier protocol.

Two checks changed rather than the tree. The `flat-paper-scripts` metric is now
judged acceptable in its own output text, because `paper/README.md` indexes every
script with its output and 54 recorded commands name those scripts, so relocating
them would break reproduction for a cosmetic gain; the count stays visible so a
later change of mind is measurable. And the `records-in-build` check was removed
as redundant: the rule it proxied for is that a study script must not write into
`build/`, which `script-output-root` already checks directly, and as a metric it
could not separate a violation from a CTest working directory, which lives in
`build/` by design and writes exactly the files it counted. The checker now
enforces six rules, reports one judged metric, and records why the seventh is
gone.

September 17 matched-baseline tables made symmetric and regenerable.

The two extension tables were produced by different means and could not agree.
`extension-tvm-pilot.csv` came from `measure_baselines.py`, a runner that only
emits a row for a task it can execute, so TVM's unsupported numeric-format task
had no row. `extension-onnx-mlir-pilot.csv` had no generator at all: no script in
the repository wrote it, so it could not be regenerated, and it likewise omitted
the unsupported external-kernel task. The two tables therefore shared only two of
the four frozen tasks.

`paper/scripts/collect_baselines.py` replaces both paths for the published tables. It
reads the preserved per-task records under `paper/baselines/<system>/`, so one
mechanism produces both tables, every frozen task is a row for every system, and
a task a system cannot complete appears with an empty footprint and
`validation=unsupported` rather than being absent. The runner keeps its job of
executing tasks; the collector's job is to summarise what was preserved.

Three derivation rules were recovered from the records rather than guessed, and
the collector checks itself against the tables that already existed. Artifact
counts are the number of digest keys, not the number of named outputs: a lowered
IR is named once and hashed once while a shared library appears only as a digest,
and counting names undercounts by one on every row. The version comes from
whichever record states it, because the per-task ONNX-MLIR records carry only the
revision and the external-kernel record is the one that nests the version. Counts
are recomputed from the declared sources with the same `source_lines` definition
the Joggle collector uses, and every count a record already states is checked
against the recomputation; the recomputation reproduces 62, 126, and 355 for TVM
and 167, 201, and 516 for ONNX-MLIR, matching the recorded tables cell for cell.

`baseline-table-tvm` and `baseline-table-onnx-mlir` run the collector with
`--check`, which fails on any disagreement between a table and its records, and
the suite is 65 tests. The check was verified not to be vacuous, and the first
attempt at that verification was itself wrong: the corruption was injected with a
pattern that did not match the row layout, so the test passed and appeared broken.
The pattern was corrected, the injection confirmed by printing the affected
columns, and the check then failed with the recorded and recomputed values and
the regeneration command, before the file was restored. Verify the injection, not
just the verdict.

September 17 redundancy pass on the manuscript.

A five-gram scan over the manuscript's sentences found the same point made in
more than one place, which is both the author's objection and the reason the
technical content had grown. Three restatements were removed and nothing else
changed.

The Discussion's deployment paragraph repeated the run-time numbers the
evaluation table already carries, including the 3% parity and the 1.47x speedup,
and added a "measured rather than promised" flourish. It now states what the
substrate does and does not buy and leaves the numbers where they are measured.

The Figure 2 caption argued the MLIR Transform comparison, which is exactly what
Section 6.1 argues at length. A caption that argues competes with the section it
should point at, so the caption now describes what the inset shows and refers to
the section, and the section gained the label the reference needs.

The second run-time paragraph restated the UltraFace result that the subsection
immediately above had just reported, including the 2.17x ONNX-MLIR comparison in
both places. It now opens by pointing at the table and spends its words on the
models that only it reports.

The build went from 16 pages to 15, with no overfull boxes and no unresolved
references. Technical content still ends on page 13, so the twelve-page limit is
still exceeded by one page. Recovering it from here would mean removing a display
or a result, which trades evidence for layout on a limit that is inactive for
this cycle, so the recovery stops at removing redundancy and the remaining page
stays recorded as an open delivery item rather than a resolved one.

One thing the pass confirmed about the earlier trims: the hedge count per
paragraph is a poor guide to what to cut. The densest passages are the table
notes that qualify the ONNX Runtime, TVM, and ONNX-MLIR rows, and those
qualifications are the evidence discipline the paper needs. Redundancy, measured
by repeated phrasing, was the signal that led to real cuts.

September 17 abstract restructured, coverage extended, and two failures recorded.

The author challenged the abstract on three counts and was right on all three.
It was long, at 231 words and ten sentences. Its first quantitative value was the
single-model 1.42x result and the ten-model range came second, which is leading
with the most favourable framing even though both numbers come from records. And
the prose around it was unclear. It is now 187 words and seven sentences, with
two hedged ones, and it leads with the range: on every model of a ten-model
campaign the policy improves run time, by 1.47x to 3.23x with a median of 2.11x,
all with bit-identical outputs. The single-model cross-system comparison follows
rather than leads, and one clause bounds the whole thing.

Coverage extended rather than asserted. The author asked why the remaining models
were not fixed to show breadth, and two of them now are. EfficientNet-Lite4 INT8
and its QDQ variant were absent from the cache, so their gates had never run at
all; both were downloaded and both pass, `onnx-zoo-efficientnet-int8` and
`onnx-zoo-efficientnet-qdq`, which takes executed coverage from ten models to
twelve. Their rows cannot enter `paper/data/model-coverage-pilot.csv` yet:
`collect_models.py` refuses to collect from a dirty tree so that every row carries
a trustworthy revision, and this working tree is uncommitted by instruction. The
two passes are recorded here and the ledger regenerates after the author commits.

The remaining gaps are not the same kind of thing and the paper must not present
them as one failure class. TinyYOLOv3-11 retains 219 unknown results and
SSD-MobileNetV1-12 retains 386 source calls, both from data-dependent shapes that
a static-shape C emitter does not support; XCiT's conversion is clean and its
only blocker is the recorded C-preparation timeout; BiDAF-9 is heavy and is gated
on a structure round trip rather than execution.

Two failures were recorded rather than worked around. The ten-model comparison
extended to TVM failed on `squeezenet1.0-13-qdq`, where TVM's output misses the
stored reference by 2.07e-2 with 30 of 1000 values outside the study's 1e-4
tolerance. A timing is not reported for a variant that does not validate, so that
model has no TVM column and the boundary is stated instead. And the run stops at
the first failure by design, so the remaining models' TVM columns are unmeasured
rather than absent by choice.

September 17 paper workspace reorganised, and two coverage corrections.

The author asked whether the results and scripts could be organised cleanly, and
they could not be as they stood: `paper/` had 31 files at its top level, 19 of
them measurement scripts, seven study documents, two CMake pilots, a task
contract, and a `__pycache__`. The scripts now live in `paper/scripts/`, the
study documents in `paper/studies/`, the pilots beside the other experiment
drivers under `paper/experiments/`, and the task contract under `paper/tasks/`.
The top level is four files and eleven directories, and the only markdown left
there is the two a reader starts from. The scripts moved as one directory rather
than being sorted into subdirectories because three of them import each other by
module name, and grouping them by function would have added a level without
adding meaning.

Two scripts broke in a way that a path search cannot catch, and the layout checks
caught neither. `render_models.py` derived the paper directory from `__file__`
and would have looked for its records and figures under `scripts/`, and
`collect_baselines.py` derived the repository root the same way and would have
read the contract from the wrong place. Both now state the parent explicitly. The
lesson is to grep for `__file__` in any script that moves, not only for the paths
it mentions.

The move broke 37 markdown links, because relative links carry no `paper/` prefix
and the earlier path rewrite could not see them. They were repaired in three
passes driven by the checker's own findings rather than by guessing, and the
first two passes are worth recording. The first prefixed `../` to every link that
lacked one, which silently broke sibling links inside `baselines/onnx-mlir/` that
had been correct all along. The second resolved each link against the document
directory, the paper directory, and the repository root, and could not repair a
link whose depth had merely overshot. It took a third rule, resolving the
remaining names inside the new subdirectories, to reach zero. A link rewrite must
be driven by existence rather than by pattern, and the checker that reports the
breakage is the right work list.

Coverage, corrected twice. EfficientNet-Lite4 INT8 and QDQ were absent from the
model cache, so their gates had never run; both were downloaded and both pass,
which takes executed coverage from ten models to twelve. The other two apparent
gap models do not extend it. TinyYOLOv3-11 and SSD-MobileNetV1-12 now pass their
gates too, but those gates assert the recorded conversion frontiers, Loop and
NonMaxSuppression retained and a conversion frontier of zero, which is boundary
evidence rather than execution; their ledger rows still read `not_run` for
execution, and the paper must not count them as executed coverage. The
distinction matters because the first reading of those two passes would have
claimed fourteen executed models.

The suite is 67 tests, including the two EfficientNet gates, and the record
stores were left flat. `paper/data/` is a ledger of 62 records with a README that
indexes each one, and splitting it by name prefix would create fifteen small
directories without adding meaning while breaking the evidence paths the
manuscript cites.
