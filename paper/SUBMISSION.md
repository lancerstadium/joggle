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

The main PDF has 14 pages. Technical content ends on page 12; pages 13--14 contain
references only. The separate supplement has fifteen pages. Table A.5's 12 paired
input/output examples occupy six portrait pages, with verbatim quoted task
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
The related-work table places eleven systems/frameworks in one horizontal band,
without interspersed category-title rows. Blue checkmarks mark supported
capabilities, and a tinted column identifies Joggle. Table 1 now separates
positioning, extension form, and native mechanism capabilities; measured package
costs remain in the evaluation rather than this literature comparison. Figure 6 uses four
independent protocol vignettes, distinct from the author's 3v3 overview. The main PDF
now cites 47 sources. The literature expansion covers IRDL, LMS, AnyDSL,
Exo, Ansor, TASO, Mirage, PluS, egglog, self-adjusting computation, IncA,
SWE-agent, Delite, Forge, Stratego, Lift, RISE/ELEVATE, guided equality
saturation, MetaSchedule, Relax, Tensor Comprehensions, TACO, Shake, pluto,
PIE, differential dataflow, SWE-bench, and Glow.
Citations occur in the introduction, motivation, and
related-work comparisons; every bibliography entry is cited. KernelBench,
TensorIR, and Exo 2 now use formal proceedings records rather than preprint
metadata. Primary papers, project documentation, institutional records, or
official proceedings were opened during the reference check; source locators
for the additions are recorded in `references.bib` comments. Final author
review remains part of submission approval.
The Agent study uses Qwen3-8B and Qwen3-14B for the 72-condition matrix.
The primary collection is stored in
`.cache/artifact/main-agents-explicit-20260925-x0f9ieig/`, frozen at `1d096c7`
with action protocol `explicit-json-actions/v2`. Its manifest fixes all 72
conditions and the original shuffled order. All 36 native references pass
isolation checks; their task, source, and oracle records were revalidated
before starting. Native performance timing has finished, so Agent tests do
not overlap those measurements. Complete the matched population and displays
before finalizing the evaluation; main result fields are still unfilled.
After assembling `data/figure-04-extension.csv` and its provenance record,
`make -C paper agent-figure` audits the complete matrix and renders the shared
2-by-3 bar layout. Add the measured figure and result analysis to Section 4.2
only after the population is complete. The first six conditions are complete;
condition seven stopped on a read timeout before receiving its first response.
The author approved a uniform transport-continuation rule: retain the timeout
archive and resend only the unanswered request with unchanged remaining budget.
The runner and trajectory validator implement this transport-only amendment;
task/API sources and native binaries remain frozen.
The transport amendment is committed as `dcb383e`. Condition seven exhausted
the three permitted continuations without a received answer; collection stopped
with six completed conditions. The interrupted condition is an infrastructure
failure, not a scored model failure. No partial primary result is published.
No effective response or completed condition has been rerun.

## First review: unresolved submission gates

### Current presentation revision

Figures 3, 4, and 5 were regenerated with the built-in image tool in a shared
portrait, single-column style. Their complete prompts and targeted corrections
are stored beside the figure descriptions in `README.md`. Figure 4 embeds
complete input/output subject functions and the actual fusion replacement
excerpt. Appendix C.7 supplies the full transformation helper and entry point.
Figures 3 and 5 now include graph API syntax and named state records.
Algorithm 1 now validates and executes stages in order inside one transaction.
Figure 5 depicts actual writes, stage-local observations, and retained records.
The implementation removes historical-footprint preselection and final global
rebasing. Regression coverage includes empty-to-nonempty writes, switched output
functions, disjoint reuse, rollback, and late writes to earlier observations.
The original reproducer now returns incremental=7, repeated=7, full=7; all nine
unit suites pass against the rebuilt main library (core revision `8735b0f`).
The broader non-model-zoo, non-install regression selection also passed all
45 tests before the final report-only selection-cost refinement; the nine
unit suites were rerun afterward. Frozen Agent binaries remain
unchanged. Existing paper timing CSVs still identify their original measured
revision; these correctness checks do not replace timing measurements.

Appendix C.5 reproduces the printed GELU subject from the frozen compiler;
C.6 reproduces one unmodified Agent candidate and its actual diagnostic.
These two kinds of output are explicitly distinguished. Existing native oracles
were rerun, without new measurement scripts: GELU 7/7, interval analysis 10/10,
and fused convolution 6/6 fixtures passed. Records are
`.cache/artifact/paper-code-check-{gelu,range,fusion}.json`; the unit-kernel
input/output figure was checked against `paper-code-fusion-unit.json`.

### Substantive gates

