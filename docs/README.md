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
  reusable facilities required by transforms, analyses, and output functions.
- [Transform module](transform.md) documents direct typed-lambda semantic
  replacement for Function and Module values.
- [Research position](research-position.md) separates implemented evidence from
  the hypotheses, comparisons, and experiments required for publication.
- [Module design](modules.md) defines the extension and dependency model.
- [Tensor module](tensor.md) records the first target-independent AI
  vocabulary and its implemented evidence boundary.
- [Quant module](quant.md) records the ordinary-function QDQ boundary, its
  executable affine oracle, and the exact semantic trust boundary.
- [QDQ composite module](qdq.md) separates operator-independent quantization
  from transparent, source-bodied composite profiles and records the complete
  official-model transformation evidence.
- [Fusion module](fusion.md) documents the first installable,
  reference-bodied real-model transformation.
- [Bitpack module](bitpack.md) documents logical projection and the first
  checked cross-element physical format.
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
  contract; [Module bundle RFC](rfcs/0006-module-bundles.md) defines lossless
  persistence for Module-owned bytes without another ownership object; and
  [reference-bodied transformation RFC](rfcs/0007-reference-bodied-transformations.md)
  defines the semantic-correctness boundary and next composition gates for
  user kernels; [logical representation RFC](rfcs/0008-logical-representation.md)
  defines representation-changing equivalence and its current trust boundary;
  and [QDQ import RFC](rfcs/0009-qdq-import.md) defines quantization as an
  ordinary semantic Module and freezes its proof boundary.

The repository contains the compiler core, static tensor and quant semantic
Modules, and two narrow ONNX profiles: IR 3/opset 7 FLOAT and IR 7/opset 13
QDQ. It does not claim general ONNX coverage or a target backend; both audited
reference models have exact differential ONNX Runtime evidence.
