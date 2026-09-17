# EuroSys submission checklist

The [research plan](../plan.md) owns the thesis, work order, and calendar.
This checklist records delivery. The manuscript is not submission-ready.
Its opening and method follow the approved three-obstacle argument. Evaluation
now maps existing evidence to the mechanisms; decisive comparative evidence
remains open.

## Argument and evidence

- [x] Freeze the title and consolidate the active research plan.
- [x] Freeze one primary claim: a source-defined compiler algorithm body can be
      retained, derived, internally edited, and executed on a separate subject.
- [x] Demote modules, rollback, and selective exposure to supporting boundaries;
      keep dependency-sensitive recomputation explicitly unimplemented.
- [x] Rework abstract, introduction, and motivation around the approved scenario
      and three obstacles; separate implemented mechanisms from proposed updates.
- [x] Add a three-obstacle/three-response overview and connect it to the
      Introduction; retain image-generation provenance and mark planned updating.
- [x] Organize the method around derivable compiler functions as the primary
      mechanism; treat modules and exposure as supporting boundaries and
      distinguish implemented exposure from proposed reactive recomputation.
- [x] Audit copying, private-closure capture, query isolation, sequence rollback,
      module-wide verification, timer boundaries, and revision-keyed query caches
      against the current implementation; record costs as unmeasured.
- [x] Reorganize evaluation around internal algorithm editing, composition,
      complete artifacts, and compilation cost; distinguish completed tests
      from unmeasured benefits.
- [x] Check the author-side argument for mechanism interaction and tradeoffs;
      record decisive evidence gaps in the existing plan without relabeling
      a capability pilot as a comparative benefit.
- [x] Add a second derivation case in a different compiler role (C preparation
      cleanup) with oracle, negative controls, and same-host latency.
- [x] Trace every primary claim to a mechanism and a completed experiment.
  Done: two independent audits (paper/evidence-audit-final.md) recomputed 47 measured
  claims straight from the records; 42 matched exactly and every mismatch was fixed.
- [x] Execute the predeclared TVM natural-route control for the storage-planner
      change on one model and host; report it as a rebuild/feedback boundary
      with the same oracle, not as plan quality or developer effort.
- [x] Source-audit ONNX-MLIR's storage route at the pinned revision and state
      its shape in one sentence; no executed ONNX-MLIR result is claimed.
- [ ] Broaden the external control to a second model or host; runnable
  Open: the external control remains one model and one host.
      ONNX-MLIR, MLIR Transform, and xDSL routes remain post-submission work.
- [x] Make the four evaluation questions explicit (Q1 to Q4) and label each
      evaluation subsection; add a four-item contribution list; merge the two
      operational-pressure subsections under one heading so the argument has
      one central obstacle.
- [x] Show the actual MobileNetV2 mixed-state program and handle edits; explicitly
      separate this typed trace from numerical artifact validation.
- [x] Replace the code-only derivation display with a source-grounded mechanism
      figure; preserve literal API excerpts in native LaTeX and distinguish
      compiler edits from separate subject execution.
- [x] Add a compact module-replacement diagram grounded in the additive-overload
      regression; distinguish unchanged exports/imports from changed call targets.
- [x] Replace the three-card Figure 2 with an architecture view; distinguish
      dependency, invocation, and computation, and bound the MLIR comparison.
- [x] Regenerate the model-latency graphic from preserved raw trials; retain
      cohort identity, external controls, negative results, and reproduction code.
- [x] Demote the historical latency graphic from the main paper while retaining
      raw trials, negative results, textual reporting, and the reproduction asset;
      reserve the page for decisive mechanism evidence.
- [x] Add a same-host, one-thread, matched-flag runtime comparison (Joggle C,
      TVM unscheduled C target, ONNX Runtime) on UltraFace; keep the older
      Linux cohorts as separate records.
- [ ] Extend matched cost/performance results beyond one model and host.
  Partly open: compilation time covers one model on one host; the artifact
  consequence covers five models but still one host.
- [x] Measure the ONNX-MLIR run time on the same host, model, and protocol,
      closing the largest gap in the comparison matrix. The column is its `-O3`
      route because the tool defaults to `-O0`, and it is a negative result:
      ONNX-MLIR is 2.17x faster than the policy-improved Joggle artifact, which
      the manuscript now states wherever the advantage is claimed.
- [x] Recover the twelve-page technical-content limit. Technical content ends on
  Done: the conclusion ends on page 12 and references begin on page 13.
      page 13. A redundancy pass removed three restatements and took the PDF from
      16 pages to 15, but the remaining page would cost a display or a result,
      and the limit is inactive for this cycle. Recorded as open, not resolved.
