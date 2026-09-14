# Paper figures

Only submission candidates rendered from frozen records belong here. The old
pass table and three-panel source-line chart were removed: both described
implementation inventory rather than testing a systems claim.

The planned evidence sequence is:

1. **Motivation figure:** one vertical extension before and after a hardware
   revision. The horizontal path is relation → representation → structure →
   target → artifact; vertical callouts show which contracts the revision
   invalidates. A lower strip contrasts the stable boundaries offered by
   MLIR/Relax, kernel DSLs, and ISA-derived backends. This is a conceptual vector
   figure, not a performance chart.
2. **Mechanism figure:** one Joggle function progressively exposed from a source
   call to an executable alternative, with portable and specialized paths
   coexisting. Modules and checked choice appear only where they affect the
   running example; there is no box for every implementation class.
3. **Evolution evidence:** an aligned change-path diagram plus a native LaTeX
   table for initial implementation and two withheld revisions. Cells report
   contracts touched, rebuild obligations, preserved paths, diagnostics, and
   artifact correctness; no composite “ease” score is permitted.
4. **Operator evidence:** full-width native LaTeX speedup matrices, one baseline
   section at a time, with rotated shape headers, exact values, restrained
   shading, explicit unsupported cells, and a geometric mean. These reproduce
   the information density of the supplied Axon table without copying its data
   or turning the matrix into a raster heatmap.
5. **Whole-model evidence:** four aligned horizontal panels sharing model order:
   latency, peak memory/workspace, compile/load cost, and artifact footprint.
   Absolute values and task accuracy live in a compact adjacent table. CNN,
   detection, ViT, and compact attention/LM workloads appear only after their
   complete paths execute.
6. **Ablation/failure evidence:** small multiples for progressive exposure,
   target policy components, chooser substitution, and rejected/rolled-back
   candidates. This panel explains causality; it does not compare Joggle with
   itself as if those variants were external baselines.

No placeholder chart is checked in before its complete record set exists.
Figures use the venue's two-column geometry and native vector/PDF or LaTeX
output. The user-supplied visual examples define the desired density and shared
alignment; they do not authorize synthetic values.
