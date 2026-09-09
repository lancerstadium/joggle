# Roadmap

Joggle is intended to become a small compiler workbench that AI software and
hardware co-design researchers can extend without forking its core. Completion
means that a new frontend, data format, optimization, execution model, or
emitter can be supplied as a module and composed with existing work through the
same function IR. A collection of demos is not completion.

## Design sources

The project uses a deliberately narrow subset of ideas from earlier systems:

- Lift: transformations and algorithm structure compose as functions; Joggle
  should not require a second rewrite language or an operator class hierarchy.
- TVM and TileLang: users may start from tensor calls and progressively expose
  loops, storage, and target details. Joggle keeps those stages in `Fn` rather
  than imposing a built-in graph/tile pipeline.
- MLIR, IREE, and ONNX-MLIR: symbols, types, conversion legality, and explicit
  interfaces are essential. Joggle adopts those checks without dialect classes,
  generated adaptors, or an MLIR dependency.
- MIR: a small embeddable implementation, stable handles, explicit ownership,
  and a plain C boundary are more valuable here than a large framework API.
- XOC: data-driven definitions, shared queries, and composable pipelines are
  useful. Its global record registry, stringly `kind` protocols, backend-owned
  reinterpretation, and many behavior fields must not reappear in Joggle.

These are design inputs, not compatibility promises. New abstractions enter the
core only when two unrelated modules need the same structural capability.

## Completion criteria

The project is ready for sustained research use when all of the following hold:

1. Types and symbols are structural, scoped, diagnosable, and extensible by
   modules. Generic calls infer and check their result types without operator
   switches.
2. C++ and `.jog` functions can construct, inspect, clone, move, replace, and
   erase every public IR form while preserving dominance, use-def links, source
   locations, and transactional failure.
3. Ordinary `fn(Mod) -> bool` functions compose into named pipelines and
   fixed-point runs. Analyses use explicit values and revision-aware caching;
   there is no pass or analysis class hierarchy.
4. Frontends transport source semantics without defining optimization policy.
   Semantic conversion lives in explicit bridge modules and is never triggered
   by parsing or loading.
5. Target modules can define data formats, legal computations, cost queries,
   reference execution, storage choices, and emitted artifacts without adding
   target cases to the core.
6. Module discovery, installation layout, dependency diagnostics, ABI checks,
   and source compatibility are documented and tested on a clean consumer
   project.
7. Official conventional neural-network models exercise importing, conversion,
   fusion, tensor/loop transforms, storage planning, and at least two genuinely
   different target modules. All results are deterministic and reproducible.

## Milestones

### M6 — types and symbols

- Replace opaque type strings internally with immutable structural type nodes.
- Resolve local, imported, qualified, overloaded, and generic function calls.
- Infer generic bindings from arguments and explicit parameters; propagate
  result and loop-element types; diagnose ambiguity and arity/type failures.
- Keep unknown external computation printable, but distinguish it explicitly
  from a resolved declaration.
- Let module-defined type constructors use the same lookup and compile-time
  value machinery as built-in types.

Exit gate: a module-defined parametric number format and nested tensor/list
signatures resolve without parser cases, while intentionally ambiguous and
incompatible programs fail with stable diagnostics.

### M7 — complete IR editing

Status: complete.

- Support multi-result calls and complete constant, call, `Blk`, loop, branch,
  return, and yield construction.
- Add nested walking, selective use replacement, cloning, moving, and `Blk`
  argument editing with explicit insertion points.
- Give every successful mutation a module revision and make compound edits
  atomic.
- Preserve readable bindings during printing without exposing generated SSA
  names.

Exit gate: one textual module and one C++ module independently build and rewrite
the same nested-loop kernel, including a multi-result transformation and a
forced rollback.

### M8 — composition and analyses

- Compose ordinary transform functions into pipelines without a pass class or
  new surface keyword.
- Provide bounded fixed-point execution, per-step diagnostics, change counts,
  timing hooks, and deterministic pipeline reports.
- Cache pure analysis function results by module revision and explicit inputs;
  mutation invalidates them automatically.
- Implement reusable constant folding, dead-call elimination, common
  subexpression elimination, canonicalization, and region fusion as modules.

Exit gate: pipelines can be assembled in source and through the embedding API,
produce identical results, and expose which step changed the module.

### M9 — distributable modules

Status: in progress. Deterministic local discovery, inspection, dependency and
native-binding validation, staged installation, collision rejection, and
validated removal are implemented without a registry or second manifest. The
installed package is exercised by a clean external CMake consumer. An
out-of-tree native module is also built against that package, installed through
the installed CLI, loaded, called, and removed. The explicit
compatibility-upgrade gate remains.

- Specify source, native library, tests, documentation, dependencies, and
  compatibility metadata in one module directory.
- Add deterministic discovery, inspection, validation, installation, and
  removal commands; loading remains explicit and side-effect free.
- Keep the exported native entry stable and evolve the size-tagged ABI by
  append-only fields until a deliberate major break.
- Test installed use from an external CMake consumer on supported platforms.

Exit gate: an out-of-tree module can be packaged, installed, discovered, run,
upgraded compatibly, and removed without editing or rebuilding Joggle.

### M10 — neural-network workflow