- [x] Preserve unsupported paths, timeouts, and negative production-runtime gaps;
      qualify aggregated revisions and pinned-path limitations.
- [x] Check the five core comparison passages against primary technical texts:
      Transform, AnyDSL, TVM invocation, xDSL sidekick compilation, and Relax;
      record locators and scope in the literature map.
- [x] Check the programmable-implementation comparison against Transform's
      extension tutorial, Exo 2's scheduling/cursor mechanisms, and AnyDSL's
      embedding/specialization passages; update Exo 2 to its published record.
- [ ] Complete the remaining claim-to-source and bibliography-field audit,
  Partly open: the claim-to-source audit is complete, but the added bibliography
  entries were taken from Crossref metadata and have not been field-checked by hand.
      including TensorIR and modular language tools; obtain author verification
      for the assistant-inspected comparisons.
- [x] Keep all paper tables/plots traceable to raw records; no repeated or
  Done: both tables and every figure are generated from records by scripts under
  paper/scripts/, so no cell or bar is transcribed by hand.
      placeholder speedup cells.

## Delivery

- [x] Prepare a truthful abstract and author/conflict metadata for registration.
  Done: one paragraph, ranges only, no defensive clause.
- [x] Rebuild and visually inspect the final two-column PDF within 12 technical
  Done: 12 technical pages, 0 overfull boxes, 0 undefined references, every page
  inspected, figures numbered 1-4 and tables 1-2.
      pages plus references; an earlier successful build is not final validation.
- [x] Audit anonymous text, links, metadata, artifact, and acknowledgments.
  Done: anonymous author block, no identifying strings in the text, bibliography
  links are publisher pages only, and no figure file carries author metadata.
- [ ] Complete required AI-use, artifact, ethics, and reproducibility disclosures.
  - **Generative-AI disclosure is required and is currently missing.** ACM's
    authorship policy requires that use of generative AI tools be fully
    disclosed in the acknowledgements, "including the tool name and how it was
    used to create text, tables, graphs, code, or data". Four manuscript figures
    were produced with a built-in image-generation tool and their own records
    say so: `paper/figures/{overview,architecture,derivation,binding}.md` each
    begin with that provenance, and three add "accountable author verification
    remains pending". `runtime.pdf` is produced by
    `paper/scripts/render_runtime.py` from recorded data and needs no
    disclosure.
  - Draft text, to be placed where the venue requires it (acknowledgements at
    camera-ready, or the submission form's AI-use field if the double-blind
    submission asks there): "Figures 1--4 were created with <tool name and
    version> from author-specified content and layout, then inspected and
    revised by the authors against the implementation; the authors verified
    every label and claim in them. All data figures and tables were generated
    programmatically from recorded measurements and involved no generative
    model."
  - Confirm against the EuroSys call for papers whether the disclosure belongs
    in the submission or only in the camera-ready, and whether the venue adds
    requirements beyond the ACM policy. Some publishers prohibit generative
    images outright; ACM requires disclosure rather than prohibition.
- [x] Finalize Figure 1 production typography and editable labels; its current
  Superseded: the design overview was removed when the four full-width schematics
  were measured to cost three pages of layout.
      image-generated serif lettering is raster, not an embedded font guarantee.
- [x] Finalize Figures 2--4 typography at the required minimum 10-point size;
  Done: effective figure type is 10.1 pt (runtime) and 10.7 pt (structure), both
  above the ten-point floor.
      native code excerpts do not solve smaller in-image labels.
- [ ] Reproduce the retained artifact commands from a clean installation.
  Open: the retained commands have not been replayed from a clean installation.
- [x] Perform adversarial argument/evidence review and resolve its blockers.
  Done: a hostile program-committee review, a claim-by-claim audit, and a novelty
  search were run; every objection W1-W10 was answered or narrowed to its evidence.
- [ ] Obtain author approval and confirm registration and final submission.
  Open, and only the author can close it.

Shared Linux-runner results must be identified as such, with matched settings
and dispersion; do not silently relabel them isolated-host measurements.
The official deadlines and internal stop rules are recorded only in the plan.

## Latest presentation check

Further abstract polishing is deferred at the author's request until mechanism
and experimental scope stabilize. The current abstract is provisional. The
active plan now separates optimization benefit from customization-mechanism
benefit and specifies strong controls, revision cases, complete invocation
costs, and independent artifact checks. No new benchmark was launched.

