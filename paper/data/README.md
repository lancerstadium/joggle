# Pilot measurements

Files in this directory are auditable engineering observations, not final
paper measurements. They preserve negative results that determine the next
experiment instead of presenting them as controlled performance claims.

`mobilenetv2-fusion-pilot.csv` was recorded on 12 September 2026 on an Apple
M4 running Darwin 24.6.0 with Apple Clang 17.0.0 (`clang-1700.6.3.2`). Both
programs used `-std=c11 -O3`, the same external weight blob, three untimed
warm-up calls, and 30 timed calls in one process. The machine was not isolated,
frequency-controlled, or otherwise prepared for publication measurement.

The baseline is the official MobileNetV2 application artifact. The fused
variant applies `tile.fuse` and then `mem.plan` to the same prepared model.
Both use `examples/onnx/benchmark.c`; each CSV checksum is identical. Clang
loop-vectorization remarks showed interleave count four for the separate
BatchNorm and ReLU loops and one for most fused loops. Repeat this experiment
under the eventual frozen protocol before using latency in a paper claim.
