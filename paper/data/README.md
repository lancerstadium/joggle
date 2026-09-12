# Pilot measurements

Files in this directory are auditable engineering observations, not final
paper measurements. They preserve negative results that determine the next
experiment instead of presenting them as controlled performance claims.

`extension-footprint-pilot.csv` is generated from the frozen task manifest by
`paper/measure_extensions.py`. It validates the named modules and tests, then
records only mechanically observable footprint: source files, nonblank
non-comment source lines, bytes, declared dependencies, and public functions.
The three out-of-tree tasks range from 20 source lines for an alternative
matrix body to 210 source lines for a generic external-kernel adapter plus its
C implementation. The bundled parametric numeric-format task contains 284
source lines across its semantic module, native binding, and two target
companions. These counts do not measure development time, comprehension, or
usability and therefore do not answer RQ2 without controlled baselines.
Each row also stores a digest over the measured relative paths and source bytes
so regeneration cannot silently measure a different implementation.

`instance-specialization-pilot.csv` records the first four-model evaluation of
compiler-owned call-site instances. Each automatic variant starts from the
same semantic model, invokes `opt.instantiate(m, "nn")`, prepares C, plans
memory, selects explicit static placement, emits external-data C, and compiles
it with Apple Clang 17 using strict C11 and `-O3 -DNDEBUG`. The compiler binds
inferred types and literal scalar/list configuration but keeps tensor weights
as parameters. This produces 102, 139, 70, and 40 deduplicated private
functions for MobileNetV2, UltraFace, SqueezeNet, and ResNet18, respectively,
without an operator-specific emitter case. ResNet18's symbolic entry batch is
explicitly bound to one first; the pilot script accepts `ENTRY=main` and
`ENTRY_TYPES=["1"]` to reproduce that step from its semantic model.

All generated sources pass strict compilation and retain their prior reference
error bounds. Against freshly compiled scalar-expanded sources on the same
unisolated Apple M4, median latency changes from 201.841 to 198.469 ms for
MobileNetV2, 33.374 to 32.022 ms for UltraFace, 185.743 to 188.152 ms for
SqueezeNet, and 1.207 to 1.194 s for ResNet18. External-data source size falls
by 26.3%, 8.4%, 15.4%, and 21.8%, while payload size is unchanged. These small
and mixed latency changes are treated as performance parity, not a speedup
claim. Preparation cost and cross-function tensor storage were therefore
selected for direct measurement rather than inferred from source size.

`result-storage-pilot.csv` evaluates the first storage-forwarding step on the
same four automatic-instance models. The revised `mem.plan` treats a returned
tensor binding as caller-owned storage instead of allocating a private local
slot and copying the complete tensor at return. This is a function/storage
rule, not an operator or C-emitter case. It removes 102, 125, 52, and 40 local
slots and reduces the sum of statically allocated workspace elements by 86.6%,
82.2%, 63.9%, and 76.8% for MobileNetV2, UltraFace, SqueezeNet, and ResNet18.
External-data C source falls by another 4.7--5.2% and all reference error bounds
are unchanged.

The latency rows come from three alternating processes per variant, each with
three warm-ups and ten recorded calls, under the same unisolated Apple M4 and
strict C11 `-O3 -DNDEBUG` setup. Median changes are +2.3%, +5.8%, -0.2%, and
+0.9%, respectively. This is a substantial deterministic-workspace reduction,
not a latency optimization.

`dead-fill-pilot.csv` records the next storage/code-quality step. `mem.plan`
proves from Def-Use and loop structure that a constructor is followed by an
unconditional full linear or rectangular overwrite. The C target then omits
that constructor's fill loop. No operator name participates in the proof.
Across MobileNetV2, UltraFace, SqueezeNet, ResNet18, and TinyYOLOv2 it removes
25/70, 105/191, 21/61, 11/30, and 22/37 fills, respectively; source size falls
by 0.9--3.0%. Reference error bounds are unchanged. The table intentionally
contains no latency column because the first unisolated runs did not establish
a stable speed effect.

`spatial-conv-pilot.csv` evaluates an out-of-tree convolution body rather than
a C-emitter special case. The module keeps each output's floating-point
reduction order but makes output columns the innermost loop. Compiler-owned
call-site instances bind shapes and scalar configuration; the same module is
then used unchanged for MobileNetV2, ResNet18, TinyYOLOv2, UltraFace, and
SqueezeNet. All variants use external weights and Apple Clang 17 with strict
C11,
`-O3 -DNDEBUG`, three warm-ups, and ten measured calls in one unisolated
process per variant.

