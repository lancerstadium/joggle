# Documentation

The top level contains stable user and contributor references. `mods/`
documents shipped libraries; `design/` records accepted implementation
decisions.

## Start

- [Getting started](getting-started.md)
- [Language](language.md)
- [C++ API](api.md)

## Compiler model

- [Architecture](architecture.md)
- [IR](ir.md)
- [Staging](staging.md)
- [Mods](mods.md)
- [Repository](repository.md)

## Shipped mods

- [Tensor](mods/tensor.md)
- [Quantization](mods/quant.md)
- [Transform](mods/transform.md)
- [ONNX](mods/onnx.md)

## Design records

- [0001 — Language core](design/0001-language.md)
- [0002 — Fn values](design/0002-fns.md)
- [0003 — Expr rewriting](design/0003-rewrite.md)
- [0004 — Tensor semantics](design/0004-tensor.md)
- [0005 — ONNX import](design/0005-onnx.md)
- [0006 — Mod bundles](design/0006-bundles.md)
- [0007 — Definitional equivalence](design/0007-equivalence.md)
- [0008 — Quantized ONNX import](design/0008-quantization.md)

Design records explain why current interfaces exist. They are not a second
user manual or a migration log. [Research](research.md) separately tracks
unimplemented hypotheses and publication gates.
