# Figure evidence plan

The manuscript will not use a pass/partial checklist as a main result. Main
tables and figures follow the two visual patterns supplied by the project owner:
compact cell-shaded LaTeX tables for high-dimensional results, and aligned
small multiples with one shared legend for model-level measurements. The visual
language is a style reference only; all values, labels, groupings, and captions
must be generated from Joggle's preserved records.

## Figure 1 — One vertical experiment, several ownership boundaries

**Claim.** A bounded co-design change spans source semantics, computation,
schedule/layout, storage, and artifact interfaces, but existing systems expose
these decisions through several extension surfaces.

This Motivation figure is primarily a wide left-to-right ownership map, not a
bar chart. It follows one running change---a low-precision fused projection
mapped to an external edge instruction---through source relation, numeric
representation, tensor body, schedule/layout, storage, and artifact ABI. One
row per system marks where that intent must cross a different representation,
registration mechanism, or runtime boundary. Only facts demonstrated by the
matched task artifacts are drawn. The visual establishes *why* the experiment
is vertical before any source counts appear.

The current three-panel source-count plot moves out of Motivation. If retained,
it becomes a secondary extension-cost figure with a denser 2-by-2 layout:
authored files and lines on the first row, native/build obligations and emitted
artifacts on the second. Joggle, ONNX-MLIR, and TVM retain one legend and task
order. A stopped task receives an explicit unsupported marker, not a zero. No
panel combines dimensions into an ease score.

## Appendix coverage record — Workload compilation frontier

**Claim.** The progressive representation is exercised by distinct model
families, and failures identify specific semantic capabilities rather than a
single opaque compatibility percentage.

This evidence is not encoded with the supplied speedup-table visual grammar.
The main text states the completed corpus and named unsupported frontiers; the
artifact retains per-model residual counts. A compact appendix table may list
those raw counts if space permits, without ratio shading or pass/partial cells.

The pinned XCiT-Tiny model now has a zero type-inference frontier and a seven-
call conversion frontier in positional embedding. The remaining required
addition is one compact transformer language model, alongside the existing
official ONNX Zoo CNN/detection suite and TFLite MobileNetV2. GPT-2 may be
retained as a heavy frontier probe, but it does not replace a compact
edge-relevant language model.

## Tables 2--4 — Operator and shape speedups

**Claim.** Target-aware policies change performance only in identifiable
operator/shape regions; no single optimization should be advertised from one
MobileNet aggregate.

Every shaded cell has exactly one meaning: baseline median latency divided by
Joggle median latency, so values above 1.0 are Joggle speedups and values below
1.0 are slowdowns. Table 2 has eight executable rows (Add, Multiply, ReLU,
SiLU, Softmax, ReduceMean, RMSNorm, LayerNorm) across 25 `M×N` columns. Table 3
contains contractions and fused transformer subgraphs across 27 `M×K×N`
columns. Table 4 contains standard, depthwise, and pointwise convolution under
an explicit spatial/channel header. Each is a full-width native LaTeX table
with rotated headers and a geometric-mean column, matching the supplied dense
table grammar. Different arities are never forced onto one ambiguous axis.

Row sections identify the baseline (one-thread ONNX Runtime, TVM-generated C,
and LiteRT only for a matched TFLite path). All systems consume the same input
buffers and semantic instance, run on the same isolated Linux host, and use the
same warm-up and trial policy. A dash means the baseline cannot synthesize that
case. Cell shading encodes the speedup ratio itself, not support, stage state,
or an unrelated count. A geometric mean is printed only for a semantically
coherent row section. Until those matched measurements exist, the manuscript
does not render or populate this table.

## Figure 2 — End-to-end model outcomes

**Claim.** Malleability has measurable compile-time, artifact-size, workspace,
correctness, and latency consequences across workloads.

Use a full-width two-row aligned small-multiple layout with one shared legend,
following the supplied grouped-panel example. Columns preserve the same model
order and panels have complementary roles rather than repeating one metric:

- top row: compile time, inference latency with dispersion, and peak workspace;
- bottom row: executable size, immutable-weight size, and task metric or
  numerical error.

The subjects are grouped by workload family: compact classifier, mobile CNN,
detection, ViT-class encoder, and compact transformer/attention. Systems are
Joggle portable C, Joggle target policy, one-thread ONNX Runtime, TVM, and
ONNX-MLIR wherever the same ONNX artifact and CPU contract are reproducible.
LiteRT appears only in a separate TFLite panel and is never pooled with a
converted ONNX model. This figure answers whole-model questions; operator-table
cells do not substitute for it.

The same model order and system colors are preserved across panels. Latency is
never pooled across different serialized models or different machines. Shared
GitHub runners remain diagnostic and are visually separated from controlled
host data.

## Table 5 — Design mechanism and boundary comparison

**Claim.** Joggle is an experimental control plane on which manual rules,
schedule languages, learned search, or synthesis can choose the same exposed
decisions without becoming mandatory core architecture.

Use a wide qualitative table rather than a decorative architecture figure.
Rows are Joggle, TVM/Relax+TensorIR, ONNX-MLIR, IREE, Exo, TileLang, and
Mirage/Axon. Columns are source-model relation, inspectable tensor body,
user-authored structural edit, automatic chooser attachment, custom numeric
type, storage edit, external target primitive, artifact interface, and explicit
unsupported frontier. Each cell contains a short mechanism name backed by a
primary source or a reproduced task; it is not a checkmark. This makes both
Joggle's advantage and its cost visible: a continuous edit surface and small
module boundary versus weaker production breadth, target automation, and tuned
kernels.

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