Reference/spatial median latency is 203.305/105.479 ms for MobileNetV2,
1,206.575/385.016 ms for ResNet18, 2,247.627/357.548 ms for TinyYOLOv2,
34.180/25.139 ms for UltraFace, and 181.193/52.591 ms for SqueezeNet. The
corresponding pilot ratios are 1.93x, 3.13x, 6.29x, 1.36x, and 3.45x.
External-data source size stays within -1.3% to +3.5% of the reference-loop
variant; this is a loop-quality result rather than source compaction.
Checksums match within each pair and the prior reference comparisons retain
maximum absolute errors of `2.0981e-5`, `5.0068e-6`, `1.6689e-5`,
`3.5763e-7`, and `5.2452e-6`.
Clang reports width-four vectorization of representative innermost output
column loops that it did not vectorize in the reference ordering. This is
direction-setting evidence for replaceable implementation modules, not a
production-runtime or publication speed claim; the processes were not
interleaved, isolated, pinned, or frequency-controlled.

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

`tinyyolo-backend-pilot.csv` adds a fifth numerically executed ONNX Zoo model.
`paper/reference.py` generated one seed-0 f32 input and the ONNX Runtime 1.26.0
reference for explicit shape `1,3,416,416`; the model's symbolic batch was
instantiated as one before target preparation. Strict generated C agrees over
21,125 outputs with maximum absolute error `1.6689301e-5`. Its ten-call median
is 2.285 s versus 22.587 ms for one-thread sequential ONNX Runtime, roughly a
101x gap. This unisolated pilot is a blocking code-generation result, not a
competitive performance claim. Both reference and benchmark scripts accept an
explicit `--shape` for dynamic input signatures.

`qdq-backend-pilot.csv` adds the sixth numerically executed ONNX Zoo model and
the first model whose graph contains explicit quantize/dequantize boundaries.
The checksum-pinned SqueezeNet 1.0-13 QDQ model uses one deterministic seed-0
`f32` input of shape `[1, 3, 224, 224]`; ONNX Runtime 1.26.0 produced the
1,000-element reference. The same ordinary `onnx.nn` conversion, out-of-tree
spatial convolution selection, call-site instantiation, C preparation, static
memory planning, and external-data emission path produced 173,461 bytes of C
and a 1,298,888-byte payload. The current plan reports 44 slots and 7,428,640
scalar elements across 19 emitted functions; this is deterministic but still
too large to support a low-resource deployment claim. Apple Clang 17 compiled
it with strict C11,
`-O3 -DNDEBUG`, and warnings as errors. All outputs agree with maximum absolute
error `1.3411045e-7`.

After three warm-ups, ten calls in one unisolated process have a 46.021 ms
median. A separate one-thread sequential ONNX Runtime process has a 2.514 ms
median, leaving an approximately 18.3x gap. These raw rows extend correctness
and operator-boundary coverage; their latency is a direction-setting pilot,
not publication evidence.

`model-coverage-pilot.csv` records stage-level status for checksum-pinned ONNX
Zoo models. A row with `compile_c=pass` is not counted as numerical
correctness; only rows with `execute=pass` used the official stored output.
`not_run` means that the current gate stops before that stage; it must not be
read as either support or failure. `n/a` means that the frontend has no
separate stage with that name. The TFLite MobileNet row uses the pinned model
checked by `test/tflite.cpp`; its exposure gate validates structure and
round-trip stability, not generated-C execution.

The same matrix now includes focused partial-cache runs for ShuffleNet V2 and
DenseNet-121. Both decode, infer to a zero unknown-result frontier, convert to
shared semantics, verify, and round trip. DenseNet's later stages remain
`not_run`: no generated-C or numerical claim is inferred from structural
compatibility.

TinyYOLOv3-11 and SSD-MobileNetV1-12 are explicit negative rows. TinyYOLOv3
decodes and round trips but retains 219 unknown results after inference.
SSD-MobileNetV1 infers all result types, then retains 710 source-format calls
after conversion. Ordinary variadic broadcast conversion to shared
`nn.maximum` and `nn.minimum` functions removed the prior 360 Max and 451 Min
calls; no target-emitter case was added. The remaining calls include comparison,
dynamic indexing, NonZero, NonMaxSuppression, If, and Loop. The Zoo gate checks
both frontiers so later changes cannot silently relabel partial compatibility
as full support.

