# MobileNetV2 application

This is Joggle's application-sized example, not a synthetic operator fixture.
It uses the hash-pinned official ONNX Model Zoo MobileNetV2 model, protobuf
input, and protobuf reference output.

The example performs one explicit pipeline:

1. import the ONNX file;
2. convert source calls through `onnx.nn`;
3. remove dead constant data;
4. expose the same function bodies for the deterministic VM;
5. execute the VM and check all 1,000 outputs;
6. expose the remaining computation for C;
7. plan and place static tensor storage;
8. emit C and its header, compile them, and check all outputs again.

The files are deliberately shared with the regression gate:

- `app.cpp` drives the compiler and VM through the public C++ API;
- `main.c` consumes only the generated header and executes generated C;
- `run.cmake` records the complete reproducible command pipeline.

From the repository root:

```sh
cmake -DOUT=.cache/onnx-zoo -DMODELS=mobilenetv2-7 -DAPP=ON \
  -P test/zoo.cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DJOGGLE_BUILD_ONNX=ON \
  -DJOGGLE_EXAMPLE_MOBILENET=.cache/onnx-zoo/app/mobilenetv2-7
cmake --build build
ctest --test-dir build -R '^onnx-app-mobilenet$' --output-on-failure
```

This is intentionally slow: the scalar VM executes roughly 98 billion
instructions. It is a correctness and inspectability reference, not the
preferred optimized execution strategy. After it finishes, inspect:

```text
build/examples/mobilenet/model.jog
build/examples/mobilenet/model.vm
build/examples/mobilenet/model.c
build/examples/mobilenet/model.h
build/examples/mobilenet/result.txt
```

`model.jog` is the actual loop-level IR given to both targets. `model.h` is the
only declaration consumed by `main.c`; the harness contains no handwritten
model prototype.
