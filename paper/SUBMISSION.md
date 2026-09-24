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

The main PDF has 13 pages; technical content ends on page 12 and page 13
contains references only. The separate supplement has six pages. Both entry
points explicitly enable page numbers.

The compact authoring figures are not yet submission-size compliant: their
labels use fonts below 10 pt. Some table bodies, bibliography text, and ACM
caption defaults are also below 10 pt. Reflow these before upload; do not satisfy the page limit
by shrinking type. Recheck pagination after the font and figure layout pass.
The two-column rule is explicit for the paper; the CFP gives no separate
layout specification for the optional supplement.
