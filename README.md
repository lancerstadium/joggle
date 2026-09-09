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
single native-function ABI. Functions, operations, and values may carry open,
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
Named ONNX dimensions become ordinary integer generics on the imported
function, while anonymous dynamic dimensions remain `_`; both preserve one
tensor type instead of introducing a dynamic-shape IR.
Node attributes remain operation metadata rather than fake dataflow operands,
while original value identity remains value metadata, so ordinary signature
matching can bridge a real imported network. The pinned
MobileNetV2 gate maps every one-input ReLU through the same data-driven relation
used by small models and proves that a second bridge run is unchanged.
The standard `math`, `tensor`, `quant`, and `nn` modules contain scalar math
primitives and inspectable bodies for tensor algebra, explicit quantization,
grouped 2-D convolution, batch
normalization, average/max pooling, reshape, linear layers, and ReLU. A generic
NumPy-style broadcast relation supports rank extension and singleton
dimensions in shared tensor code, so ONNX and TFLite Add reuse the same
inspectable semantics. A generic body-expansion edit can expose a
selected network call as tensor calls and later expose those calls as loops.
`opt.legalize` can instead retain a consumer's function capabilities and expose
everything else to a bounded depth; `opt.frontier` reports the remainder. Both
resolve ordinary overloads and have no NN-operator switch. `base`
dictionary access lets ordinary bridge functions interpret frontend
attributes. A bridge may add a module dependency and apply a data-driven call
mapping, so frontend-to-network relationships stay outside both codecs and
core. Compile-time functions can inspect, construct, and write structural `Ty`
trees. `tensor.elem`, `tensor.shape`, `tensor.dims`, and `tensor.type` provide
concrete and symbolic tensor projections plus one constructor, enabling shape
and custom-format reasoning without parsing type strings. Generic function
expansion can reuse caller-owned dimension bindings inside shape lists.
Structural and static-shape predicates let relations safely retain
not-yet-inferred calls. Common ONNX binary operations, Flatten, and rank-two-or-
higher MatMul preserve named extents and reuse the same inspectable tensor/
network bodies; MatMul broadcasts leading batch dimensions and Transpose is an
ordinary rank-generic tensor permutation.
Unrepresentable symbolic products remain source calls instead of triggering a
model-specific guess.
Convolution and pooling likewise keep symbolic batch dimensions while checking
only the extents used by spatial arithmetic. ONNX Conv's optional bias reuses
the existing layout-explicit `nn.conv2d` composition. The frontend-neutral
`quant` module defines per-tensor and per-axis zero-point subtraction,
rescaling, round-to-nearest-even, saturation, and conversion as ordinary
functions. Quantized element type and bounds are explicit operands, so custom
formats can reuse the computation without becoming core types. The ONNX bridge
maps compatible three-input QDQ nodes to those functions and leaves newer or
incompatible schema forms visible.
The same tensor library supplies broadcast-batched MatMul and an axis-generic
line-offset relation. `nn.softmax` uses the latter directly, allowing frontend
bridges to materialize an axis as an ordinary operand instead of choosing a
rank-specific kernel class. A complementary multi-axis relation maps reduction
coordinates without transposing the tensor; the inspectable `tensor.mean` body
therefore covers arbitrary normalized axes and either retained or removed
dimensions. The ONNX bridge derives the result shape and conservatively keeps
malformed reductions in the source namespace.
Broadcast-aware division and power plus elementwise square root, reciprocal,
and hyperbolic tangent complete a decomposed normalization/GELU path without a
fused model-specific call. ONNX Add/Sub/Mul use the two-operand semantic
overloads; activation-bearing overloads remain available to TFLite instead of
injecting a synthetic `"NONE"` operand into every frontend.
Shape programs do not introduce a second shape IR. `ir.def` exposes a value's
ordinary defining operation, while bounded byte access lets a module decode
small integer tensor constants. `onnx.nn` recursively evaluates the shape-only
subset of Shape, Gather, Slice, Unsqueeze/Squeeze, Concat, and Cast when
deriving a Reshape result. Known rank and unknown extents remain ordinary `_`
dimension terms, while exact symbolic factors are cancelled without adding an
expression dialect. A symbolic target such as `[N, 12]` therefore remains a
normal tensor type and the converted Reshape still expands through
`tensor.reshape`.
Type refinement fills only open tensor elements or dimensions and rejects
conflicting facts. Structural relations for ConstantOfShape, OneHot, dynamic
quantization, integer MatMul, Split, and transposed batched MatMul let a
12-layer quantized BERT graph reach shared semantics. Dynamic quantization is a
normal multi-result function, integer MatMul exposes zero-point subtraction and
`i32` accumulation, Cast is an element loop, and a parameterized
`tensor.matmul` absorbs transpose/scale instead of preserving a vendor call.
`ir.retarget` atomically changes a call and its operands only when the ordinary
overload resolver accepts the prospective call, so an unsupported mapping
leaves that call unchanged rather than invalidating a complete transform.
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
This second frontend adds no dependency or case to the core. Every TFLite
tensor is represented by one typed `Val`; its source identity and optional
quantization or sparsity description live on that value, while operator
options remain on the producing `Op`. A dependency-local quantized Add gate
checks import and canonical round-trip, then requires the generic floating-
point bridge to retain that call until a TFLite relation can prove and
materialize its rescaling semantics through `quant`.
The separately selected `tflite.nn` relation then converts all 66 compute calls
in that model to shared `nn`/`tensor` functions. Logical-axis operands retain
NHWC and both TFLite weight layouts without creating a second IR or a
layout-specific core operation.

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

Module directories remain the only distribution unit. The CLI can discover,
validate, inspect, install, and uninstall them without a registry or another
manifest:

```sh
./build/joggle module list -M modules
./build/joggle module info tensor -M modules
./build/joggle module check nn -M modules
./build/joggle module install path/to/my.module local-modules -M modules
./build/joggle module uninstall my.module local-modules
```

Installation refuses to overwrite an existing module and commits a staged copy
only after its declarations, dependencies, and optional native binding load
successfully.

The optional ONNX codec keeps Protobuf out of the core build:

```sh
cmake -S . -B build -DJOGGLE_BUILD_ONNX=ON
cmake --build build
./build/joggle read onnx.read model.onnx -M build/modules > model.jog
```

After a frontend bridge, a consumer can retain exactly the functions it
supports and expose the rest through ordinary function bodies:

```jog
module edge
use opt

fn caps() -> list<str> {
  return ["edge.load", "edge.mac", "edge.store"]
}

fn prepare(m: Mod) -> bool {
  return opt.legalize(m, caps(), 16)
}
```

`opt.frontier(m, caps())` reports the remaining unsupported calls. This is a
module function, not a target registry or a second IR.

The optional TFLite codec similarly keeps FlatBuffers private to its module.
Its opt-in build requires a FlatBuffers package that provides both the library
and `flatc`:

```sh
cmake -S . -B build -DJOGGLE_BUILD_TFLITE=ON
cmake --build build
./build/joggle read tflite.read model.tflite -M build/modules > model.jog
./build/joggle run tflite.nn.convert model.jog -M build/modules > network.jog
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
