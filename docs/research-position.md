# Research position

This is an evidence ledger, not a paper draft. It separates mechanisms already
present in Joggle from the next research hypothesis and its falsification
criteria.

## Question

Can an AI software/hardware co-design compiler let a library author add a
semantic operation, an optimized kernel, a physical format, and the
transformations connecting them through one typed `type`/`fn` extension plane,
without moving correctness or optimization policy into a fixed compiler
registry?

The scope is edge inference: reproducible compilation, controllable kernel
implementation, low integration cost for unusual formats and instructions,
and an optional measured-selection loop whose chosen program can be frozen for
deterministic deployment.

## Evidence in the current tree

The implementation already demonstrates:

1. one `Module` owns declarations, editable CFG/SSA Functions, imports, and
   content-addressed data;
2. `@call` is the only stage switch, while ordinary calls always remain program
   calls;
3. typed capture-free lambdas are ordinary anonymous Functions rather than a
   pattern AST;
4. typed expression replacement is atomic, rejects unsafe data-flow and effect
   boundaries, and now has a bounded definitional-equivalence overload for
   source-bodied functions;
5. a target-independent tensor Module and an optional ONNX Module import the
   exact Model Zoo SqueezeNet 1.1 graph;
6. the imported Function reconstructs an ONNX graph whose deterministic
   ONNX Runtime output is bit-identical to the original model;
7. an installable `fusion` Module gives a generic Conv/ReLU function its
   portable source meaning, composes `@onnx.read` and `@fusion.run` in source,
   and safely replaces all 26 pairs in the official model;
8. the complete 117-call source Function and 91-call transformed Function are
   definitionally equivalent, with shared-DAG normalization memoized by the
   existing `Value` identities; and
9. `bitpack` maps an i4 tensor Function to exact i4x8/u32 physical shapes and
   format-aware source functions, then proves whole-Function equivalence under
   an idempotent logical Type projection; and
10. Module bundles preserve and verify all imported data through public
   `check`, `run`, `install`, and `lock` workflows.

These are infrastructure results. They do not establish general mathematical
equivalence of user rewrites, competitive kernels, support for a physical
format at run time, or publication-level novelty.

## What the closest systems already solve

Joggle must not claim novelty for user-controlled scheduling alone.

- Halide separates an algorithm from a schedule, but schedules select from a
  compiler-owned set of controls.
- TVM and TensorIR provide mature graph/tensor optimization, schedule
  primitives, blocks with dependency signatures, tensor intrinsics, validation,
  and automatic search.
- Lift and RISE preserve functional data-parallel semantics while rewrite rules
  choose low-level parallel patterns; ELEVATE makes rewrite strategies
  programmable and composable.
- Exo externalizes instructions, memories, configuration state, and scheduling
  policy into user libraries. Its effect analysis and SMT checks make
  imperative rewrites safe. This is the nearest mechanism to the proposed
  work.
- TileLang gives kernel authors concise tile-level control over placement,
  movement, thread mapping, layout, tensorization, and pipelines.
- Astra separates legal-variant enumeration from measured selection across a
  repetitive DNN workload, avoiding a monolithic performance model.
- MLIR Quant represents expressed/storage scalar relationships and
  quantization parameters; TVM BYODT uses runtime scalar type and lowering
  registries and explicitly excludes block formats in its published scope.

Their relevant boundaries are also precise. Lift/RISE use a fixed family of
low-level hardware patterns. TensorIR's block signatures and transformations
are compiler abstractions. Exo targets affine imperative library kernels and
its paper prototype uses syntactic locations; it does not unify model-scale AI
semantics, user-defined value formats, and kernel transformations in one
source-level mechanism. Astra targets long-running training and cannot be
transferred to resource-constrained inference without a bounded, cacheable
selection protocol.

## Hypothesis: reference-bodied extensions

The next mechanism under test is a reference-bodied extension:

