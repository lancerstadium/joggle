# EuroSys 2027 submission checklist

Author-side checklist; not manuscript content. One manuscript entry point:
`make -C paper`. Revise `README.md` first, then synchronize TeX.

## Deliverables

| Upload | File | Current pagination |
| --- | --- | --- |
| Main paper | `sigconf.pdf` | 12 technical pages; references on pages 13–14 |
| Supplement | `supplement.pdf` | 16 portrait pages; separate upload |
| Figure data | `data/` | One canonical CSV per reported dataset |

The author-provided ACM template is retained. Main and supplementary files
are anonymous and numbered. Figures 1 and 2 remain author-owned and unchanged.
Do not concatenate the supplement with the main paper.

## Reporting map

| Section | Evidence | Interpretation |
| --- | --- | --- |
| 4.2 | 12 tasks × three checked native references | Source size and cross-role composition |
| 4.3 | 24 changed packages; 18 maintenance-parent controls | Initial integration and subsequent change footprint |
| 4.4 | 540 update/rebuild rows; nine sites in three models | Executable-ready turnaround and prepared-body reuse |
| 4.4 / Appendix D | 900 retained-graph pairs; 45 sites in 15 models | Dependency-directed stage processing |
| 4.5 | 11,604 operator rows; 4,911 model rows | Correctness coverage and steady-state latency |
| Appendix C | 42 complete Agent trajectories; 30 incomplete conditions | Observed candidates, diagnostics, and collection status |

Native reference size is a descriptive analysis added after inspection of the
Agent collection. It counts nonempty physical lines including imports and
comments, excluding shared drivers/build files. UTF-8 bytes accompany the counts.
All 36 source files match their passing admission reports. Reproduce with
`python3 paper/render_extensions.py --collect
.cache/artifact/main-agents-explicit-20260925-x0f9ieig/manifest.json`.
The default invocation validates the exported counts and renders Figure 7.

The Agent study has zero full-task completions among its 42 complete records.
The remaining conditions are 25 unstarted, four interrupted prefixes, and one
blocked trajectory. They are not scored failures. The batch is stopped; original
records and the planned completion criterion remain unchanged. Checked reference
programs are never presented as generated Agent output. No Agent advantage is claimed.

## Evidence and source alignment

- Executable-ready and integrated execution measurements identify core revision
  `cc82ef114093b6d90ca94df05b54a60084585e72`. Prepared-body reuse yields
  1.46–2.49× paired speedup. This is distinct from retained-graph scheduling.
- The scheduler correctness repair is identified by `8735b0f`; its direct study
  has separately recorded source identity in `data/reactive-scheduler.json`.
  Affected-edit stage-processing speedup is 18.86× across 450 pairs.
- Eight jointly correct models yield 2.03× geometric-mean execution speedup
  over default TVM. Absolute cross-system update results remain in the main text.
- Native package integration uses one Joggle file versus three per comparator.
  Maintenance uses one file and one ownership zone in every system.
- This final pass changes reporting, plotting, and figure assets, not compiler
  code or measured execution records. It does not relabel older binaries as
  a newly measured current build.
- Original records remain at their recorded `.cache/artifact/` locations;
  they were not moved or deleted during convergence. Git tracks source,
  contracts, exports, and renderers. These local raw records still need inclusion
  in any separately distributed reproduction bundle.

## Review passes — 25 September

1. **Evidence:** verified all 36 reference identities, literal source counts,
   complete task/system coverage, and admission outcomes. Distinguished source
   size from development effort and from the incomplete Agent endpoint.
2. **Argument:** revised Section 4.2 around concrete definitions/composition;
   retained shorter comparator implementations; removed repeated methods;
   corrected the introduction's old stage-preselection description. Synchronized
   MD/TeX figure references and the protocol's current reporting map.
3. **Layout:** rebuilt both PDFs; inspected the new figure/workflow page,
   related-work/discussion/conclusion page, and appendix source-size table.
   Recovered 12 technical pages by shortening prose, without reducing body type.
   No overfull boxes or undefined references in the build logs.

Figure 6 was corrected after visual inspection rejected a generated draft with
wrong comparator and sample-count labels. The accepted version names TVM,
24 operators / 15 models, and 10 warm-ups / 100 samples. Its prompt is recorded
at Figure 6 in `README.md`. Figure 7 is code-rendered, with six boxed panels,
one-row legend, shared zero-origin scale, and no statistical error bars for
literal source counts.

## Before upload

Official source: [EuroSys 2027 CFP](https://2027.eurosys.org/cfp.html),
checked 25 September 2026. Fall full-paper deadline is 24 September AoE,
equivalent to 25 September 19:59:59 in Asia/Shanghai.

- Confirm all authors, anonymous submission links, and required submission-form
  disclosures, including actual AI assistance with prose, code, and figures.
- Upload the main PDF and supplementary PDF separately.
- The requested dense author-review layout uses 5.5 pt plot text and smaller
  table/caption text. The CFP explicitly requires at least 10 pt text including
  figures/captions and 12 pt leading. This remains an unresolved format issue;
  pagination alone does not establish submission compliance.
- Reference-size comparisons are implementation-specific; the Agent collection
  does not establish a productivity benefit. Retain the current scoped claims.
- Final author approval and submission upload have not been performed.
