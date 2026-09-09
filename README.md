# Joggle

Joggle is a small C++20 compiler workbench for neural-network and hardware/
software co-design research. It gives experiments one readable IR, one module
format, and one extension boundary without prescribing a target, scheduler, or
paper mechanism.

The rebuilt core parses and verifies typed generic functions, multi-result
calls, constants, structured loops and conditions, explicit returns, and
loop-carried values.
Generic parameters are ordinary `Val`s, so custom widths, element types, and
shapes use the same type checker instead of a trait or kind registry. A tested
C++ transform and an ordinary `.jog` function edit the same representation.
Compile-time functions traverse universal IR handles and run transactionally;
they are functions, not instances of a pass class. A separately built native
module is also discovered, signature-checked, loaded, and called through the
single native-function ABI. Functions and operations may carry open,
user-defined attributes without adding parser cases. Generic zero/multi-result
call, constant, loop, and branch construction, deep cloning, checked motion,
nested traversal, selective use replacement, and region fusion let such modules
make real graph changes; the pinned MobileNetV2 test fuses 36 three-call chains.
The `opt` module also supplies policy-parameterized CSE and dead-call
elimination plus a bounded fixed-point pipeline; callers state which functions
are pure instead of adopting a built-in effect hierarchy.
An optional `Attr` output from `run` reports nested function completions,
reported change flags, and actual revision deltas without another result type.
Ordinary read-only functions can be invoked through `query`; results are cached
by function, explicit inputs, environment epoch, and module revision, while an
attempted mutation is isolated and rejected.
Host code may also pass an ordered span of function names to `run`; the complete
sequence is transactional and returns the individual structural reports.
The core contains no ONNX, device, instruction-set, runtime, or code-generation
policy; those capabilities belong in removable modules.
The optional ONNX transport preserves typed intermediate values and native
multi-result/multi-output structure without defining any ONNX operator in core.
Node attributes remain operation metadata rather than fake dataflow operands,
so ordinary signature matching can bridge a real imported network. The pinned
MobileNetV2 gate maps every one-input ReLU through the same data-driven relation
used by small models and proves that a second bridge run is unchanged.
The standard `math`, `tensor`, and `nn` modules contain scalar math primitives
and inspectable bodies for tensor algebra, grouped 2-D convolution, batch
normalization, global pooling, reshape, linear layers, and ReLU. A generic
body-expansion edit can expose a
selected network call as tensor calls and later expose those calls as loops;
it resolves ordinary overloads and has no NN-operator switch. `base`
dictionary access lets ordinary bridge functions interpret frontend
attributes. A bridge may add a module dependency and apply a data-driven call
mapping, so frontend-to-network relationships stay outside both codecs and
core. Compile-time functions can inspect, construct, and write structural `Ty`
trees. `tensor.elem`, `tensor.shape`, and `tensor.type` provide the common
tensor projections and constructor, enabling shape and custom-format reasoning
without parsing type strings.
The optional `onnx.nn` relation module is selected explicitly. On the official
MobileNetV2 it propagates all intermediate tensor types, then converts every
compute node to shared semantics. The model marker and tensor payloads remain
ONNX transport calls; unknown operators in other models remain open rather
than acquiring guessed semantics.
Conversion removes source-schema metadata only after its values have become
ordinary operands. The same official model is then expanded one function body
per compute node and verified and round-tripped as loop/tensor IR.
The optional TFLite codec independently exercises the same boundary on the
official TensorFlow Hub MobileNetV2. Its FlatBuffer schema generates a private
build header with mini-reflection, so schema-known operator option tables are
transported without a switch over operator names or a checked-in generated API.
This second frontend adds no dependency or case to the core.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/prefix
```

Check a module or run a textual transform:

```sh
./build/joggle check test/data/matmul.jog
./build/joggle run opt.fold_add_zero test/data/matmul.jog -M modules
```

The optional ONNX codec keeps Protobuf out of the core build:

```sh
cmake -S . -B build -DJOGGLE_BUILD_ONNX=ON
cmake --build build
./build/joggle read onnx.read model.onnx -M build/modules > model.jog
```

The optional TFLite codec similarly keeps FlatBuffers private to its module.
Its opt-in build requires a FlatBuffers package that provides both the library
and `flatc`:

```sh
cmake -S . -B build -DJOGGLE_BUILD_TFLITE=ON
cmake --build build
./build/joggle read tflite.read model.tflite -M build/modules > model.jog
```

The pinned integration model can be fetched and checked independently:

```sh
cmake -DOUT=/tmp/mobilenet_v2.tflite -P test/tflite.cmake
```

The optional `sat` module is a compact extension example rather than a built-in
target. It adds a parameterized saturating integer, a type-directed selection
function, a scalar reference model, and a SystemVerilog emitter:

```sh
cmake -S . -B build -DJOGGLE_BUILD_SAT=ON
cmake --build build
./build/joggle run sat.select test/data/sat.jog -M build/modules
```

The core and command-line tool require only a C++20 compiler and the standard
library. Building does not download dependencies. Installation provides the
`Joggle::joggle` CMake target, CLI, public header, and standard modules under
`share/joggle/modules`.

## Shape of the project

- `joggle::Env` owns loaded modules, native bindings, and environment
  diagnostics.
- `joggle::Mod` owns one self-contained IR unit.
- `Fn`, `Blk`, `Op`, and `Val` are stable handles into a `Mod`.
- `Ty` and `Attr` are structural values.
- `.jog` is the only source and readable IR format.
- imports, transforms, analyses, simulators, and emitters are module functions,
  not separate plugin class families.

Start with [the design](docs/design.md), then read the
[language](docs/language.md), [module model](docs/modules.md), and
[tutorial](docs/tutorial.md). The [roadmap](docs/roadmap.md) defines the
remaining completion gates and compatibility policy.

The implementation removed during the redesign remains recoverable at Git tag
`archive/pre-relaunch-a2a281e`.

## License

MIT. See [LICENSE](LICENSE).
