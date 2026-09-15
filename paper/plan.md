# Research and submission plan

Frozen: 2026-09-15. This is the single active plan for the EuroSys submission.
The engineering backlog remains in [the roadmap](../docs/roadmap.md), verified
source comparisons in [the literature map](literature-map.md), and experimental
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

The research question is whether this representation reduces the work needed
to customize and compose compiler behavior while retaining useful optimization
and acceptable compilation cost. Naming every action a function is insufficient:
the function body must actually be available to the relevant edits and execution.

## Two mechanisms, one research question

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

Module loading, types, verification, and rollback support these mechanisms.
They are not additional headline contributions or a new taxonomy of roles.
No `MetaPass` class, parallel compiler IR, generated wrapper layer, or second
DSL is planned. Native code is an explicit opaque boundary.

The motivating example `f3(f2)` is one case of derivation, not an unrestricted
self-modifying compiler. First derive an inactive copy, then execute that copy.
Optimizing a procedure's body is different from invoking it to optimize its
input. Neither implies that arbitrary procedures can safely optimize themselves.

## Status and evidence obligations

| Item | Inspected implementation or preserved evidence | Still required |
| --- | --- | --- |
| Progressive representation | Common IR, function cloning, structural edits, semantic exposure, C/VM paths | A serialized mixed-state trace from a real model and a precise account of identity/invariant preservation |
| Function execution | Typed invocation with/without a subject; separate code/model `Mod` values; copied generator edited; private lexical helper closure captured; real `c.prepare` algorithm edited and source-isolated on a typed-model regression and [four complete-model IR inputs](experiments/derive-prepare.md) across three network architectures | General rewrite conditions, matched mechanism control, and isolated cost beyond this pilot |
| Reuse across roles | Source modules implement analyses, policies, transformations and generation | Demonstrate independent composition, not merely several unrelated examples |
| Derivation performance | Existing `c.prepare` copied, edited, and output-checked on ONNX/TFLite MobileNetV2, UltraFace, and SqueezeNet; [phase probe](experiments/derive-prepare.md#phase-boundary-probe) shows the edited second exposure was inactive on these inputs; unisolated macOS times show no established speedup | A useful derived change, rewrite precondition, repeatable cost, and matched alternative; do not claim acceleration from this pilot |
| Complete artifacts | Versioned ONNX/TFLite numerical paths and negative runtime/scaling records in the study files | Matched end-to-end measurements with environment and failure boundaries |
| Usability and agents | Text modules and shared APIs exist | No measured user productivity or agent success advantage; neither is a current result |

Automatic effect inference, cross-run dependency-aware caching, traversal fusion,
JIT specialization, and agent-assisted authoring are research directions, not
implemented claims. Existing per-evaluation caching is not an incremental
compiler. Valid IR does not prove semantic equivalence.

## Work order and acceptance

### P0: freeze the argument and establish a decisive mechanism result

- [x] Preserve the title and consolidate competing plans here.
- [x] Separate the two mechanisms from package/safety support and future work.
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
      equality. If no legal reusable optimization is available, report the
      boundary and do not promise a new optimizer before submission.
- [ ] Establish the closest mechanism control using MLIR Transform or xDSL.
      Compare TVM/Relax and ONNX-MLIR on the existing artifact study, not on an
      artificially Joggle-shaped API. AnyDSL is a staging/optimization
      counterexample; do not call an unimplemented port a measured baseline.
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

1. A running change explains the problem and what the strongest alternative
   already supports. Do not use IR count as a proxy for difficulty.
2. A wide mechanism figure embeds short real IR fragments: abstract call,
   exposed body, selected implementation, and a separately derived compiler
   function. Show actual edits and dependencies rather than boxes of slogans.
3. A dense native-LaTeX cross-system table reports the decisive experiment:
   preserved behaviors, changes/rebuild scope, costs, and explicit frontiers.
   No pass inventory or unsupported yes/no superiority matrix.
4. Aligned complete-model panels show latency, compile cost, memory, and
   artifact footprint. Separate compiler RSS from runtime workspace; do not
   stack alternative artifact formats as though all are deployed together.
5. The Axon-style wide LaTeX speedup table is supporting evidence only. Each
   cell is an actual baseline/Joggle ratio for that shape and environment.
   Unsupported, timeout, and unmeasured are distinct. Do not replace it with
   a raster heatmap or repeat one measurement across columns.

The abstract should state problem, gap, mechanisms, strongest measured result,
and boundary concisely. Related Work explains why competing designs make sense,
what they already establish, and the specific remaining question. Source lines
are descriptive, not a productivity metric. No invented numbers, implied user
study, universal speed claim, or claimed agent advantage.

## Calendar and release gate

Official [EuroSys 2027 dates](https://2027.eurosys.org/cfp.html):
abstract September 17 AoE (September 18, 11:59 Beijing); full paper September 24
AoE (September 25, 11:59 Beijing). Twelve technical pages plus references.

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
