# Modules

This directory contains the optional standard Modules shipped with Joggle.
Their public semantics are ordinary `.joggle` declarations rather than hidden
declaration kinds or test fixtures. The embedded language ABI lives separately
in `language/prelude.joggle`; Prelude is not an installable package.

- Native `i1/i8/i16/i32/i64`, `u8/u16/u32/u64`,
  `f16/bf16/f32/f64`, and `index` types are declarations in the automatically
  linked `prelude` Module. They need no explicit import.
- Compiler arithmetic, comparisons, logic, `ceildiv`, `min`, `max`, and
  integer `range` are ordinary Prelude `fn` declarations with deterministic
  Hermetic core implementations. Local declarations can shadow them through
  the normal name and operator rules.
- `arith` defines Residual computation over Prelude types and custom numeric
  types. `prelude.scalar` means fixed-width scalar representation;
  `prelude.numeric` means arithmetic is meaningful. The distinction excludes
  logical `i1` from numeric functions without fixing a target representation.
- `tensor` defines ranked and unranked tensor values plus structural functions.
  It imports `arith` only to reuse the single `arith.elementwise` function
  contract. It does not define neural-network operators or storage.
- `nn` defines common inference functions over `tensor.ranked`, including
  explicit NCHW convolution and pooling shape contracts. It does not define an
  import format, device, schedule, or storage mapping.
- `buffer` defines explicit storage values and token-ordered memory effects. It
  names an address space but does not prescribe devices, capacities, banks, or
  schedules.

The list is not an abstraction ladder. A function body may call declarations
from any installed Modules. Joggle has no built-in `lower` direction. A
conversion is an ordinary typed function in a bridge Module that imports the
vocabularies it connects. A bridge may own conversions in both directions
without changing either connected Module or the compiler core.

Input decoding, transformation, analysis, and output encoding use the same
function registration and invocation mechanism. Format-specific code therefore
belongs to Modules and their optional behavior libraries, never to parallel
subsystem hierarchies.

`prelude.scalar` declares the deterministic `storage_bits` field. Fixed-width
Prelude types implement it in `language/prelude.joggle`; a custom scalar
derives the same field from its own parameters. Target-sized `index`
intentionally does not implement this fixed-width interface, but it is numeric.
Likewise, `tensor.ranked_tensor` and `buffer.storage` expose element, shape, and
address-space facts without making either concrete type part of compiler core.

The complete stability and ownership rules are specified in
[`docs/standard-modules.md`](../docs/standard-modules.md).
