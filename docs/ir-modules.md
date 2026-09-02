# IR modules

Joggle has one runtime program representation, `Graph`, and many installable
IR Modules. A Module is the namespace, schema, version, package, and extension
boundary; it is not a second graph container. Different abstraction levels can
therefore coexist in one Graph and a pass may replace only the operations it
owns.

The shipped Modules are integration fixtures and starting points, not a
mandatory standard library:

| Module | Owns | Does not own |
|---|---|---|
| `arith` | scalar-format interface, integer type, elementary arithmetic | tensors, devices, target costs |
| `tensor` | ranked tensor type, dense constants, reshape | neural-network operators, schedules |
| `nn` | `linear`, `relu`, structural canonicalization | tensor storage, target instructions |
| `fixed` | a third-party fixed-point format and codec | compiler-wide numeric policy |
| `edgevec` | target operations, target metadata, NN-to-target lowering | core device or resource model |
| `mlp` | graph members using the Modules above | reusable declarations or behavior |

This division follows two established compiler principles without cloning
either implementation. MLIR keeps reusable types and operations in narrowly
scoped dialects, while TVM permits high- and low-level functions to coexist in
one IRModule for cross-level transformation. Joggle keeps the first principle
and uses one Graph rather than introducing fixed high/low function classes.

## Extension rules

1. Name a Module after the abstraction it defines, not after a demo or target
   application.
2. Put structural contracts in `.joggle`; use C++ behavior only for nonlinear
   checks, interface methods, or transformations the text rule form cannot
   express.
3. Import only declarations referenced by the Module source. A host may load a
   target Module beside a model and explicitly select its pass without adding a
   target dependency to the model.
4. Keep target facts in target Modules. Core `Graph`, `Compiler`, and package
   APIs do not know lane counts, storage capacities, instruction sets, or
   scheduling vocabularies.
5. Treat a Module's canonical source and semantic version as its public schema.
   C++ behavior binds to the exact content identity and cannot mutate it.

The names intentionally resemble familiar compiler vocabulary, but Joggle
does not claim source or semantic compatibility with MLIR dialects or TVM IR.
Its interoperability boundary is an explicit importer/exporter or lowering
pass, implemented as an extension rather than hidden in the core.

## External references

- [MLIR dialect documentation](https://mlir.llvm.org/docs/Dialects/)
- [MLIR Builtin dialect](https://mlir.llvm.org/docs/Dialects/Builtin/)
- [MLIR Arith dialect](https://mlir.llvm.org/docs/Dialects/ArithOps/)
- [Apache TVM architecture](https://tvm.apache.org/docs/arch/index.html)
- [Apache TVM Relax](https://tvm.apache.org/docs/deep_dive/relax/index.html)
- [Apache TVM TensorIR](https://tvm.apache.org/docs/deep_dive/tensor_ir/index.html)