`shufflenet-backend-pilot.csv` advances ShuffleNet V2 through spatial
convolution selection, call-site instantiation, C preparation, static memory
planning, external-data emission, strict C11 compilation, and execution. A
seed-0 `[1, 3, 224, 224]` input produces 1,000 outputs within
`8.5830689e-6` maximum absolute error of ONNX Runtime 1.26.0. The generated C
is 158,829 bytes, its separate payload is 9,179,136 bytes, and four static
workspace slots contain 785,000 `f32` elements in total. After three warm-ups,
ten unisolated calls have a 37.503 ms median versus 1.945 ms for one-thread
sequential ONNX Runtime, a roughly 19.3x backend gap. This is the seventh
application-scale numerical pilot (the eighth end-to-end ONNX model when the
small MNIST gate is included) and a negative performance result, not a
production-speed claim. Adding `-mcpu=native -ffast-math` changes the median
only to 36.917 ms and retains a `7.6293945e-6` error bound, so compiler flags
do not explain the gap; loop structure, layout, and specialized kernels remain
the relevant backend work.

`generic-kernel-pilot.csv` records a four-model follow-up using one portable
NCHW/OIHW f32 convolution implementation selected through ordinary generic
functions. MobileNetV2, ResNet18, UltraFace, and SqueezeNet select 54, 20, 52,
and 26 convolution calls respectively without adding a C-emitter or frontend
case. Their external-weight sources are 85,762, 36,665, 119,688, and 46,310
bytes, compared with the corresponding scalar sources of 223,627, 87,161,
258,279, and 114,057 bytes. All four preserve their recorded numerical error
bounds; SqueezeNet extends the executed coverage with maximum absolute error
`5.2452087e-6` against an ONNX Runtime 1.26.0 reference generated from a seed-0
input.

The same Apple M4, Clang 17, three-warm-up, ten-call pilot was compiled either
as separate translation units or with `-flto`; all other strict C11 flags were
identical. Median latencies in separate/LTO form were 1030.117/333.830 ms for
MobileNetV2, 2035.626/1430.333 ms for ResNet18, 194.961/58.356 ms for
UltraFace, and 475.761/206.846 ms for SqueezeNet. LTO therefore recovers a
large part of the lost constant propagation, but the generic runtime-stride
kernel remains slower than the earlier scalar C baseline on MobileNetV2 and
UltraFace and is about 71x slower than the 2.908 ms SqueezeNet ONNX Runtime
median. This is a negative, direction-setting result: one user definition is
compact and compatible, but Joggle still needs compiler-owned deduplicated
specialization by concrete call signature. Neither the latency nor source-size
observations are publication claims.

The UltraFace-RFB-320 row uses the checksum-pinned ONNX Zoo model and a
deterministic NumPy input generated with seed 0. ONNX Runtime 1.26.0 produced
both reference outputs. Strict external-weight C was compiled with Apple Clang
17 using `-std=c11 -O3 -DNDEBUG -Wall -Wextra -Werror -pedantic-errors` and
matched the scores and boxes with maximum absolute errors `2.9802322e-7` and
`3.5762787e-7`. The external-weight source is 258,279 bytes and the payload is
1,230,688 bytes; the embedded form was about 5.21 MB. After three warm-up calls,
ten C calls ranged from 34.186 to 35.026 ms with a 34.401 ms median. A separate
one-thread, sequential ONNX Runtime pilot had a 4.342 ms median. These
non-isolated measurements expose an approximately 7.9x backend latency gap;
they are not publication performance results.

The ResNet18-v1-7 row uses the checksum-pinned 45 MB ONNX Zoo model. Its
symbolic batch `N` is explicitly instantiated as one with `opt.instantiate`
before C preparation. The same seeded-input and strict external-weight C11
protocol produced maximum absolute error `5.0067902e-6`. Generated C is 87,161
bytes and the payload is 46,796,448 bytes. Ten calls after three warm-ups had a
1.957 s median, compared with 24.862 ms for one-thread ONNX Runtime in a
separate run, an approximately 79x gap. The much larger gap than UltraFace
points specifically to convolution and layout code generation rather than a
uniform frontend or call overhead.

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