1. A user operation or optimized kernel is an ordinary typed `fn`.
2. Its source body is its portable reference semantics, expressed using other
   ordinary functions. No operation schema, implementation interface, kernel
   class, or target registration is added.
3. A transformation remains an explicitly staged function over `function` or
   `module` values.
4. A semantic replacement is accepted only when normalizing source bodies
   proves the before and after expressions identical. Matching types and
   preserving effects are necessary but not sufficient.
5. Structural transformations are a separate class only internally: each
   primitive must preserve a stated Function invariant for all inputs and be
   independently checked. They are still exposed as ordinary functions.
6. Selection policy composes transformations; it does not change their
   correctness. Later, a compile-time function may enumerate equivalent
   Function values and another function may measure or choose among them.

This makes the semantic source body the common seam between a model operation,
a fused kernel, and eventually a format conversion. It also keeps the key
property of RISE/Shine: once a concrete implementation program is chosen,
emission must not silently make new optimization decisions.

## Why this is not yet a contribution

Reference-body normalization may be too weak for important kernels, may expand
programs excessively, or may merely move a privileged registry into native
functions. User formats may still require compiler changes to represent
storage. A model-wide optimization may not compose from kernel-local
equivalences. These are open risks, not details to hide in implementation.

## Required experiments

The hypothesis survives only if the same mechanism supports all of the
following without a new core declaration category:

1. define a fused tensor kernel with a portable source body;
2. reject a type-correct but semantically different replacement;
3. compose fusion with at least one structural or layout transformation;
4. add one independently installable low-bit or packed format and its
   conversions;
5. import and transform externally maintained ONNX models, then compare
   numerically with a trusted runtime;
6. produce and execute at least one useful edge implementation;
7. record extension-local versus core changes against Exo and one established
   AI compiler; and
8. measure latency, memory, binary size, compile/selection cost, deterministic
   rebuilds, and diagnostic coverage.

A later cloud/edge experiment may enumerate and benchmark variants away from
the constrained device, then lock the selected Function and measurement record
into a content-addressed Module bundle. Online selection is optional; repeated
deployment of the selected bundle must not require search.

## Kill criteria

Narrow or abandon the mechanism if realistic extensions repeatedly require
new core declaration kinds, if semantic checking accepts unsound rewrites or
rejects most useful kernels, if user-defined formats cannot be represented
without a hidden target API, if whole-model composition is impractical, or if
the controlled extension study shows no meaningful reduction in integration
coupling.

## Venue gate

ASPLOS is plausible only after the mechanism has measured compiler/runtime and
hardware consequences on real edge inference. TACO additionally requires
substantial architecture and code-optimization evidence. A language surface,
an importer, or a synthetic emitter is insufficient for either venue.

## Primary literature

- [Halide: Decoupling Algorithms from Schedules](https://people.csail.mit.edu/jrk/halide12/)
- [Lift: A Functional Data-Parallel IR](https://lift-project.github.io/publications/2017/steuwer17LiftIR.pdf)
- [Achieving High Performance the Functional Way](https://michel-steuwer.github.io/files/publications/2020/ICFP-2020.pdf)
- [Exocompilation for Productive Programming of Hardware Accelerators](https://people.csail.mit.edu/yuka/pdf/exo_pldi2022_full.pdf)
- [TensorIR: An Abstraction for Automatic Tensorized Program Optimization](https://arxiv.org/abs/2207.04296)
- [TileLang: Bridge Programmability and Performance in Modern Neural Kernels](https://proceedings.iclr.cc/paper_files/paper/2026/hash/76fb92288bf90360c527efb0d1c2aba6-Abstract-Conference.html)
- [Astra: Exploiting Predictability to Optimize Deep Learning](https://www.microsoft.com/en-us/research/publication/astra-exploiting-predictability-to-optimize-deep-learning/)
- [MLIR Quant dialect](https://mlir.llvm.org/docs/Dialects/QuantDialect/)
- [TVM Bring Your Own Datatypes](https://tvm.apache.org/2020/09/26/bring-your-own-datatypes)
