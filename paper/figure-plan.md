# Figure evidence plan

The manuscript will not use a pass/partial checklist as a main result. Main
tables and figures follow the two visual patterns supplied by the project owner:
compact cell-shaded LaTeX tables for high-dimensional results, and aligned
small multiples with one shared legend for model-level measurements. The visual
language is a style reference only; all values, labels, groupings, and captions
must be generated from Joggle's preserved records.

## Figure 1 — Why a vertical experiment fragments

**Claim.** A bounded co-design change spans source semantics, computation,
schedule/layout, storage, and artifact interfaces, but existing systems expose
these decisions through several extension surfaces.

This is the Motivation and infrastructure-comparison figure. Its top strip
follows one running change (a low-precision fused projection mapped to an
external edge instruction) through the five decisions. Below it, aligned
grouped-bar panels report authored source files, authored source lines, native
registrations, build files, new dependencies, and generated artifacts for the
four frozen tasks. Joggle, ONNX-MLIR, and TVM retain one shared legend and task
order. A system that stops at a mandatory boundary receives a hatched
``unsupported'' marker at that task, not a zero-height bar. No panel combines
these dimensions into an ease score, and the old “pass, N lines” table is not
part of the manuscript.

## Appendix coverage record — Workload compilation frontier

**Claim.** The progressive representation is exercised by distinct model
families, and failures identify specific semantic capabilities rather than a
single opaque compatibility percentage.

This evidence is not encoded with the supplied speedup-table visual grammar.
The main text states the completed corpus and named unsupported frontiers; the
artifact retains per-model residual counts. A compact appendix table may list
those raw counts if space permits, without ratio shading or pass/partial cells.

Required additions before drawing: one pinned XCiT/ViT-class model and one
compact transformer language model, alongside the existing official ONNX Zoo
CNN/detection suite and TFLite MobileNetV2. GPT-2 may be retained as a heavy
frontier probe, but it does not replace a compact edge-relevant language model.

## Main LaTeX table — Operator and shape speedups

**Claim.** Target-aware policies change performance only in identifiable
operator/shape regions; no single optimization should be advertised from one
MobileNet aggregate.

This is a native two-column LaTeX table following the supplied dense-table
example. Every shaded cell has exactly one meaning: baseline median latency
divided by Joggle median latency, so values above 1.0 are Joggle speedups and
values below 1.0 are slowdowns. Rows are inference computations such as Add,
ReLU, SiLU, LayerNorm, Softmax, MatMul, convolution, and depthwise convolution.
The horizontal axis is deliberately broad rather than a handful of examples.
The exact grids are frozen in `operator-study.md`: 25 `M×N` cases, 27
`M×K×N` cases, 24 spatial/depthwise cases, and 20 pointwise-convolution cases.
Shape families use separate tables when their arity differs; they are not
forced into one misleading common axis.

Row sections identify the baseline (one-thread ONNX Runtime, TVM-generated C,
and LiteRT only for a matched TFLite path). All systems consume the same input
buffers and semantic instance, run on the same isolated Linux host, and use the
same warm-up and trial policy. A dash means the baseline cannot synthesize that
case. Cell shading encodes the speedup ratio itself, not support, stage state,
or an unrelated count. A geometric mean is printed only for a semantically
coherent row section. Until those matched measurements exist, the manuscript
does not render or populate this table.

## Figure 2 — Artifact quality across model families

**Claim.** Malleability has measurable compile-time, artifact-size, workspace,
correctness, and latency consequences across workloads.

Use a two-row aligned small-multiple layout with one shared legend, following
the supplied multi-panel example. Columns preserve the same model order and
panels have complementary roles rather than repeating one metric:

- compilation and transformation time;
- generated source/binary/weight size;
- planned peak workspace;
- inference latency with dispersion;
- numerical error or task metric;
- optional policy delta paired with its unchanged runtime control.

The same model order and system colors are preserved across panels. Latency is
never pooled across different serialized models or different machines. Shared
GitHub runners remain diagnostic and are visually separated from controlled
host data.

## Figure 3 — Where automation attaches

**Claim.** Joggle is an experimental control plane on which manual rules,
schedule languages, learned search, or synthesis can choose the same exposed
decisions without becoming mandatory core architecture.

This design figure positions Lift/RISE and TileLang/Exo on the explicit-control
side, Ansor/TVM search in the schedule-search space, and Mirage/Axon in
multi-level superoptimization/synthesis. Arrows terminate at ordinary module
functions over represented semantics, loops, storage, and artifacts. The figure
must not imply these external systems are implemented inside Joggle; it defines
an integration opportunity and the boundary evaluated by the paper.

## Data and rendering rules

- No mock values enter a paper figure.
- Every numeric panel is generated from a checked-in CSV/JSON record and records
  the revision, host, model hash, command, warm-up, repetitions, and numerical
  tolerance.
- A dash means unsupported; blank means not run; zero is printed only when zero
  was measured. These states never share a color.
- Heatmap colors encode a declared numeric interval, not categorical preference.
- Multi-panel plots use one legend, identical model order, aligned axes, and
  readable text at final two-column size.
- Negative results, including slower fused code or an unexposed modern-model
  frontier, remain in the main evidence when they bound the claim.
