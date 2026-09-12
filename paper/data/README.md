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
Zoo models. A row with `compile_c=pass` is not counted as numerical
correctness; only rows with `execute=pass` used the official stored output.
`not_run` means that the current gate stops before that stage; it must not be
read as either support or failure. `n/a` means that the frontend has no
separate stage with that name. The TFLite MobileNet row uses the pinned model
checked by `test/tflite.cpp`; its exposure gate validates structure and
round-trip stability, not generated-C execution.

`mobilenetv2-unroll-pilot.csv` compares the unmodified generated C with a
module policy that applies `tile.unroll` to legal innermost loops whose static
trip count is at most three. Both variants use `-std=c99 -O3 -DNDEBUG`, five
warm-up calls, and 20 timed calls. The policy expanded 72 loops and increased
external-weight C from 244,239 to 307,711 bytes. All outputs retained maximum
absolute error `2.0980835e-05`; median latency changed from 206.182 to
205.722 ms, which is not treated as a speedup on this unisolated sequential
pilot. The result directs optimization work toward index/layout structure and
target implementations rather than more indiscriminate unrolling.

`mobilenetv2-specialize-pilot.csv` is a matched follow-up using the same
strict C flags, input, external payload, five warm-ups, and 20 timed calls.
The specialized variant expands only `[stage: "shape"]` loops, projects
compile-time list positions, and selects constant branches. It removes 328
rank loops and 492 compound-list indices, reduces generated C from 244,239 to
226,855 bytes, and preserves the official-output error bound. Median latency
changed from 204.549 to 201.240 ms (-1.62%) on this unisolated run.

`compiler-overhead-pilot.csv` is a matched release-build measurement of
`debug.prepare` on the same 28,413,202-byte MobileNetV2 IR. The baseline is
commit `f2a759c`; the candidate caches immutable module visibility and stores
small compile-time frames contiguously instead of allocating one hash-table
node per value. Both commands used the same model and module tree and emitted
byte-identical 28,523,126-byte IR with the recorded SHA-256. One untuned run
fell from 75.47 to 55.71 seconds (-26.18%). This is an engineering pilot, not
a publication timing result: it has one repetition, no host isolation, and
mixes two implementation changes that require separate ablation if compiler
overhead becomes a paper claim.
