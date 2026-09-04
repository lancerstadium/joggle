# RFC 0008: Logical projection for physical representations

Status: reference codec gate implemented

## Problem

Exact definitional equivalence requires identical Function signatures. That is
correct for fusion, but it cannot validate a representation-changing transform:
a logical `tensor<i4, [1, 8]>` and one physical `u32` word do not have the same
Type even when they denote the same eight logical values.

Treating `pack` as an opaque cast would move correctness into a trusted name.
Adding a core Buffer, Layout, Memory, or Format class would create the second
extension hierarchy rejected by RFC 0001. A representation module instead
needs a narrow way to state which physical Type corresponds to which logical
Type while source bodies continue to explain changed computation.

## Accepted mechanism

The C++ equivalence primitive accepts an optional Type projection:

```cpp
using TypeProjection =
    std::function<std::optional<Type>(const Type&)>;

bool equivalent(Compiler&, const Function&, const Function&,
                const TypeProjection&, Diagnostics&, std::size_t limit = 256);
```

The projection is supplied by the representation Module's native compiler
function, not registered globally. It must be total for every observed Type
and idempotent: projecting a projected Type must return the same Type. The
equivalence checker applies it to argument, result, Known-value, callable, and
call-result Types before canonical comparison. Missing or non-idempotent
answers fail closed.

Projection changes only Type observation. Calls still compare by exact
declaration identity after bounded source-body expansion. Therefore a physical
operation is equivalent to its logical counterpart only when its ordinary
source body expands to that counterpart; a bodyless physical primitive does
not become trusted merely because its Types project.

## First consumer: `bitpack@1.0.0`

The Module defines:

```joggle
type integer(bits: int, signed: bool = false) {
  storage_bits: int = bits;
}

type packed(
  logical: type,
  storage: type,
  axis: int,
  lanes: int,
  order: string = "lsb"
);
```

`logical` and `storage` are complete `tensor.tensor` Types. This keeps logical
and physical shapes explicit without adding a tensor-core format parameter.
The native type verifier requires static equal-rank shapes, exact lane filling,
one changed axis, and `lsb` or `msb` ordering. The first implemented example
maps eight logical i4 values to one u32 storage element with no padding.

Format-aware `constant`, Conv, Relu, pooling, Concat, Reshape, and Softmax
functions are ordinary source-defined wrappers over the tensor vocabulary.
`bitpack.run` maps Types and calls with the existing `clone` primitive, then
publishes the result only after projected whole-Function equivalence succeeds.

## What this proves

- the core can carry a user-defined cross-element physical representation;
- a Module can change Function signatures and operation vocabulary without a
  target or datatype registry;
- the physical shape and bit-capacity relation is checked;
- the changed computation has a source-grounded logical meaning; and
- the transformation is transactional and fails when any Type, call, or proof
  is unsupported.

It does not prove a lossy f32-to-i4 quantizer, packed tensor arithmetic,
bit-accurate hardware execution, or performance. The Module now contains a
deterministic reference codec for complete, byte-aligned storage words. Exact
LSB/MSB and signed two's-complement vectors establish the concrete
representation independently of logical projection, but do not establish an
optimized implementation. Variant search remains deferred until at least one
physical implementation runs.

## Relation to prior systems

MLIR Quant types encode expressed and stored scalar values plus quantization
parameters, then commonly flatten to integer storage. TVM BYODT registers a
scalar type code and per-operation, per-target lowerings; its published scope
explicitly excludes block formats and custom-datatype hardware. Exo lets
libraries define memories and source-semantic instructions, but its paper
prototype trusts the instruction annotation link and uses a separate
scheduling API.

Joggle's narrower hypothesis is different: a normal user Type owns the
logical/physical relation, normal source functions own computational meaning,
and one reusable equivalence primitive composes them without a registry. The
current implementation establishes feasibility, not superiority.

## Gates

1. [x] Add fail-closed idempotent Type projection to definitional equivalence.
2. [x] Define and verify exact, padding-free packed tensors.
3. [x] Transform a multi-operation i4 Function to i4x8/u32 physical storage.
4. [x] Prove the complete representation-changing Function equivalent.
5. [x] Add bit-accurate pack/unpack reference execution and exhaustive i4
   domain vectors.
6. [x] Import and differentially execute the hash-pinned ONNX Model Zoo QDQ
   SqueezeNet through independent `quant` and `tensor` Modules.
7. [ ] Measure integration cost and compile-time scaling against a registry-
   based custom datatype path.
