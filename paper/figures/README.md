# Paper figures

Figures in this directory are generated from committed raw records. Do not edit
rendered files or derived summaries by hand.

## Figure 1 contract

- Core conclusion: one operator-independent loop-order policy changes real
  function bodies across three conventional models and improves every paired
  technical call with negligible generated-source growth.
- Results-level question: can an out-of-core structural policy produce a useful
  artifact change without operator cases or code replication?
- Archetype: two-panel quantitative grid.
- Output: EuroSys double-column figure, 178 mm wide, Python backend.
- Panel a: all paired latency ratios, with medians and interquartile ranges.
- Panel b: exact generated C growth and number of changed loop bodies.
- Hero evidence: paired latency direction across the three models.
- Boundary evidence: generated-source growth.
- Statistics: descriptive only. The 20 pairs per model are technical calls in
  one unisolated process, not independent experimental units.
- Reviewer risk: these pilots do not establish isolated cross-machine speedup
  or close the gap to a production runtime.

Regenerate with the panel-alignment helper on `PYTHONPATH`:

```sh
PYTHONPATH=/path/to/nature-figure/scripts python3 paper/figures/reorder_tradeoff.py
```

The script requires matplotlib and NumPy. It validates row counts, source
hashes, exact paired-output agreement, and the presence of an actual loop-body
change before plotting. `qa/` contains the blocking panel-alignment and
rendered collision audits. The editable SVG is the primary artifact; PDF, PNG,
and TIFF exports are derived from the same figure object.
