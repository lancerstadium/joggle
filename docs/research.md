# Research scope

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

1. one `Mod` owns declarations, editable CFG/SSA Fns, imports, and
   content-addressed data;
2. `@` is the only stage switch, while ordinary calls always remain program
   calls;
3. typed capture-free lambdas are ordinary anonymous Fns rather than a
   pattern AST;
4. typed expression replacement is atomic, rejects unsafe data-flow and effect
   boundaries, and now has a bounded definitional-equivalence overload for
   source-bodied fns;
5. a target-independent tensor Mod and an optional ONNX Mod import the
   exact Model Zoo SqueezeNet 1.1 graph;
6. the imported Fn reconstructs an ONNX graph whose deterministic
   ONNX Runtime output is bit-identical to the original model;
7. a second hash-pinned Model Zoo model exercises standard QDQ inference: its
   228 f32/u8/i8/i32 constants, 130 affine quantization boundaries, and 41
   tensor calls import without a vendor operation, and reconstruction through
   ONNX Runtime is exactly equal (`max_abs=0`, `mean_abs=0`);
8. `quant@2` is a source-only semantic Mod: the 130 QDQ calls require no
   native operation bindings, while whole-graph ONNX Runtime reconstruction
   checks the imported numerical behavior;
9. typed replacement preserves pure shared DAG ancestors instead of rejecting
   or duplicating them, while rollback and exact repeated-hole equality remain
   checked;
10. the installable `transform` Mod exposes equivalence-checked replacement
    directly as ordinary Fn and Mod overloads taking typed lambdas;
11. Mod bundles preserve and verify all imported data through public
    `check`, `run`, `install`, and `lock` workflows.
12. `transform.resolve` constructs deterministic concrete source instances and
    preserves bodyless calls as an explicit leaf set without invoking them.
13. On the hash-pinned FLOAT SqueezeNet graph, an ordinary source fn factors a
    concrete Conv-Relu expression through the same `transform.replace`
    service, and `transform.resolve` closes the result to one local source
    instance plus the original 117 bodyless tensor leaves. The pass adds no
    operator binding and the complete Mod still verifies.

These are infrastructure results. The real-model factor is deliberately
monomorphic: one lambda signature names one concrete tensor shape. The tensor
and QDQ leaves remain opaque program semantics, and no emitted kernel exists
yet. The result does not establish shape-polymorphic matching, general
mathematical equivalence of user rewrites, support for a physical format at
run time, or publication-level novelty.

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

## Hypothesis: one fn model across abstraction levels

The next mechanism under test is deliberately smaller than another IR stack:

1. A model, user operation, or executable kernel is an ordinary typed `fn`.
2. Fn bodies may use different imported vocabularies, but remain the
   same `Fn` representation and type system. No `Kernel` subclass or
   fixed lowering ladder is added.
3. A conversion or optimization is an explicitly staged fn over
   `fn` or `mod` values.
4. A portable reference and an implementation are separate ordinary
   fns. Their relationship must be checked explicitly; a declaration
   never carries a hidden second body.
5. Definitional equivalence remains the safe checker for transparent
   factoring. Real kernels require stronger, independently auditable evidence
   rather than weakening that checker.
6. Physical formats are parameterized types and conversion fns, not
   copies of the tensor operator vocabulary.
7. Candidate enumeration, measurement, and selection are ordinary compiler
   fns. Selection policy does not alter transformation correctness.

Once a concrete implementation Fn is chosen, emission must not silently
make new optimization decisions. This preserves the useful separation in
RISE/Shine while keeping the extension surface uniform.

## Why this is not yet a contribution

One Fn representation may be too weak for both model graphs and useful
kernel bodies, or may merely move a privileged registry into native fns.
User formats may still require compiler changes to represent storage. A
model-wide optimization may not compose from kernel-local equivalences. These
are open risks, not details to hide in implementation.

## Required experiments

The hypothesis survives only if the same mechanism supports all of the
following without a new core declaration category:

1. define an executable tensor kernel whose implementation structure differs
   from its portable reference expression;
2. reject a type-correct but semantically different replacement;
3. compose that kernel transformation with a model-scale structural pass;
4. only after the kernel path works, add one independently installable low-bit
   or packed format and its conversions without duplicating tensor fns;
5. import and transform externally maintained ONNX models, then compare
   numerically with a trusted runtime;
6. produce and execute at least one useful edge implementation;
7. record extension-local versus core changes against Exo and one established
   AI compiler; and
8. measure latency, memory, binary size, compile/selection cost, deterministic
   rebuilds, and diagnostic coverage.

A later cloud/edge experiment may enumerate and benchmark variants away from
the constrained device, then lock the selected Fn and measurement record
into a content-addressed Mod bundle. Online selection is optional; repeated
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
- [Lift: A Fnal Data-Parallel IR](https://lift-project.github.io/publications/2017/steuwer17LiftIR.pdf)
- [Achieving High Performance the Fnal Way](https://michel-steuwer.github.io/files/publications/2020/ICFP-2020.pdf)
- [Exocompilation for Productive Programming of Hardware Accelerators](https://people.csail.mit.edu/yuka/pdf/exo_pldi2022_full.pdf)
- [TensorIR: An Abstraction for Automatic Tensorized Program Optimization](https://arxiv.org/abs/2207.04296)
- [TileLang: Bridge Programmability and Performance in Modern Neural Kernels](https://proceedings.iclr.cc/paper_files/paper/2026/hash/76fb92288bf90360c527efb0d1c2aba6-Abstract-Conference.html)
- [Astra: Exploiting Predictability to Optimize Deep Learning](https://www.microsoft.com/en-us/research/publication/astra-exploiting-predictability-to-optimize-deep-learning/)
- [MLIR Quant dialect](https://mlir.llvm.org/docs/Dialects/QuantDialect/)
- [TVM Bring Your Own Datatypes](https://tvm.apache.org/2020/09/26/bring-your-own-datatypes)
