# ONNX application path

This example runs a complete, single-input/single-output `f32` ONNX model
through the same explicit Joggle pipeline used by applications:

1. import the binary model;
2. convert source calls through `onnx.nn`;
3. remove dead payloads;
4. expose and execute the deterministic VM image;
5. expose the same computation for C;
6. plan static tensor storage;
7. emit C and its header, compile them, and compare with the official output.

The driver derives tensor sizes from the official protobuf inputs instead of
embedding one model's names or dimensions. The C harness consumes only the
generated header.

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
model.jog
model.vm
model.c
model-blob.c
model-blob.h
model.bin
model.h
bounds.json
input.bin
expected.bin
result.txt
```

`model.jog` is the prepared and statically planned loop-level IR consumed by
both C forms; `model.vm` is emitted from the same converted model before the
target-specific preparation step. `model.h` and `model-blob.h` are the
declarations consumed by the two C executions.
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
