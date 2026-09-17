# EuroSys submission draft

`main.tex` is the authoritative manuscript. The title is fixed:
**Joggle: Malleable Compilation with a Progressive Intermediate Representation**.
The manuscript is a research draft, not submission-ready.

## Argument and organization

The submission thesis is now singular: Joggle makes a source-defined compiler
algorithm body a unit of reuse. A derived compiler function copies the selected
definition and private helper closure, admits an internal structural edit, and
executes on a separate subject while the original remains callable. Module
resolution, replacement validation, query isolation, and rollback are supporting
boundaries. Consumer-directed exposure is a companion use of the progressive
representation; dependency-sensitive recomputation remains future work.

The opening establishes the coupling of computation, numerical representation,
and hardware before explaining the three customization obstacles in the
[research plan](../plan.md). The method separates editable compiler definitions
from the subject program they inspect or transform. Module resolution composes
definitions; consumer-directed exposure controls implementation detail.
Precise dependency-sensitive recomputation remains proposed, not measured.

The abstract was rewritten on September 17 after the author rejected the
previous version as long and defensive: it had 242 words of which six of twelve
sentences carried a negation or a hedge, and it ended on a list of weaknesses.
It is now 210 words with four hedged sentences, and it leads with the mechanism's
structural effect and with the measured advantage. It carries the quantifiable
result the author asked for: a source-defined locality policy applied without a
rebuild makes the artifact $1.57\times$ faster than TVM's default C-target
lowering on one host, with a bit-identical checksum and per-model gains of
$1.47\times$ to $3.23\times$ in a ten-model campaign. The coverage fact of
fourteen pinned models moves to Evaluation, where the per-model status belongs.
The single boundary sentence that remains names the interpreter and the scalar
backend, and no case result is presented as an aggregate or external speedup. The
policy reductions are recomputed from raw trials in [the model-figure
record](../figures/models.md#abstract-evidence-selection).

The September 16 method audit now distinguishes copied read-only query stores,
mutating-sequence backups, and cross-store derivation rollback. It states that
verification is module-wide, that direct `Fn` queries bypass the persistent
named cache, and that the named cache is keyed by the whole subject revision
rather than fine-grained dependencies. Per-function sequence timers exclude
the initial backup and input verification. These are source observations, not
measured cost advantages.

The Introduction closes with a four-item contribution list. Motivation has
one central obstacle (restricted customization) and one subsection for the
two operational pressures. Evaluation opens with four explicit questions
(Q1 to Q4) and labels each subsection with the questions it answers:

1. Primary mechanism: an internal storage-planner edit, its artifact consequence,
   a source-patch control, refactoring checks, stale-helper limits, a second
   derivation that retargets the cleanup step inside `c.prepare` (18 guards
   folded, bit-identical outputs, no latency change), and the
   executed TVM natural-route control (Table 2: same rule, compiler rebuild,
   feedback time, storage under each system's accounting, same oracle).
2. Supporting boundary: cross-role composition and checked replacement behavior.
3. Breadth: complete-model execution, recorded runtime comparisons, and the
   same-host UltraFace comparison against TVM's unscheduled C target and ONNX
   Runtime (`paper/data/ultraface-same-host.*`).
4. Cost boundary: interpretation/traversal costs attributed by stack sampling to
   value representation, timeouts, and coarse caching.

The declared-static-slot reduction is a one-model/input result, not peak RSS.
The source-patch control obtains the same plan; lower maintenance cost remains
unmeasured. The TVM natural-route control (September 16, one model, one host)
requires a from-source compiler build (514 s clean, no LLVM) and an 8-second
one-unit rebuild per edit, plans natively in 0.15 s against Joggle's 2.5 s, and raises TVM's
planned storage by 1.06% under the same oracle; the storage columns use each
system's accounting and are not cross-system plan quality. Model coverage aggregates revisions. Runtime comparisons retain
their own hosts, revisions, oracles, and adjacent controls.

## Displays

The [figure index](README.md) owns assets and source mappings.

- Figure 1: approved obstacle/response overview.
- Figure 2: asymmetric architecture view and bounded MLIR Transform comparison.
  It replaces the three-card trace drawing, not the preserved
  [MobileNetV2 trace](../experiments/progressive-trace.md).
- Figure 3: compiler-function derivation and separate subject execution,
  with literal API excerpts in native LaTeX.
- Figure 4: additive-overload replacement failure.
- The reconstructed Linux latency figure remains in the artifact record but is
  no longer a main-paper figure. Its cohorts and negative runtime results remain
  reported in text; the released space holds the comparison tables below.
- Table 1: the refactor-survival matrix. Four unrelated source edits against the
  same planner change, showing where the source patch stops applying while the
  IR derivation still reproduces the validated plan.
- Table 2: the two-route storage-planner comparison (Joggle derivation against
  TVM's natural source route) from the external control record.
- Table 3: observed extension routes and failure boundaries. The old node-count
  table and source-line-count presentation have been removed.
- Table 4: the same-host five-system UltraFace run time (ONNX Runtime, ONNX-MLIR
  at `-O3`, Joggle after `locality.apply`, TVM default C target, ordinary Joggle),
  all in one job. The caption states that
  the TVM row is its unscheduled default C target, that the ONNX-MLIR row is its
  `-O3` route because that tool defaults to `-O0`, and that ONNX Runtime's
  accuracy row is a definition, not an independent check. The ONNX-MLIR row is a
  negative result: its optimizing backend is 2.17x faster than the
  policy-improved Joggle artifact, and the paper states that in the abstract, the
  run-time paragraph, and the contract table.

No raw measurement records were deleted. The operator shape matrices remain a
native-LaTeX plan pending admitted measurements, not raster tables or repeated
placeholder cells. Matched compilation time, compiler RSS, runtime workspace,
and deployment footprint do not yet form a complete-model comparison suite.

## Reproduction and checks

```sh
python paper/scripts/render_models.py
latexmk -pdf -interaction=nonstopmode -halt-on-error \
  -cd paper/submission/main.tex
```

The plot needs matplotlib and numpy; recorded versions and input hashes are
in `paper/figures/models.json`. It does not require an installed skill.

The latest PDF has 15 pages including references, down from 16 after a
redundancy pass, and technical content still ends on page 13, one page past the
limit the checklist tracks. The pass removed three restatements rather than
substance: the Discussion no longer repeats the run-time numbers that the
evaluation table already carries, the Figure 2 caption describes the figure
instead of arguing the comparison that Section 6.1 makes, and the second
run-time paragraph stops restating the UltraFace result it sits beside. Closing
the remaining page would mean cutting a display or a result, and the limit
belongs to a venue this cycle is not submitting to, so it stays an open delivery
item rather than a resolved one. There are no overfull boxes and no unresolved
references. Bibliography-field warnings and the ACM reference-format warning
remain; image-generated mechanism labels require a 10-point minimum-size audit. A
successful build is not venue compliance.

[CHECKLIST.md](CHECKLIST.md) tracks submission blockers.
The [literature map](../studies/literature-map.md#implementation-boundary-comparison)
records primary-source comparison scope. Evidence IDs remain in the
[derivation study](../experiments/derive-prepare.md#manuscript-evidence-map),
[extension study](../studies/extension-study.md#manuscript-mechanism-evidence),
and figure records. Accountable author verification, required disclosures,
anonymity review, and final approval remain open.
