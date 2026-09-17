# Measured runtime

## Purpose / 论证目的

This figure carries the abstract's headline range and the cross-system boundary
in one display. A group is a model; the bars inside a group are the systems that
were actually run on that model, so the ten-model campaign and the five-system
comparison share one form instead of appearing as separate panels. 图的任务是把
"一条源定义策略无需重编编译器即可作用于全部模型"与"与其它系统的差距"放在同一
张标准分组柱状图里；不把单模型比较单列成面板，也不把没有运行记录的模型画成零高
柱。

## Sources and reproduction

- Script: `paper/scripts/render_runtime.py`.
- Outputs: `runtime.pdf` (7 by 3.15 inches, matplotlib default sans-serif),
  `runtime.png` (300-dpi preview), `runtime.json` (medians, deviations, ratio
  range, and input hashes).
- Reproduce from the repository root: `python3 paper/scripts/render_runtime.py`.
- Input: `paper/data/locality-matrix.{csv,json}`, 960 rows: ten models x 20
  trials x the systems that could be built and validated for each, which is the
  two Joggle artifacts plus ONNX Runtime everywhere, TVM on nine models, and
  ONNX-MLIR on nine. All five columns come from one job, so they share one set
  of conditions. The earlier two-variant record of the same name is superseded
  and remains in the repository history.
- The generator refuses a record whose trial count is not 20 or whose thread
  count is not 1, rejects an observation that is non-finite, non-positive, or not
  marked `pass` with a zero failure count, discards a repetition whose output
  digest departs from the first run of the same variant, and requires each
  recorded `speedup` to equal a median ratio recomputed from the raw rows. These
  are run-admission checks, not reported metrics.

## Recorded values

Each bar is a median over 20 fresh processes, normalized to the same model's
first-fit median; each error bar is that record's `mad_ms` divided by the same
reference. Paired-trial speedup over first-fit, by model: TinyYOLOv2 1.365,
UltraFace RFB-320 1.490, ResNet18 1.765, SqueezeNet 1.1 1.919, SqueezeNet 1.0
QDQ 1.972, DenseNet 2.112, GoogLeNet 2.307, MobileNetV2 2.491, ShuffleNetV2
2.485, MNIST 3.238. Campaign minimum 1.365, median 2.042, maximum 3.238.
Ordinary run time spans 1.65 ms (MNIST) to 2312.42 ms (TinyYOLOv2). The job ran
at load average 3.56 to 3.61, so absolute times are not comparable with the
earlier pair-only job; the paired within-trial rotation is what makes the ratios
readable across that difference.

Cross-system position, over the models where the external tool was measured.
TVM: the policy artifact is faster on all nine, by 1.06x to 2.85x. ONNX-MLIR
`-O3`: faster than the policy artifact on six models, by up to 3.83x, and slower
on three (ResNet18 1.27x, TinyYOLOv2 1.15x, GoogLeNet 1.08x). ONNX Runtime:
ahead on every model, by 7.7x to 85.9x. SqueezeNet 1.0 QDQ carries three bars
because TVM fails its stored reference on that model and the pinned ONNX-MLIR
aborts on it, so neither has a timing.

## Caption and placement

Native caption and alt text are in `submission/main.tex`, `fig:runtime`, in
"Cost and performance boundary." The caption states what a bar, an error bar,
and a group each are, so the display is read from the figure rather than from
prose.

## Review and limits

Both input records are single-host, single-thread jobs with setup excluded, on
the same macOS arm64 host (Apple M4). They are nevertheless **two separate jobs
recorded about twelve hours apart** with different ambient load (load average
1.57 to 2.63 for the campaign, 2.20 to 2.35 for the five-system record), so the
only value comparable across them is the policy-improved Joggle artifact, which
both contain. Error bars are median absolute deviations, not confidence
intervals, and no significance test was applied.

The pinned set holds sixteen models; this figure plots the ten with a run
record. The other six closed their conversion frontier without a recorded
executed artifact and therefore have no runtime to plot; the caption states
this, and no row is drawn as a zero bar. UltraFace RFB-320 is a single model and
its five-bar group cannot be read as a suite-level system ranking. The TVM bar
is its default C-target lowering, unscheduled and without LLVM, and the
ONNX-MLIR bar is its `-O3` route although its own default is `-O0`. Author
verification of data, interpretation, and final submission format remains
pending.
