# Modules

Modules are Joggle's only extension unit. They define types, semantics,
analyses, transformations, codecs, and artifact generation with the same
`.jog` functions used by application code.

This document explains module boundaries and the bundled module set. Function
signatures in `module.jog` are the authoritative API.

## Package layout

A source-only module needs one file:

```text
my_module/
  module.jog
```

Larger modules may add sorted source fragments and one native library:

```text
my_module/
  module.jog
  lib/
    shapes.jog
    transforms.jog
  native/
    libmy_module.dylib
```

`module.jog` begins with a module name and optional dependencies:

```jog
module my_module
use ir
use tensor

local fn helper(op: Op) -> bool {
  return ir.callee(op) == "nn.relu"
}

fn apply(m: Mod) -> bool {
  var changed = false
  for op in ir.ops(m) {
    if helper(op) {
      changed = ir.set(m, op, "implementation", "lut") || changed
    }
  }
  return changed
}
```

Top-level `fn` declarations are public. `local fn` declarations are
implementation details. No generated header, export list, registration
routine, or version suffix in a symbol name is required.

## Discovery and lifecycle

Module roots are supplied with repeatable `-M` options. Resolution is by
module name, and `use` dependencies close transitively. Commands operate on
the same public surface used by embedding code:

```sh
joggle module list -M modules
joggle module info tensor -M modules
joggle module check tensor -M modules
joggle module install path/to/source installed-modules -M modules
joggle module upgrade path/to/source installed-modules -M modules
joggle module uninstall my_module installed-modules
```

Installation and upgrade validate the candidate and its dependency closure
before replacing an installed module. Source-only modules remain readable and
portable. A native boundary is optional and should be used only for facilities
that cannot be expressed economically in `.jog`, such as binary decoding or
executing a host artifact.

## Composition

There is no built-in pipeline object. Users compose module functions explicitly:

```sh
joggle read onnx.read model.onnx -M modules > model.jog

joggle run onnx.nn.convert opt.basic model.jog \
  -M modules > semantic.jog

joggle run c.prepare mem.plan semantic.jog \
  -M modules > prepared.jog

joggle emit c.source prepared.jog -M modules > model.c
```

Several `run` functions form one transaction. If a later function fails, all
earlier mutations in that invocation are rolled back. `query` and `emit`
use the same resolver but do not establish separate analysis or backend
registries.

Functions can also accept a function handle. For example, a loop-fusion policy
is an ordinary `fn` selected with `ir.find` and invoked by `ir.invoke`.
This allows an experiment to replace policy without modifying the mechanism.

## Responsibility map

The bundled modules are grouped here for explanation only. The runtime does not
hard-code these categories.

| Module | Public responsibility |
| --- | --- |
| `base` | Compile-time collections, structural type construction, text utilities, assertions, and scalar operator declarations |
| `ir` | Reflection, resolution, safe editing, cloning, expansion, replacement, and function invocation |
| `tensor` | Tensor type constructor, shape algebra, indexing, broadcasting, reductions, reshaping, and inspectable tensor bodies |
| `nn` | Frontend-neutral neural-network semantics expressed in terms of tensor and scalar functions |
| `quant` | Quantization, dequantization, and quantized tensor computation |
| `math` | Portable scalar mathematical functions |
| `opt` | General simplification, dead-code elimination, common-subexpression elimination, exposure, implementation selection, and call fusion |
| `bounds` | Conservative integer range inference and representation checks |
| `stat` | Structural program measurements through user-supplied measurement functions |
| `mem` | Static tensor-buffer reuse planning and inspectable slot annotations |
| `tile` | Explicit loop splitting, unrolling, and pointwise producer/consumer fusion |
| `onnx` | ONNX binary decoding and source-format calls |
| `onnx.nn` | ONNX type refinement and explicit conversion to shared semantics |
| `tflite` | TFLite binary decoding and source-format calls |
| `tflite.nn` | TFLite type refinement and explicit conversion to shared semantics |
| `c` | C capability checks, preparation, ABI description, header/data generation, and source emission |
| `vm` | Deterministic VM capability checks, preparation, image emission, and execution |
| `sat` | Example parametric saturating type, overloads, selection, and materialization |
| `sat.c` | C-specific representation of `sat` |
| `sat.vm` | VM-specific representation of `sat` |

## Foundational modules

### `base`

`base` is the compile-time standard library. It supplies collection and
attribute operations, structural type constructors, canonical text utilities,
and the generic operator declarations used by overload resolution. It should
not acquire neural-network or target knowledge.

### `ir`

`ir` is the sole program-editing surface for `.jog` modules. Its API exposes:

- collections such as `ir.fns`, `ir.blks`, `ir.ops`, and `ir.vals`;
- symbol and type queries such as `ir.find`, `ir.resolve`, and `ir.type`;
- metadata queries that preserve extension-owned keys;
- construction and editing through `ir.call`, `ir.constant`, `ir.loop`,
  `ir.branch`, `ir.clone`, `ir.move`, `ir.replace`, and `ir.args`;
