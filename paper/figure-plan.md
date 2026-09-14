# Figure evidence plan

The manuscript will not use a pass/partial checklist as a main result. Main
figures follow the two visual patterns supplied by the project owner: compact
cell-shaded matrices for high-dimensional operator results, and aligned small
multiples with one shared legend for model-level measurements. The visual
language is a style reference only; all values, labels, groupings, and captions
must be generated from Joggle's preserved records.

## Figure 1 — Why a vertical experiment fragments

**Claim.** A bounded co-design change spans source semantics, computation,
schedule/layout, storage, and artifact interfaces, but existing systems expose
these decisions through several extension surfaces.

This is the Motivation figure. Its left side follows one running change (a
low-precision fused projection mapped to an external edge instruction) through
the five decisions. Its right side is a measured extension-surface matrix for
Joggle, ONNX-MLIR, TVM, and one kernel/edge control. Cells contain the number of
distinct definitions or registrations touched, not subjective checkmarks.
Generated code, framework boilerplate, build dependencies, and unsupported
boundaries use separate encodings. The existing “pass, N lines” table moves to
the appendix once this matrix is complete.

## Figure 2 — Workload and operator frontier

**Claim.** The progressive representation is exercised by distinct model
families, and failures identify specific semantic capabilities rather than a
single opaque compatibility percentage.

Rows are grouped as CNN classification, detection, quantized CNN, vision
transformer, and language/attention. Columns form an ordered frontier:
decode, infer, relate to shared semantics, expose function bodies, emit,
compile, and execute. A cell contains a measured count: unknown results,
remaining source calls, unexposed calls, or zero at a completed stage. Color
encodes the normalized remaining fraction; completed cells are visually quiet,
and not-run cells are blank with a distinct hatch. This replaces the current
pass/partial table. A side strip lists model size and dominant operator family.

Required additions before drawing: one pinned XCiT/ViT-class model and one
compact transformer language model, alongside the existing official ONNX Zoo
CNN/detection suite and TFLite MobileNetV2. GPT-2 may be retained as a heavy
frontier probe, but it does not replace a compact edge-relevant language model.

## Figure 3 — Operator and shape envelope

**Claim.** Target-aware policies change performance only in identifiable
operator/shape regions; no single optimization should be advertised from one
MobileNet aggregate.

The layout follows the dense matrix example. Rows are computation families:
elementwise/broadcast, reductions, pooling, convolution/depthwise convolution,
matrix contraction, normalization, and attention subgraphs. Columns are grouped
by shape regime (small edge, channel-heavy CNN, sequence length, and projection
width). Separate row groups compare Joggle generated C against ONNX Runtime or
LiteRT, then against TVM-generated kernels where the semantics and host match.
ncnn is added only for the selected edge operator/model it natively represents.
Cells show latency ratio and restrained ratio shading; unsupported synthesis is
an explicit dash. The geometric mean appears only within a semantically coherent
row group.

## Figure 4 — Artifact quality across model families

**Claim.** Malleability has measurable compile-time, artifact-size, workspace,
correctness, and latency consequences across workloads.

Use aligned small multiples with one shared legend, following the supplied
multi-panel example. Panels have complementary roles rather than repeating one
metric:

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

## Figure 5 — Where automation attaches

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

