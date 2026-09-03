# Joggle documentation

Joggle has one current documentation set. Each document owns a distinct part
of the project; none is a migration log.

- [Getting started](getting-started.md) builds, checks, and runs one Module.
- [Architecture](architecture.md) defines the compiler's scope and public
  concepts.
- [Intermediate representation](ir.md) specifies Ops, SSA, properties,
  transformations, and Module-owned data.
- [Language](language.md) is the source-language reference.
- [Standard Modules](standard-modules.md) specifies Prelude and the shipped
  `arith`, `tensor`, `nn`, and `buffer` vocabularies.
- [Compiler functions](compiler-functions.md) defines composition and the
  reusable facilities required by transforms, analyses, and emitters.
- [Modules and native behavior](modules.md) covers packages, C++ behavior, and compiler
  functions.
- [C++ API](cpp-api.md) documents the in-process library surface.
- [Module repository](module-repository.md) specifies installation,
  resolution, and lock files.
- [ONNX Module](../modules/onnx/README.md) documents source-preserving import
  and explicit conversion to NN IR.
- [Precision Module](../modules/precision/README.md) documents the first
  representation-changing transformation over Module-owned data.

The checked-in declarations in [`modules`](../modules) and executable examples
in [`examples`](../examples) are part of the reference. If prose and behavior
disagree, tests and public headers describe the implemented release; please
report the documentation mismatch.
