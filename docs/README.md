# Joggle documentation

Joggle has one current documentation set. Each document owns a distinct part
of the project; none is a migration log.

- [Getting started](getting-started.md) builds, checks, and runs one Module.
- [Architecture](architecture.md) defines the compiler's scope and public
  concepts.
- [Intermediate representation](ir.md) specifies Ops, SSA, properties,
  transformations, and Module-owned data.
- [Language](language.md) is the source-language reference.
- [Compiler functions](compiler-functions.md) defines composition and the
  reusable facilities required by transforms, analyses, and emitters.
- [Research position](research-position.md) separates implemented evidence from
  the hypotheses, comparisons, and experiments required for publication.
- [Module design](modules.md) defines the clean extension and target model.
- [Tensor module](tensor.md) records the first target-independent AI
  vocabulary and its implemented evidence boundary.
- [ONNX inference import](onnx.md) documents the optional, transactionally
  checked real-model importer and its exact support boundary.
- [C++ API](cpp-api.md) documents the in-process library surface.
- [Module repository](module-repository.md) specifies installation,
  resolution, and lock files.
- [Core language RFC](rfcs/0001-core-language.md) freezes the implementation
  gates; [callable values RFC](rfcs/0002-callable-values.md) defines typed
  lambdas without synthetic declarations; [expression replacement
  RFC](rfcs/0003-expression-replacement.md) defines typed matching and explicit
  effect safety without a pattern IR; [tensor RFC](rfcs/0004-tensor-module.md)
  defines the first real-model vertical slice; [ONNX inference
  RFC](rfcs/0005-onnx-inference-import.md) freezes the audited importer
  contract.

The repository contains the compiler core, the first static tensor semantic
Module, and a narrow ONNX IR 3/opset 7 inference importer. It does not claim
general ONNX coverage, differential runtime equivalence, or a target backend.
