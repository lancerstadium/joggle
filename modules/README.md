# Modules

This directory is the single source tree for Modules shipped with Joggle.
Their public semantics are ordinary `.joggle` declarations rather than hidden
declaration kinds or test fixtures. The embedded language ABI lives separately
in `language/prelude.joggle`; Prelude is not an installable Module.

- Native `i1/i8/i16/i32/i64`, `u8/u16/u32/u64`,
  `f16/bf16/f32/f64`, and `index` types are declarations in the automatically
  linked `prelude` Module. They need no explicit import.
- Compiler arithmetic, comparisons, logic, `ceildiv`, `min`, `max`, integer
  `range`, and generic list access are ordinary Prelude `fn` declarations with
  deterministic Hermetic core implementations. Local declarations can shadow
  them through the normal name and operator rules.
- `arith` defines Residual arithmetic, comparison, bitwise, shift, and logical
  functions over Prelude types and compatible custom types. `prelude.scalar`
  means fixed-width scalar representation; `prelude.numeric` means arithmetic
  is meaningful. The distinction excludes logical `i1` from numeric functions
  without fixing a target representation.
- `tensor` defines ranked and unranked tensor values plus structural functions.
  `tensor.immutable_data` lets generic transformations recognize constants
  independently of the Module that produced them.
- `nn` defines common inference functions over `tensor.ranked`, including
  explicit NCHW convolution and pooling shape contracts. It does not define an
  import format, device, schedule, or storage mapping.
- `mem` defines extensible reference, layout, address-space, alias, and effect
  contracts. User-defined reference types participate without inheriting a C++
  class or adopting a fixed device model.
- `onnx` preserves a bounded ONNX source vocabulary and provides the explicit
  `onnx.to_nn` conversion. Its native behavior is optional.
- `precision` supplies a representation-changing compiler function over the
  same Module IR and content-addressed data.
- `anchor` demonstrates a user-owned target vocabulary, tensor-to-
  reference mapping, explicit layout/address-space types, deterministic static
  placement, and validated scratch analysis without adding a target
  abstraction to compiler core.

The list is not an abstraction ladder. A function body may call declarations
from any installed Modules. Joggle has no built-in `lower` direction. A
conversion is an ordinary typed function in a bridge Module that imports the
vocabularies it connects. A bridge may own conversions in both directions
without changing either connected Module or the compiler core.

Every Module follows one package layout:

```text
modules/<module_name>/
  module.joggle
  README.md              # when the Module needs its own guide
  CMakeLists.txt         # only when native code is present
  src/                   # private native implementation
  tests/                 # Module-local tests
```

The snake_case directory name must equal the name declared by
`module.joggle`; CMake rejects a mismatch. Source-only Modules contain no
placeholder build files. Native Modules are selected together with
`JOGGLE_BUILD_MODULES`, so adding one never adds another root-level option.
There are no parallel `extensions`, `passes`, or `targets` package trees.

Input decoding, transformation, analysis, and output encoding use the same
function registration and invocation mechanism. Format-specific code therefore
belongs to Modules and their optional behavior libraries, never to parallel
subsystem hierarchies.

`prelude.scalar` declares the deterministic `storage_bits` field. Fixed-width
Prelude types implement it in `language/prelude.joggle`; a custom scalar
derives the same field from its own parameters. Target-sized `index`
intentionally does not implement this fixed-width interface, but it is numeric.
Likewise, `tensor.ranked_tensor` and `mem.reference` expose semantic facts
without making either concrete type part of compiler core.

The complete stability and ownership rules are specified in
[`docs/standard-modules.md`](../docs/standard-modules.md).
