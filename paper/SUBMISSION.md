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

The main PDF has 13 pages. Technical content ends on page 12; page 13 contains
references only. The separate supplement has ten pages. Table A.5's 12 paired
input/output examples occupy five portrait pages, with verbatim quoted task
contracts and 8.5 pt DOT labels. Section C.4 uses portrait tables while retaining its diagram typography
and display sizes. All supplement pages are portrait. Both entry points explicitly enable page numbers.

The three measured figures use the author's requested compact review layout:
single-column 3.33 × 2.25 in, two rows by three columns, 5.5 pt text, and
single-row legends. Data, estimators, and scales are unchanged. This figure
typography is below the CFP's explicit 10 pt requirement for figures and
captions; the compact review layout is not a completed submission-format check.
Architecture illustrations still need a printed-size audit. Some table bodies,
bibliography text, and ACM caption defaults are also below 10 pt. Resolve the
typography and recheck pagination before upload.
The two-column rule is explicit for the paper; the CFP gives no separate
layout specification for the optional supplement.

## Code and measurement alignment

The complete native-package comparison is collected in
`data/package-footprint.csv`, with hash-bound source records in the adjacent
JSON file. It contains eight conditions across three systems: two initial
integrations and six independent maintenance changes. All 24 changed packages
pass 201 semantic fixtures and 16,011 runtime probes; all 18 parent controls
fail the corresponding changed contract. Counts describe the observed
implementations, not hunk-minimized patches. The main text reports the
integration difference and the equal maintenance file/ownership counts.
The 72-condition Agent completion collection is still running and is not a
completed result. Add its results and associated displays before finalizing
the evaluation.

The repeated-update data in `data/figure-06-update.json` are pinned to revision
`1dce55b815f2f63d75b8b799119e8febc8956523`, including prepared-body reuse.
The current main-branch resident compiler does not yet contain that reuse path.
Integrate and verify the measured implementation before packaging the artifact;
do not relabel those measurements as results of the current main branch.
The external-data integration check at
`.cache/artifact/external-data-verified.qpfoeC/` is a separate single-repetition
correctness run, not a replacement for the paper's repeated measurements.