- body reuse through `ir.expand` and `ir.fold`;
- policy invocation through `ir.invoke`.

All mutations are checked against handle ownership and take part in the caller's
transaction. A higher-level module should build on these operations instead of
requiring a new native binding for each transformation.

## Semantic modules

### `tensor`

`tensor` owns the `tensor<E, S>` constructor and reusable tensor algebra.
Shapes and element types remain structural values. Indexing, reshape, broadcast,
permutation, concatenation, reduction, and matrix multiplication have ordinary
function signatures; inspectable bodies can be expanded when a target needs
lower-level computation.

This is the common substrate for imported networks and user kernels. It is not
an ONNX or TFLite operator catalogue.

### `nn`

`nn` gives frontend-neutral names and bodies to common inference operations:
convolution, bias and activation, pooling, normalization, linear algebra,
elementwise functions, and softmax. Overloads capture layout, shape, and
optional parameters without creating operation classes.

An `nn` call may remain compact for graph rewrites or be exposed into tensor
and scalar computation. New implementations may be added as overloads or
selected through open metadata; a target consumes the resolved call rather
than a hard-coded neural-network enum.

### `quant` and `math`

`quant` expresses quantized values and conversions with tensor functions.
`math` supplies scalar operations required by exposed neural-network bodies.
Keeping both independent prevents a frontend schema or target emitter from
becoming the semantic definition.

## Frontends

A frontend is deliberately split into transport and meaning:

- `onnx.read` and `tflite.read` decode bytes into faithful source-format
  calls and typed constants;
- `onnx.nn.convert` and `tflite.nn.convert` explicitly map those calls to
  shared functions.

The split preserves source attributes for inspection and allows a user to run
format-specific checks before conversion. It also keeps multiple frontends
from duplicating canonical tensor and neural-network bodies.

Adding a frontend should require:

1. one codec function returning `.jog` text;
2. schema-local type refinement where the external format requires it;
3. a conversion function built from `ir` edits and shared semantics;
4. conformance tests against authoritative models and reference outputs.

It should not require edits to the core, the C emitter, or another frontend.

## Analyses and transformations

`bounds` and `stat` return ordinary compile-time data. `opt`, `mem`, and
`tile` edit the same function bodies that users inspect.

`opt.expose` is the main connection between semantics and a target. It asks a
capability function whether an operation is accepted and expands available
bodies only where needed. `opt.apply` selects compatible implementations;
`opt.basic` performs target-independent cleanup. `opt.specialize(m, key,
value)` fully expands finite static loops carrying the selected open attribute
and folds list projection and constant branches around dynamic values. The
selection is explicit: the core never treats an attribute as behavior.

`mem.plan` assigns reusable static slots to tensor values after lifetimes and
shapes are known. `tile` provides conservative structural loop operations.
These modules are intentionally separate: storage and scheduling policy can be
replaced independently and neither changes the core IR.

## Target modules

A useful target module exposes:

```jog
fn accepts(m: Mod, op: Op) -> bool
fn prepare(m: Mod) -> bool
fn source(m: Mod) -> str  // or another artifact function
```

The names are conventions, not interfaces baked into the runtime.
`accepts` defines a testable boundary. `prepare` explicitly exposes or
rewrites unsupported calls. Artifact functions return text or bytes and must
reject programs outside their advertised boundary.

`c` derives scalar spelling, alignment, index type, headers, payload layout,
and external prototypes from a configuration dictionary and resolved
signatures. `vm` emits a deterministic image and reports executed steps.
Neither receives privileged access to the IR.

By default, `c` derives public symbols from qualified function names and keeps
valid source value names. Named pointer results derive from the return value
with a collision-checked `_out` suffix. An external payload argument uses
exactly the name passed to `c.source`/`c.header` after C identifier
sanitization; a collision with a source parameter is rejected. The `joggle_`
prefix is reserved for otherwise unnamed result buffers, storage slots,
temporary values, and C-keyword escapes; it is not added to user names. A
module can pin an external name with `[c: {name: "vendor_kernel"}]`. Because
Joggle is pre-1.0, source-derived spelling is not itself a stable ABI promise:
applications that require ABI stability should use an explicit binding and
compile the emitted header and source from the same IR.

Target-specific support for a user type belongs in a small companion module.
The `sat.c` and `sat.vm` modules illustrate this rule: `sat` owns the type
semantics, while each companion owns only its representation at that target.

## Extension checklist

A module is ready to share when:

- its name and public functions describe concepts rather than a development
  phase or version;
- public behavior is visible in `module.jog`, with helpers marked `local`;
- dependencies are explicit and minimal;
- unknown metadata is preserved;
- failure is transactional and diagnostics identify the rejected operation;
- output is deterministic;
- at least one out-of-tree use works without modifying core files;
- examples show both invocation and resulting IR or artifact;
- claims about numerical correctness or speed are backed by stored inputs,
  reference outputs, commands, and measurements.

See [tutorial.md](tutorial.md) for guided examples and
[design.md](design.md) for the invariants behind these rules.
