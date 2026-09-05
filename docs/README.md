# Documentation

The documentation is normative unless a page explicitly labels work as
planned. Historical design records were removed because they described
abandoned APIs and made the implemented system ambiguous.

## Read first

1. [Getting started](getting-started.md) runs the MatMul–Relu compiler path.
2. [Architecture](architecture.md) defines the object model and ownership
   boundaries.
3. [Tensor pipeline](pipeline.md) specifies fusion and loop expansion.
4. [Language](language.md) is the textual language reference.

## Reference

- [IR](ir.md): `Fn`, `Blk`, `Op`, `Val`, editing, and verification.
- [C++ API](api.md): embedding and native-package interfaces.
- [Staging](staging.md): ordinary calls versus explicit `@` execution.
- [Packages](mods.md): when a new `Mod` is justified.
- [Repository](repository.md): installation, bundles, identities, and locks.
- [Research scope](research.md): hypothesis, measurements, and publication
  gates.

## Shipped packages

- [Arith](mods/arith.md)
- [Tensor](mods/tensor.md)
- [Neural network](mods/nn.md)
- [Transform](mods/transform.md)
- [Quantization](mods/quant.md)
- [ONNX](mods/onnx.md)
