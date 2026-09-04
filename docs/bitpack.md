# Bitpack module

`bitpack@1.0.0` is an installable physical-format experiment. It separates
three things that are often conflated:

- `bitpack.integer<bits, signed>` is the logical integer value format;
- `bitpack.packed<logical, storage, axis, lanes, order>` describes how logical
  tensor elements occupy a physical tensor; and
- format-aware source functions state logical computation independently of a
  later encoder, instruction, or code emitter.

For example, eight logical i4 values in `[1,8]` may occupy one u32 word:

```joggle
bp.packed<
  tensor<bp.integer<4>, [1, 8]>,
  tensor<u32, [1, 1]>,
  1,
  8,
  "lsb"
>
```

Both tensor Types are explicit. The verifier rejects mismatched ranks,
non-static shapes, lane widths that do not exactly fill a storage element,
changes outside the packing axis, and unknown bit order. Padding is not part of
version 1.

## Transformation

```joggle
fn pack(input: function) -> function {
  return @bitpack.run(input, u32, 1, "lsb");
}
```

`run` maps every tensor value to a checked `packed` Type and each supported
tensor call to the corresponding source-defined bitpack call. Unsupported
element widths, shapes, or operations reject the whole transform. Before the
new Function is returned, the Module projects every packed Type to its
`logical` field and runs whole-Function definitional equivalence.

The implemented test transforms two `[1,8]` i4 inputs and a `[1,16]` result to
physical u32 shapes `[1,1]`, `[1,1]`, and `[1,2]`; two Relu calls and one Concat
become bitpack calls while preserving logical behavior.

## Reference codec

The Module also exposes ordinary compiler functions for concrete format
evidence:

```joggle
encoded = @bitpack.encode(values, bitpack.integer<4>, u32, "lsb");
decoded = @bitpack.decode(encoded, bitpack.integer<4>, u32, "lsb");
```

Version 1 encodes complete storage words only. Storage widths are byte-aligned
and at most 64 bits. Because the compiler's `int` domain is signed i64, signed
elements may use 64 bits and unsigned elements at most 63 bits. Each lane uses
the low `bits` of its two's-complement representation; unsigned and signed
inputs are range checked. `lsb` places the first logical lane in the
least-significant field, while `msb` places it in the most-significant field.
In both cases, the completed storage word is serialized least-significant byte
first. Thus unsigned values `[0,1,2,3,4,5,6,7]` encode as bytes
`10 32 54 76` for i4x8/u32 `lsb`, and `67 45 23 01` for `msb`.

These functions are a deterministic compiler-side oracle for formats and test
vectors. They do not claim that a transformed tensor Function can execute
packed arithmetic.

## Current boundary

The module currently supplies reference semantics, compiler-time
representation transformation, and a bit-accurate reference codec. It does
not yet execute packed tensor arithmetic, quantize floating point, or emit
hardware code. In particular, logical projection is not permission to treat
arbitrary physical primitives as equivalent: opaque calls still require exact
identity. See [RFC 0008](rfcs/0008-logical-representation.md) for the formal
boundary.
