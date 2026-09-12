# Pilot measurements

Files in this directory are auditable engineering observations, not final
paper measurements. They preserve negative results that determine the next
experiment instead of presenting them as controlled performance claims.

`mobilenetv2-fusion-pilot.csv` was recorded on 12 September 2026 on an Apple
M4 running Darwin 24.6.0 with Apple Clang 17.0.0 (`clang-1700.6.3.2`). All
three programs used `-std=c11 -O3`, the same external weight blob, three untimed
warm-up calls, and 30 timed calls in one process. The machine was not isolated,
frequency-controlled, or otherwise prepared for publication measurement.

The baseline is the official MobileNetV2 application artifact. The `fused`
variant applies unrestricted greedy `tile.fuse`; `selected` instead invokes
the module-owned `cost.profitable` policy with `max_extent = 65536` and
`max_calls = 100`. Both transforms start from the same prepared model and run
`mem.plan` afterwards. All variants use `examples/onnx/benchmark.c`; each CSV
checksum is identical. Clang loop-vectorization remarks showed interleave count
four for the separate BatchNorm and ReLU loops and one for most fused loops.

The selected policy reduces loops from 374 to 357, local tensor initializers
from 155 to 138, and BatchNorm intermediates from 53 to 36. Its external-data
C is 253,210 bytes versus 267,813 bytes for baseline, its weight blob is
byte-identical, and all 1,000 outputs agree with maximum absolute error
`2.00271606e-05`. Its mean/median are `208.772/208.556 ms`: substantially
closer to baseline than unrestricted fusion, but still about 1.1% slower by
mean in this non-isolated run. This shows that the ordinary module callback
controls application-scale rewrite selection; it does not validate the
example's two-feature cost model as a useful target policy. Repeat this
experiment under the eventual frozen protocol before using latency in a paper
claim.

`mobilenetv2-backend-pilot.csv` records a later ten-run comparison on the same
host and input. `joggle-c-strict` uses Apple Clang 17 with `-O3 -DNDEBUG`;
`joggle-c-fast` additionally uses `-mcpu=native -ffast-math`; `onnxruntime`
uses ONNX Runtime 1.26.0 with NumPy 2.4.4, CPUExecutionProvider, graph
optimizations, sequential execution, and one intra/inter-op thread. Each row
excludes loading and allocation after three warm-up calls. The run was not
isolated or frequency-controlled, so it is a direction-setting pilot rather
than a paper performance result.

The ONNX Runtime rows can be reproduced with
`python3 paper/bench_onnxruntime.py MODEL INPUT EXPECTED --repetitions 10` in
an environment containing the recorded ONNX Runtime and NumPy versions. The C
rows use `examples/onnx/benchmark.c` and the flags stated above.

`model-coverage-pilot.csv` records stage-level status for checksum-pinned ONNX
Zoo models. A row marked `compile` is not counted as numerical correctness;
only rows with `execute=pass` used the official stored output.