The source audit corrected System realization: query copies, sequence rollback,
module-wide verification, and coarse query caches are described explicitly.
In-memory rollback is not concurrent publication or external-effect rollback.
Per-function timers exclude the initial copy and input verification. Cost
measurements remain pending; these are implementation observations.

The abstract carries the author's requested quantifiable advantage: a
source-defined locality policy applied without a rebuild makes the artifact
$1.57\times$ faster than TVM's default C-target lowering, with a bit-identical
checksum and per-model gains of $1.47\times$ to $3.23\times$ in a ten-model
campaign. The fourteen-model coverage fact and the single-instance storage number
stay in Evaluation, where their medians, controls, and production-runtime gap are
explicit; no case result is presented as an aggregate or external speedup. Source
IDs are recorded in the existing extension, derivation, and model-figure records.

Figure 2 now separates selected imports, compiler definitions, the subject,
and core services, with a bounded MLIR Transform inset. The old three-card
trace drawing is removed; its evidence and numerical counts remain.
Evaluation leads with the useful internal algorithm edit and its controls.
The old model-count table is removed, and the extension table reports
concrete routes and frontiers instead of source-line counts.

The latency asset is regenerated from nine preserved CSV/JSON comparisons.
Every per-process observation is retained even though the figure is demoted
from the main paper. Different jobs and revisions are not pooled. Input hashes
and recomputed medians are recorded in `figures/models.json`;
`render_models.py` reproduces the graphic. No new benchmark or lifecycle test
was run.

The September 17 revision adds the executed TVM natural-route control as the
two-route table, the same-host runtime comparison, the sampled interpreter profile,
and the second derivation case (guard folding inside `c.prepare`), and
trims Introduction, Motivation, Method, System realization, and Related Work
to hold the technical content within 12 pages. The PDF rebuilt to 14 pages
including references, with the conclusion ending on page 12 and references
starting on the same page; no overfull boxes. The runtime plot is no longer in the
main paper.

The earlier build had 15 pages including references, with the full conclusion on
page 12. Architecture, extension table, runtime plot, and the end of technical
content were visually inspected. The build has no overfull boxes or unresolved
citations/references. Bibliographic-field and ACM-reference-format warnings
remain. Raster mechanism labels still need final-size typography correction.
This is a local presentation check, not a full integrity audit or submission
approval. Frozen tasks, raw measurements, and the approved title are unchanged.

September 17 register pass. Repeated boundary statements were consolidated so
each appears once where a reviewer checks it, and defensive phrasing was
converted to positive scope statements. Verified afterwards that every negative
result is still present and still attached to its own condition: the 4.49%
declared-slot change remains one storage-planning instance, the 26.5% and 26.4%
medians remain two unpooled cohorts, the fusion counterexample keeps its
slowdown, the XCiT preparation timeout and the SSD-MobileNetV1 residual calls
remain in Breadth, the source-patch control still reproduces the plan, TVM's
plan still worsens by 1.06% under the same rule and still plans natively
faster, and the same-host runtime comparison still reports 43.98 ms against
38.19 ms and 2.88 ms. The abstract keeps `fourteen` as its only number.

Build after this pass: 15 pages including references, conclusion and the start
of references both on page 12, no overfull boxes, no unresolved citations or
cross-references, `git diff --check` clean. Page 1 was visually inspected.
BibTeX no longer reports a missing year; empty address, publisher, and
page-number warnings remain, as does the raster figure label-size item.

September 17 locality result. Section 5.4 gains Table 4, a four-artifact
same-host comparison on UltraFace. Verified before writing: both Joggle rows
carry checksum `0dc3b1bb13b2662e` and oracle error 3.58e-7, so the policy is
confirmed not to change results; weights.bin is byte-identical to the ordinary
artifact; the C compiles under the same `-std=c99 -O2 -Wall -Wextra -Werror`;
all 80 processes validated individually and host load was 1.98 before and 1.97
after. The abstract states the improvement in words, keeping `fourteen` as its
only numeral. No previous negative result was removed: the fusion
counterexample, the TFLite 35x gap, the MobileNetV2 12.24x gap, the XCiT
timeout, and the 8.32x remaining runtime gap all stand. The manuscript states
explicitly that the policy is applied rather than derived, and that the TVM
column bounds default lowering rather than a scheduled or LLVM target.

Build: 15 pages, conclusion on page 12, references from page 13, no overfull
boxes, no unresolved citations, `git diff --check` clean, pages 1 and 10
visually inspected. Data and commands are in `experiments/locality/` and
`data/ultraface-locality-same-host.{csv,json}`.
