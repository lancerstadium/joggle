# Import an official ONNX model

Download the pinned official model matrix and enable the optional codec:

```sh
cmake -DOUT=.cache/onnx-zoo -P test/zoo.cmake
cmake -DOUT=.cache/onnx-backend -P test/backend.cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_TEST_ONNX_ZOO=.cache/onnx-zoo \
  -DJOGGLE_TEST_ONNX_BACKEND=.cache/onnx-backend
cmake --build build
ctest --test-dir build -L onnx-zoo --output-on-failure
mkdir -p build/examples
./build/joggle read onnx.read .cache/onnx-zoo/mobilenetv2-7.onnx \
  -M build/modules > build/examples/mobilenet.jog
```

The download script also accepts a semicolon-separated `MODELS` subset. When
`JOGGLE_TEST_ONNX_ZOO` names a partial cache, configure verifies every present
model against its pinned SHA-256, registers only those cases, and prints each
absent case as skipped. This makes a small focused gate possible without
weakening integrity checks for the files that are used:

```sh
cmake -DOUT=.cache/onnx-zoo \
  -DMODELS='shufflenet-v2-12;densenet-12' -P test/zoo.cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_TEST_ONNX_ZOO=.cache/onnx-zoo
ctest --test-dir build -R 'onnx-zoo-(shufflenet|densenet)' \
  --output-on-failure
```

Downloading the default set still registers the complete matrix. A partial
cache is therefore an explicit local workflow, not evidence that the omitted
models passed.

[`test/models.cmake`](https://github.com/lancerstadium/joggle/blob/main/test/models.cmake) is the single model declaration
list used by both download and CTest registration. It pins the upstream
revision, path, SHA-256, structural gate, and optional application archive.
Adding a model therefore does not require synchronizing a second name/hash
table in the top-level build. All registered matrix cases carry the
`onnx-zoo` label; the separately opted-in BiDAF case also carries `heavy`.
The consistent structural cases additionally carry `onnx-zoo-record` and emit
one machine-readable stage record without requiring callers to parse human
diagnostics.

The separate backend download is small. It pins ONNX v1.19.0
`test_matmul_2d`, including both inputs and the official output. Its execution
gate converts the imported model, selects the out-of-tree `ikj` implementation,
runs the same ordinary IR through VM and compiled C, and checks both results
against that output. Normal builds
remain offline; neither download runs during configure.

The normal numerical application gate uses official MNIST data. It is a real
convolutional network but remains small enough to inspect and execute through
the scalar reference VM:

```sh
cmake -DOUT=.cache/onnx-zoo -DMODELS=mnist-8 -DAPP=ON \
  -P test/zoo.cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_EXAMPLE_MNIST=.cache/onnx-zoo/app/mnist-8
cmake --build build
ctest --test-dir build -R onnx-app-mnist --output-on-failure
```

The application-sized MobileNetV2 stress gate uses the same driver and is
separately opt-in because model preparation and strict C compilation are much
larger than the normal test cases:

```sh
cmake -DOUT=.cache/onnx-zoo -DMODELS=mobilenetv2-7 -DAPP=ON \
  -P test/zoo.cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_EXAMPLE_MOBILENET=.cache/onnx-zoo/app/mobilenetv2-7
cmake --build build
ctest --test-dir build -R onnx-app-mobilenet --output-on-failure
```

That test performs import, explicit semantic conversion, dead-data cleanup,
body exposure, static memory planning, explicit C storage placement, strict
C11 compilation, and comparison of all 1,000 outputs. The model, input, output,
and archive hashes are checked before execution. The scalar reference VM is
validated independently by the MNIST application and dedicated execution
tests; MobileNet does not prepare or execute a VM artifact before selecting C.
When a workflow requests both targets, the driver prepares the VM from a parsed
copy of the converted model so its destructive preparation cannot affect the C
path. MobileNet's inspectable `model.jog`, `model.c`, generated `model.h`,
input, expected output, executable, and `result.txt` remain under
`build/examples/mobilenet` after the test. The harness has
no handwritten model declaration, so strict compilation also checks that the
header is the actual application ABI rather than a decorative artifact.

The MobileNetV2 gate checks 267 tensor constants, 155 nodes,
14,156,560 initializer bytes, verifier result, and canonical round trip. It
also uses a test function built from `opt.fuse` to combine 36
Conv-BatchNormalization-ReLU chains, then verifies and round-trips the changed
module. The model-matrix gate independently imports, infers, converts, verifies,
and round-trips SqueezeNet 1.1, QDQ SqueezeNet 1.0, ResNet-18, and
Tiny-YOLOv2. They exercise Concat, whole-network QDQ boundaries, residual Add,
and detection-oriented MaxPool/LeakyReLU structure. Tiny-YOLOv3 is the complex
control-flow gate: 269 tensor constants, 291 calls, and four `Loop` body graphs
must import, verify, round-trip, and retain an exact type frontier. Each body is
an ordinary function, with lexical captures exposed as parameters and call
operands. `onnx.graph` recovers that `Fn`; the semantic module derives
loop-carried and scan types from its signature. This closes all eight Loop
outputs and four dependent Reshapes, reducing the current open frontier from
280 results to 219 without pretending the remaining operators are supported.
A separate in-memory protocol case checks the exact capture mapping, typed
multi-result nodes, scan rank, and multiple graph returns. The codec does not
interpret their operator names. Downloads remain an explicit test setup step
and never occur during configure or build. The pinned 1.2 MB UltraFace RFB-320
adds a shape-heavy edge detector rather than another classifier: its 244 tensor
constants and 242 calls infer from 240 open results to zero, then convert,
verify, and round-trip. It covers both tensor-valued Constant nodes and the
legacy attribute-form Slice schema. The evidence matrix additionally prepares,
plans, emits, strictly compiles, and numerically checks both UltraFace outputs.
ResNet-18 exercises the separate symbolic-entry path: `opt.instantiate` binds
its generic batch before the same static C pipeline. All GitHub-hosted models
use immutable repository commits and SHA-256 checks. The 28 MiB
SSD-MobileNetV1-12 detector
is a partial semantic stress gate: 1,567 constants, 5,985 nodes, eight nested
graphs, Resize, and NonMaxSuppression must verify and round-trip, while type
propagation resolves all 6,790 initially open results. Semantic conversion then
stops with 386 source calls, principally post-processing, dynamic indexing, and
control flow. Tensor comparison overloads and the source bridge remove all 279
Equal, Greater, and Less calls; a reusable composition of broadcast maximum and
minimum removes 35 Clip calls; and ten statically shaped Tile calls map to the
shared tensor implementation. None adds an emitter case. This is
deliberately not a full execution claim. ShuffleNet V2
independently requires
complete inference, conversion, verification, and round trip. DenseNet-121
removes every imported intermediate result type before inference and requires
all 910 to be reconstructed from the model signature and constants, preventing
value-info-rich models from producing a false positive. GoogLeNet adds
local-response normalization and a two-result inference Dropout. The bridge
removes Dropout only when training is disabled and the mask has no users;
otherwise the source call remains visible instead of silently changing training
semantics.