Three role-separated reviews inspected the same frozen revision `e721b39`.
The following issues remain substantive submission gates, not layout work:

- **Mechanism evidence.** The production update study measures fresh imports
  with prepared-body reuse; it does not invoke `ReactiveSchedule`. Its measured
  speedups remain attributable to prepared-body reuse. The retained-store
  scheduler needs a direct full-versus-reactive comparison using the repaired scheduler. Keep these two reuse mechanisms distinct in the claims,
  figure labels, and results.
- **Agent results.** Finish the full 72-condition population and report all
  outcomes. Successful-task costs use conditional populations; disclose their
  denominators rather than interpreting them as paired costs on identical tasks.
- **Final synchronization.** After collection, reconcile the stale package and
  update status paragraphs in `artifact/PROTOCOL.md`, and match Figure 1's
  endpoint labels to the reported measurements. Do not silently edit the
  author-owned Figure 1.

The paper now accounts for input verification and verification after every
executed stage, and states the native-C versus Python-driven invocation
boundaries in the main execution section. Table 1 distinguishes composition
units from the measured native feature packages. These edits address reporting
issues; the remaining experimental work is listed above.

Four preliminary trajectories are retained separately. The author approved
shared JSON action templates and field-specific errors before recollecting
the entire matrix with unchanged tasks, models, budgets, and scoring.
The primary assembler excludes the earlier protocol. Appendix C summarizes
the method; `artifact/PROTOCOL.md` and batch manifests retain collection details.

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
PDFs were regenerated on 25 September after native execution timing finished.
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
configurations. Appendix A, Section 4.5, and the operator figure are synchronized.

The model merge now uses the completed integrated-main run in
`.cache/artifact/main-execution-20260924-81YqJu/`. Its source is `cc82ef114093`,
matching both operator variants; identity checks are stable and release eligible.
Fourteen models pass; SSD-MobileNetV1 reaches the unchanged 600-second
preparation timeout. All three external records pass the matched protocol and
input checks. The 4,911-row export, 60 summaries, Figure 10, Appendix B, and
the main text now agree: 2.03× geometric-mean speedup over default TVM on eight
jointly correct models. The aggregate panel labels this set as “8 common.”

The subsequent native-package rerun in
`.cache/artifact/main-packages-20260924-a98ifw/` validates all 24 changed
packages and 18 paired parent controls with the integrated compiler. Its CSV
is byte-identical to the previous footprints; the published provenance now
points to the new validation records. All timing and package queues have ended.

The MD and TeX design sources now distinguish observation-based stage selection
within a retained graph from prepared-body reuse across imported graphs.
Both PDFs include this mechanism clarification, the measured host configuration,
and the native invocation/output-storage boundaries in Table A.4. Repeated
method and discussion text was condensed to keep technical content within
12 pages. The current main PDF has 14 pages, with references beginning on
page 13; the supplement has 15 portrait pages. The latest build has no
overfull boxes or undefined references. Main pages 8 and 12 and supplement
page 3 were rendered and visually checked after the prose edits.

Table 1 compares eleven systems in one horizontal header. Its binary rows
concern shared compiler-evaluated role calls, cross-stage packages, read-tracked
reuse, and transactional IR edits. Cross-stage host-language packages receive
credit; using one host language alone does not establish one compiler call/value
model. Rollback scopes distinguish conversion, alternatives, and sequences.
The added
egglog and PIE columns use the existing primary-paper citations; dependency
tracking was cross-checked against the rustc guide and Adapton description.
Appendix C.3 now reports the actual 12-task fixture split (22 public, 82
held-out), replacing the layout-definition task outside this collection.
All twelve quoted contracts match `artifact/manifests/extension-specs.json`;
the C.4 diagrams are unchanged. Main page 12 and supplement page 9 were
rendered after these table revisions; neither table overlaps adjacent content.

The Agent protocol table records request/context limits, condition order,
and the wall-time boundary in Appendix C. Provider and operational details
remain in the experimental records rather than manuscript prose.
The new model-level update medians also correct the old fastest-system sentence:
TVM has the lowest median on DenseNet and SqueezeNet, ONNX-MLIR on TinyYOLOv3.

The current end-to-end exports were recomputed from individual timing rows:
11,604 operator rows and 4,911 model rows. All exported medians, p95 values,
correctness flags, and the text's geometric-mean ratios agree. This validates
the integrated-main exports. The update protocol names
the actual operator substitutions and identifies the edited graph as the oracle
subject.

The overview in `artifact/PROTOCOL.md` now matches the eight package conditions,
24 changed packages, and 18 parent controls. It also distinguishes five operator
configurations from four model configurations (18,000 possible valid timing
rows before unsupported or incorrect outcomes).
