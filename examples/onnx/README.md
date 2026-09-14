# ONNX application path

This example runs an official ONNX model through the same explicit Joggle
pipeline used by applications:

1. import the binary model;
2. convert source calls through `onnx.nn`;
3. remove dead payloads;
4. expose and execute the deterministic VM image;
5. expose the same computation for C;
6. fold proved bounds and the scalar expressions they simplify;
7. promote proved reduction state from memory to scalar values;
8. plan static tensor storage;
9. state the application's disjoint input/output/weight ABI contract;
10. emit C and its header, compile them, and compare with the official output.

The current protobuf extractor used by the two configured application gates is
limited to one `f32` input and output. The C application boundary is not: the
test harness is generated from `c.api`, supports any number of tensor inputs
and outputs, checks byte counts before calling the model, and selects numerical
comparison from each result's C representation. It never contains a model
name, shape, declaration, or result-count table.

## Fast, inspectable case

MNIST is the normal application example. It is a conventional convolutional
network from the official ONNX Model Zoo but remains small enough for the
scalar reference VM:

```sh
cmake -DOUT=.cache/onnx-zoo -DMODELS=mnist-8 -DAPP=ON \
  -P test/zoo.cmake
cmake -S . -B build-onnx-app -DCMAKE_BUILD_TYPE=Release \
  -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_EXAMPLE_MNIST=.cache/onnx-zoo/app/mnist-8
cmake --build build-onnx-app
ctest --test-dir build-onnx-app -R '^onnx-app-mnist$' --output-on-failure
```

## Application-sized stress case

MobileNetV2 exercises the identical driver and pipeline at application scale:

```sh
cmake -DOUT=.cache/onnx-zoo -DMODELS=mobilenetv2-7 -DAPP=ON \
  -P test/zoo.cmake
cmake -S . -B build-onnx-app -DCMAKE_BUILD_TYPE=Release \
  -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_EXAMPLE_MOBILENET=.cache/onnx-zoo/app/mobilenetv2-7
cmake --build build-onnx-app
ctest --test-dir build-onnx-app -R '^onnx-app-mobilenet$' --output-on-failure
```

The MobileNet VM case is intentionally slow: it executes roughly 96 billion
scalar instructions. It is a correctness stress test, not the preferred
optimized execution strategy.

Each configured case leaves these inspectable artifacts under
`build-onnx-app/examples/<name>/`:

```text
canonical.jog
model.jog
model.vm
model.c
model-blob.c
model-blob.h
model.bin
model.h
api.json
api-blob.json
harness.c
harness-blob.c
model
model-blob
bounds.json
input.bin
expected.bin
result.txt
```

`canonical.jog` is the exposed and cleaned loop-level IR before optional loop
policy. `model.jog` additionally applies scalar-state promotion, static
storage planning, and the application's explicit `c.noalias` ABI contract;
it is consumed by both C forms. `model.vm` is emitted from
the same converted model before the target-specific preparation step.
`model.h` and `model-blob.h` are the
declarations consumed by the two C executions. The two JSON files are the
matching machine-readable interfaces; the harness sources are generated from
them and remain beside the artifacts so the exact call, allocation, comparison,
and timing code can be inspected.
`model.c` is the self-contained form; `model-blob.c` accepts the exact payload
bytes in `model.bin` as an explicit read-only function argument. Both forms are
compiled under strict warnings and checked against the official output.
`bounds.json` contains proven
integer intervals for that exact `model.jog` revision; it is evidence for later
target policy, not an implicit change to the generated C ABI.

The application gate produces both forms automatically. To reproduce the
external-data pair by hand, emit a raw companion blob and select the explicit
overload:

```sh
joggle emit c.data model.jog -M modules > model.bin
joggle emit c.source model.jog --arg '"weights"' -M modules > model-blob.c
```

Emit `c.header` with the same string argument to obtain the matching interface.
The application may read, map, download, or point into ROM for `model.bin` and
passes that pointer explicitly. The default `model.c` remains useful when a
single self-contained translation unit matters more than compile size.

## Measure generated code

`make_harness.py` consumes one exported function from `c.api`. It generates a
strict C11 program for multiple tensor inputs and outputs, optional independent
weights, reference comparison, warm-up, and repeated timing. File loading,
allocation, reference comparison, and the deterministic output hash stay
outside each timed interval. The application gate already produces
`harness-blob.c`; it can also be regenerated without parsing a header:

```sh
python3 make_harness.py api-blob.json harness-blob.c \
  --header model-blob.h
cc -std=c11 -O3 -Wall -Wextra -Wstrict-prototypes -Werror \
  -I . model-blob.c harness-blob.c -lm -o model-blob
./model-blob input.bin model.bin expected.bin 3 30 > timings.csv
```

Arguments are ordered as all input files, the optional weight file, all
reference-output files, warm-up count, and repetition count. If `c.api` exposes
multiple public functions, `--entry` selects the C symbol. Direct scalar
parameters and returns are rejected because this is a neural-network buffer
harness, not a generic C FFI generator. The resulting rows contain iteration,
wall-clock seconds, and an output hash that prevents an unused computation from
masquerading as a speedup. Report the raw rows, compiler and flags, machine
state, and a matched baseline; a single mean is not paper evidence. Standard
output is CSV; numerical comparison details and failures use standard error.

The application gate deliberately leaves optional transforms out of its
baseline. A matched fusion experiment starts from its prepared `model.jog`,
replans after the explicit rewrite, and emits a separate artifact:

```sh
joggle run tile.fuse mem.plan model.jog -M modules > fused.jog
joggle emit c.data fused.jog -M modules > fused.bin
joggle emit c.source fused.jog --arg '"weights"' -M modules > fused-blob.c
```

Emit the matching header, compile `fused-blob.c` with the same flags, and run
both binaries with identical warm-up and repetition counts. Keeping the
transform out of import, preparation, and emission is what makes the baseline
and policy choice auditable.
