# Recorded model latency

## Purpose / 论证目的

Figure 5 connects executable artifacts to observed runtime cost. It shows a
production-runtime gap, a repeatable direction for the loop-policy change,
and a fusion regression. It does not support a competitive-speed claim or a
current-revision benchmark. 图的任务是解释完整产物的性能边界，不把内部优化幅度
包装成对外领先，也不把不同机器上的历史结果拼成统一排名。

## Sources and reproduction

- Script: `paper/scripts/render_models.py`.
- Outputs: `models.pdf` (7 by 3.5 inches, embedded 10-point serif labels),
  `models.png` (300-dpi preview), `models.json` (computed values and input hashes).
- Dependencies used: matplotlib 3.11.2 and numpy 2.5.3. The generic paper-figures
  style helper was applied during preparation; the script retains explicit
  local settings so a skill installation is not a reproduction dependency.
- Reproduce from the repository root: `python paper/scripts/render_models.py`.
- C-model-latency / E-model-trials: the nine CSV/JSON pairs listed in
  `models.json`, under `paper/data`. All have 20 observations per system and
  a clean recorded revision. The generator checks finite positive timings,
  nonempty validation records, and exactly one observation per trial/system.
  This is input admission, not a new numerical-oracle run.

Each small mark is a recorded per-process latency in milliseconds. Large marks
are independently computed medians. Small deterministic vertical offsets
separate trial marks without altering latency. Overlapping values remain
overlapping; no fabricated noise is added to timings. These are repeated
process observations on shared Linux runners, not independent hardware samples.
No confidence interval, significance test, or cross-job geometric mean is used.

Panel (a) uses `linux-replication/mnist`,
`linux-replication/mobilenetv2`, and `tflite-linux/tflite-mobilenetv2`.
Panel (b) uses `mobilenetv2-policy/{plain,canon}`, the corresponding
`replication/{plain,canon}`, and `mobilenetv2-fusion-linux-{unfused,fused}-pilot`.
Both policy cohorts report AMD EPYC 9V74 hardware, but use different compiler
revisions and separately recorded runs. Panel (a)'s records report Intel
Xeon hardware. All system comparisons are within a row. A/B and fusion are
separate cohorts, not data points in one time series. The TFLite artifact is
also not the ONNX artifact under another runtime.

## Caption and placement

Native caption and alt text are in `submission/main.tex`, `fig:models`, in
“Complete programs and generated code.” The surrounding text explains the
loop-policy consequence and the fusion counterexample. The graph uses points
rather than bars because the latency range spans orders of magnitude; bars on
a logarithmic axis would make bar length a poor comparison. Both axes explicitly
state the logarithmic scale, and marker shapes redundantly identify systems.

## Review and limits

The figure was regenerated from all nine records, and medians were checked
against the existing prose. The added policy replication is explicitly named,
not silently substituted for cohort A. Initial clipped labels were corrected;
the final canvas has fixed dimensions so LaTeX does not unexpectedly shrink
a tight bounding box. Data and measured code were not modified or rerun.

The current picture is narrower than the intended evaluation: two architectures,
two frontends, and several MobileNetV2 variants, not a many-model matched suite.
There is no comparable full-model TVM/ONNX-MLIR latency series in this figure.
Compilation time, compiler RSS, runtime workspace, and complete deployment
footprint still require separately admitted measurements. The source JSONs
contain some artifact sizes, but these are not interchangeable with the missing
cost metrics. Author verification of data, interpretation, and final submission
format remains pending.

## Abstract evidence selection

The abstract uses C-policy-latency / E-model-trials for the two policy pairs,
not for an external-runtime speedup. Recomputed directly from each pair's
20 Joggle process observations:

| Cohort | Base median (ms) | Policy median (ms) | Reduction |
| --- | ---: | ---: | ---: |
| A, revision `418a34e5` | 139.679364 | 102.637659 | 26.5191% |
| B, revision `b79c2255` | 178.795807 | 131.557411 | 26.4203% |

Reduction is `100 * (1 - policy / base)`. The abstract rounds the observed
range to 26.4% to 26.5%; it is not a confidence interval or an average across
models. These pairs compare a policy-enabled artifact with its corresponding
unmodified Joggle C path, not with ONNX Runtime. The negative runtime gap
therefore remains explicit. No new timing run was performed for this edit.
The abstract now names the optimization categories rather than the individual
models and explicitly calls these two case studies. Removing model names does
not broaden either result to the coverage suite; the mappings to UltraFace
storage and MobileNetV2 latency remain unchanged in Evaluation.
