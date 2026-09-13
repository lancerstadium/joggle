# Paper figures

Figures in this directory are generated from committed raw records. Do not edit
the rendered files or derived summaries by hand.

Regenerate the current figure and its English hand-off report with:

```sh
python3 paper/figures/block_tradeoff.py
python3 paper/figures/figure_report.py
```

The figure script requires matplotlib, NumPy, and pandas. The report script
requires `python-docx`; this reporting dependency is not part of Joggle itself.

`Fig1` is a descriptive view of the three same-process block-policy pilots. It
shows all paired latency ratios and the exact generated-source cost. Repeated
calls within one process are technical observations, not independent
replicates, so the figure reports medians and interquartile ranges without a
significance test. It must not be presented as an ONNX Runtime comparison or a
publication-grade performance result.