Status: in progress. Binary ONNX and TFLite transport, real-model generic
fusion, same-signature semantic bridging, progressive body expansion, and
structural tensor type construction are complete. Named ONNX dimensions now
reuse integer function generics and anonymous dynamic dimensions remain open
terms; a dependency-local codec test covers both without a downloaded model.
Open attributes now cover `Val` as well as `Fn` and `Op`, separating tensor
quantization, layout, and provenance from computation options. ONNX preserves
source value identity, and a dependency-local quantized TFLite Add gate proves
value metadata survives import and round-trip. A frontend-neutral `quant`
module now exposes scale, zero point, parameter axis, round-to-nearest-even,
saturation bounds, and element conversion as ordinary computation; it does not
turn raw integer arithmetic into implicit quantized semantics.
Grouped 2-D convolution now has an
inspectable body, and the explicit `onnx.nn` relation propagates all official
MobileNetV2 intermediate types before converting every compute node through
shared Conv, BatchNormalization, ReLU, Add, global-pool, and reshape semantics.
Average and maximum 2-D pooling now share dilation- and layout-explicit
semantics across ONNX and TFLite.
The complete converted ONNX model also expands one semantic body per compute
node and round-trips as ordinary nested IR. The second real frontend imports
the official TensorFlow Hub MobileNetV2 with reflection-driven options and no
core or reader operator switches. Its independent `.jog` bridge converts all
66 compute calls through explicit logical-axis operands and exposes every
shared body. Shared tensor functions now define trailing-axis broadcasting;
both frontend bridges validate against that relation and lower Add to the same
inspectable `nn.add` body; Sub and Mul now use the same path. ONNX Flatten and
rank-two-or-higher MatMul reuse `tensor.reshape` and `tensor.matmul`, while
Transpose reuses rank-generic `tensor.permute`, instead of adding frontend-
shaped semantics. `opt.unresolved`
reports six open source call
families before that bridge and none afterward. The two target gates remain.
Named extents now flow through broadcast binary operations, Flatten,
broadcast-batched MatMul, and Transpose using structural type terms.
Generic-list expansion
reuses caller dimension bindings, so those converted calls can expose their
normal tensor bodies without requiring a concrete batch size. Shape relations
cancel exact factors and retain anonymous dimensions when a runtime product is
not representable as one existing term; rank information no longer disappears
with that product.
QuantizeLinear and DequantizeLinear now propagate shape and element type, then
compatible three-input forms convert to generic `quant` bodies. All 258
quantizers and 652 of 653 dequantizers across three local QDQ vision models use
that path; one inconsistent imported result remains explicit. Conv
and pooling require concrete spatial arithmetic but preserve a symbolic batch
dimension; optional Conv bias is validated and reuses the existing composed
`nn.conv2d` body. This makes the compute path of a conventional QDQ ResNet
representable while keeping quantization policy explicit and inspectable.
Leading MatMul dimensions now use the shared broadcast relation and one
rank-generic tensor body, while the 2-D overload remains the compact case.
An axis-generic line-offset relation supports shared Softmax semantics; TFLite
materializes its last axis and ONNX converts an explicit normalized axis.
ONNX's version-dependent omitted-axis default remains open.
A multi-axis coordinate relation now supports an inspectable tensor mean body.
The ONNX relation normalizes negative axes, models `keepdims` structurally, and
retains duplicate or out-of-range axes. A symbolic multi-axis reduction expands
through the same generic body. The imported quantized BERT graph now exercises
all 50 `ReduceMean` nodes after shape and integer-quantized type propagation.
Their computation is converted to the shared multi-axis mean body while the
quantized operators themselves remain explicit frontier calls.
The shared library also covers broadcast division and power plus elementwise
square root, reciprocal, and hyperbolic tangent. A symbolic decomposed
normalization/GELU chain passes inference, conversion, body expansion, and
verification without introducing fused operator classes. Those paths now reach
every transformer layer in the imported BERT model.
The first shape-program mechanism is now present without a second IR. Textual
modules can traverse `Val -> Op`, inspect bounded byte constants, and derive
symbolic Reshape types through Shape/Gather/Slice/Unsqueeze/Squeeze/Concat/Cast
chains. Compatible tensor holes are refined without overwriting imported type
contracts; ConstantOfShape, OneHot, DynamicQuantizeLinear, MatMulInteger,
Split, and transposed batched MatMul complete structural propagation through a
12-layer quantized BERT graph. All 70 Reshape calls and 774 other covered
floating tensor calls convert through shared semantics. Quantized execution
semantics and vendor fused computation deliberately remain later module work.

- Keep binary codecs such as ONNX and TFLite separate from semantic bridge
  modules.
- Define reusable tensor and scalar computation libraries with function bodies
  where semantics can be expressed in Joggle and native references otherwise.
- Implement declarative source-to-library conversion, shape/type propagation,
  fusion, layout and storage transforms, and loop exposure.
- Allow target modules to select supported calls, attach costs, simulate exact
  behavior, and emit their chosen representation.

Exit gate: official models from two frontends pass through one shared semantic
library and run through at least two targets without core operator switches.

### M11 — research instrumentation

- Record pipeline decisions, costs, code size, memory use, and deterministic
  cycle estimates through module functions and stable structured output.
- Make cloud/edge partitioning, JIT specialization, custom formats, LUT/logic
  implementations, and WCET-oriented analysis independent research modules.
- Provide reproducible experiment manifests and artifact hashes without making
  any one research policy part of the compiler core.

Exit gate: a clean checkout reproduces at least one end-to-end co-design study,
including baselines, ablations, failure cases, and generated artifacts.

## Compatibility policy

- `module.jog` is the source of declarations; native code cannot replace its
  signatures.
- The C ABI carries version and byte size in data, never in exported names.
- Pre-1.0 source changes prefer one documented migration over permanent aliases.
- Canonical text is deterministic within a release. Round-trip and installed
  consumer tests guard changes to syntax, public headers, and module layout.
- Core remains C++20 plus the standard library. Optional dependencies belong to
  the modules that need them.

Each milestone lands as reviewable commits only after strict compiler warnings,
default offline tests, optional-module tests, sanitizers, failure rollback, and
the relevant real-model gate pass. A milestone is reopened when its abstraction
requires operator-, frontend-, or target-specific logic in the core.
