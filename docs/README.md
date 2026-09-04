# Documentation

The top level contains stable user and contributor references. `modules/`
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
- [Modules](modules.md)
- [Repository](repository.md)

## Shipped modules

- [Tensor](modules/tensor.md)
- [Quantization](modules/quant.md)
- [Transform](modules/transform.md)
- [ONNX](modules/onnx.md)

## Design records

- [0001 — Language core](design/0001-language.md)
- [0002 — Function values](design/0002-functions.md)
- [0003 — Expression rewriting](design/0003-rewrite.md)
- [0004 — Tensor semantics](design/0004-tensor.md)
- [0005 — ONNX import](design/0005-onnx.md)
- [0006 — Module bundles](design/0006-bundles.md)
- [0007 — Definitional equivalence](design/0007-equivalence.md)
- [0008 — Quantized ONNX import](design/0008-quantization.md)

Design records explain why current interfaces exist. They are not a second
user manual or a migration log. [Research](research.md) separately tracks
unimplemented hypotheses and publication gates.
