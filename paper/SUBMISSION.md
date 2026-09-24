# EuroSys 2027 submission packaging

Official source: [EuroSys 2027 Call for Papers](https://2027.eurosys.org/cfp.html),
checked 24 September 2026. This file is an author-side checklist, not manuscript
content.

| Upload | Local file | Contents |
| --- | --- | --- |
| Paper | `sigconf.pdf` | At most 12 pages of technical content, followed by references. |
| Supplementary material | `supplement.pdf` | Detailed measurements, task contracts, worked input/output pairs, and graph transformations. |

Do not concatenate the two PDFs for the paper upload. The CFP permits
additional results and proofs in a separate supplementary file; reviewers are
not required to read it. The main paper therefore contains the mechanisms,
experimental controls, primary results, and conclusions needed to assess the
claims. Supplementary material does not create extra technical-content pages
in the main upload. The CFP states no separate supplementary page limit.

This supplement is not an Artifact Evaluation appendix. Artifact Evaluation
is a later, post-acceptance process with its own instructions.

## Format checks before upload

- A4 or US Letter; text and figures within a 178 × 229 mm (7 × 9 in) block.
- Main paper: two columns, separated by at least 8 mm (0.33 in).
- All text, including figures and captions: at least 10 pt, with at least
  12 pt leading. Figures must remain readable in grayscale without magnification.
- Every page numbered; anonymous manuscript, supplement, links, and metadata.
- Disclose AI-tool use as required by the CFP. The disclosure must describe
  the actual assistance used.
- Fall full-paper deadline: 24 September 2026, Anywhere on Earth
  (25 September, 19:59:59 in Asia/Shanghai).

## Current checks

The main PDF has 15 pages. Technical content ends on page 12; pages 13--15 contain
references only. The separate supplement has twelve pages. Table A.5's 12 paired
input/output examples occupy five portrait pages, with verbatim quoted task
contracts and 8.5 pt DOT labels. Section C.4 uses portrait tables while retaining its diagram typography
and display sizes. All supplement pages are portrait. Both entry points explicitly enable page numbers.

The four measured figures use the author's requested compact review layout:
single-column 3.33 × 2.25 in, two rows by three columns, 5.5 pt text, and
single-row legends. Data, estimators, and scales are unchanged. This figure
typography is below the CFP's explicit 10 pt requirement for figures and
captions; the compact review layout is not a completed submission-format check.
Architecture illustrations still need a printed-size audit. Some table bodies,
bibliography text, and ACM caption defaults are also below 10 pt. Resolve the
typography and recheck pagination before upload.
The two-column rule is explicit for the paper; the CFP gives no separate
layout specification for the optional supplement.
The author has chosen to retain the compact figure layout for the current
revision. This choice does not establish compliance with the font-size rule;
no typography reflow is scheduled in the present content/data pass.

## Code and measurement alignment

The complete native-package comparison is collected in
`data/package-footprint.csv`, with hash-bound source records in the adjacent
JSON file. It contains eight conditions across three systems: two initial
integrations and six independent maintenance changes. All 24 changed packages
pass 201 semantic fixtures and 16,011 runtime probes; all 18 parent controls
fail the corresponding changed contract. Counts describe the observed
implementations, not hunk-minimized patches. The main text reports the
integration difference and the equal maintenance file/ownership counts.
Figure 7 separates integration files, integration lines, and maintenance lines
in a six-panel display. Appendix E adds complete footprints (A.13), native
installation files (A.14), and six worked maintenance input/output pairs (A.15).
The related-work table places nine systems/frameworks in one horizontal band,
with grouped interface, ownership, and update rows. Its measured integration
rows show source files, source lines, and separate registration code for the
two native packages; other systems are marked unmeasured. Figure 6 uses four
independent protocol vignettes, distinct from the author's 3v3 overview. The main PDF
now cites 48 sources. The literature expansion covers IRDL, LMS, AnyDSL,
Exo, Ansor, TASO, Mirage, PluS, egglog, self-adjusting computation, IncA,
SWE-agent, Delite, Forge, Stratego, Lift, RISE/ELEVATE, guided equality
saturation, MetaSchedule, Relax, Tensor Comprehensions, TACO, Shake, pluto,
PIE, differential dataflow, SWE-bench, AutoTVM, and Glow.
Citations occur in the introduction, motivation, and
related-work comparisons; every bibliography entry is cited. KernelBench,
TensorIR, and Exo 2 now use formal proceedings records rather than preprint
metadata. Primary papers, project documentation, institutional records, or
official proceedings were opened during the reference check; source locators
for the additions are recorded in `references.bib` comments. Final author
review remains part of submission approval.
The local Agent collection is stopped. The authors authorized SiliconFlow
collection on 24 September; Qwen3-8B and Qwen3-14B are selected for the same
72-condition matrix. Both model identifiers are available in the authenticated
model list. Two connectivity-only JSON requests succeeded at temperature zero
with thinking disabled (46 input tokens and 10 output tokens in total); they
are not task outcomes. The returned model identifiers match the requests;
the provider returns an empty system fingerprint, not a weight revision.
The credential is stored in the macOS Keychain, outside this repository.
The existing local-only runner and merger still need the API adapter before
formal collection. Complete the matched population and its displays before
finalizing the evaluation. Do not run native Agent tests during performance
timing. Protocol controls are synchronized in Section 4.2 and Appendix C;
result fields remain unfilled until collection.

The repeated-update data in `data/figure-06-update.json` now identify integrated
main source revision `cc82ef114093b6d90ca94df05b54a60084585e72`, including
prepared-body reuse and its invalidation, transfer, and storage fixes. The
540 raw rows, 180 phase records, 27 edit/system summaries, and both Appendix D
tables were replaced from the completed matched collection. The source audit
checks population, inputs, edits, compiler identity, and paired output equality.
Joggle and ONNX-MLIR each pass all 180 trials; TVM passes 120 and rejects all
60 TinyYOLOv3 trials because its ONNX frontend does not support `Loop`.
The measured paired reuse range is now 1.46–2.49×. Abstract, introduction,
evaluation, and conclusion are synchronized in MD and TeX. Figure 8 and both
PDFs still require regeneration after the live execution timing finishes.
The external-data integration check at
`.cache/artifact/external-data-verified.qpfoeC/` is a separate single-repetition
correctness run, not a replacement for the paper's repeated measurements.

The integrated-main repeat completed serially in
`.cache/artifact/main-updates-20260924-H8DNhW/`: Joggle, TVM, then ONNX-MLIR;
three models, nine edit sites, ten repetitions, and two policies per system
(540 records). The measured source revision is
`cc82ef114093b6d90ca94df05b54a60084585e72`. Paper-only revisions do not alter
that source identity. All three collectors record stable, clean source state
throughout collection. Their generic full-population release flag remains
false because this study uses the declared three-model edit population rather
than the broader model manifest; the paper exporter validates the complete
540-trial population explicitly.

End-to-end and package measurements have separate source records. The operator
merge now includes both Joggle variants from integrated main `cc82ef114093`:
24 correct cases and 2,400 measured samples per variant. The reused ORT, TVM,
and ONNX-MLIR records match the new runs' workload contracts, inputs, sampling,
batches, thread controls, and numerical-oracle policy. The sole published
operator CSV and its 120 summaries contain 11,604 rows across five
configurations. Appendix A and Section 4.5 are synchronized in MD and TeX;
the operator figure awaits regeneration after native timing finishes.

The current model merge still includes a Joggle run from `38a426d`; its
integrated-main replacement is running. Revalidate the native packages after
that run, before claiming complete main-branch data alignment. Keep external
model results only where their recorded inputs, sampling protocol, and
compiler configurations match the completed new run.

The MD and TeX design sources now distinguish observation-based stage selection
within a retained graph from prepared-body reuse across imported graphs.
Rebuild and inspect the PDFs after the live timing collection; the current PDF
predates this mechanism clarification.
The subsequent source revision also adds the measured host configuration to
the methodology and native invocation/output-storage boundaries to Table A.4.
Check both the main paper and supplement when rebuilding.

The current end-to-end exports were recomputed from individual timing rows:
11,604 operator rows and 4,911 model rows. All exported medians, p95 values,
correctness flags, and the text's geometric-mean ratios agree. This validates
the existing export, not a new-main measurement. The update protocol now names
the actual operator substitutions and identifies the edited graph as the oracle
subject.

After the timing collection, correct the stale overview in `artifact/PROTOCOL.md`:
its Figure 5 summary still says 12 tasks / 36 patch rows, although the amended
package section and collected data use eight conditions / 24 package rows.
The execution study uses five configurations for operators and four for
models, not five for both. Do not edit `artifact/` while the live collection is
checking source stability.
