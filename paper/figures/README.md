# Paper figures

The [research plan](../plan.md) owns figure selection and evidence order.
This directory holds manuscript assets, not an implementation inventory.

## Current figures

Two schematics that earlier drafts included are no longer in the manuscript:
the design overview (`overview.png`) and the system architecture
(`architecture.png`). Measurement settled the choice: the four full-width
schematics cost three pages of layout, and text plus captions alone come to ten
pages, so the paper could not fit the twelve-page limit with all four. Both were
also the most redundant with the prose, since the Introduction states the
obstacle and the two pressures, the metaprogramming section states the Mod S /
Mod K / subject separation, and the MLIR inset was a drawing rather than
evidence. Both assets are retained here with their records
([overview](overview.md), [architecture](architecture.md)) so the dropped drafts
can be restored if the page budget changes.

1. **Overview** (`overview.png`, `fig:overview`): six general panels in three
   paired columns contrast customization obstacles with corresponding facilities.
   Red crosses locate obstacles; green checks identify implemented capabilities,
   not measured superiority. A narrow central MobileNetV2 example illustrates
   mixed-state computation. Function editing, module composition, and exposure
   remain general in the surrounding panels. Dependency updating is dashed and
   planned without a green check.
   [Design, generation prompts, and production boundaries](overview.md).
2. **Architecture** (`architecture.png`, `fig:progressive`): an asymmetric
   comparison inset and main Joggle view separate selected module imports,
   editable compiler definitions, the subject program, and core services.
   The MLIR inset compares the documented Transform implementation boundary;
   it explicitly credits mixed abstraction levels. The module DAG is not a
   pass sequence. The old three-card trace drawing is removed, but its
   [measurement record](../experiments/progressive-trace.md) and manuscript
   observations remain. [Source mapping and prompts](architecture.md).
3. **Compiler-function derivation** (`derivation.png`, `fig:derive`): three
   panels show closure copying, the changed selection predicate with retained
   eligibility tests, and checked driver retargeting. A separate subject lane
   connects invocation to annotations and unchanged artifact consumers. Literal
   clone/insertion excerpts use native LaTeX below the drawing, not tiny raster
   code. This replaces the code-only figure without changing the overview or
   model trace. [Design, evidence, and generation record](derivation.md).
4. **Module replacement** (`binding.png`, `fig:binding`): corresponding
   installed and staged import graphs retain the old declarations, while the
   added facade overload redirects the client's resolved call. Distinct import
   and binding edges explain why exports alone do not justify replacement.
   This is a single-column close-up of the actual regression, not another
   installation overview. [Source mapping and generation record](binding.md).
5. **Measured runtime** (`runtime.pdf`, `fig:runtime`): one grouped bar chart.
   A group is a model and the bars inside it are the systems run on that model,
   drawn against the zero baseline of the model's own first-fit artifact, with
   error bars from the recorded median absolute deviation. It carries the
   abstract's headline range and the cross-system boundary in the same display,
   and UltraFace RFB-320 keeps the same form as every other group rather than a
   panel of its own. [Recorded values, limits, and reproduction](runtime.md).

**Recorded model latency** (`models.pdf`, `models.json`) is retained as a
generated asset but is no longer included in the manuscript: its cohorts are
older comparisons that the campaign record and `tab:samehost` supersede, and
`submission/main.tex` never referenced it. It stays here with its
[data and reproduction](models.md) note rather than being deleted, because the
nine records it draws remain part of the record set.

The old model node/count table has been removed from the manuscript. The
extension table now states integration routes and failure boundaries rather
than displaying source-line counts as an ease proxy. Raw records are retained.

## Next displays in evidence order

The following is a display plan, not a claim that the measurements exist.
The [research plan](../plan.md#evaluation-design-tied-to-the-argument) owns the
experiments; these choices specify how their evidence should be presented.

| Priority | Reader's question | Form and admission rule |
| --- | --- | --- |
| 1 | Are complete artifacts useful and costly? | Runtime is now drawn from nine preserved comparisons in Figure 5. Replace this historical-cohort view with aligned latency, compile-time, compiler-RSS, and artifact panels only after a matched suite exists. Do not fill missing dimensions with earlier unrelated measurements. Runtime workspace is not compiler RSS. Code and weights may be components of one deployed artifact; alternative formats are not additive. |
| 2 | What work does customization actually save? | A wide native-LaTeX cross-system revision table, with concrete edited boundaries, host rebuild requirements, artifact checks, and observed failure stage. Add cost plots only after matched observations; code volume is descriptive, not developer productivity. |
| 3 | Which shapes explain generated-code quality? | Native-LaTeX operator matrices from the existing [operator protocol](../studies/operator-study.md): one baseline/Joggle latency ratio per cell; separate pointwise, contraction, and convolution shape axes. Keep the complete shape grid and per-baseline denominators. No raster table or copied value across unmeasured columns. |

For grouped bars, use a zero baseline, readable units, and trial dispersion
from raw observations. Retain failures as explicitly labeled missing outcomes,
not zero-height bars. If dynamic range needs a logarithmic axis, label it and
prefer points/intervals rather than misleading truncated bars. A 1x reference
belongs on normalized comparisons; milliseconds and memory need separate axes.
One static-slot observation does not justify a broad memory-performance plot.
The existing 4.49% planner result therefore remains bounded numerical prose.
No plot of incremental-update speedup is admissible before that mechanism exists.

## Presentation rules

Conceptual drawings and measured results must remain distinguishable. User
references guide visual density and alignment, not values or system claims.
Keep tables in native LaTeX and produce quantitative plots from frozen records.
Use consistent typography, meaningful graphical structure, and legible labels
at final two-column width. Check both the standalone asset and the compiled
paper. Store only the selected generated asset here, not rejected variants.
