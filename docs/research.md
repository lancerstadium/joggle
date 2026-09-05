# Research scope

Joggle targets compiler researchers and AI hardware/software co-designers who
need to add semantic operators, transformations, data formats, and edge targets
without modifying a central operation hierarchy.

The current artifact is an implementation substrate, not yet a publication
claim.

## Problem

Extensible NN compilers commonly require one or more of:

- a frontend operation family followed by legalization;
- per-operation fusion categories or pattern registrations;
- separate graph and loop IR object models;
- operation interfaces for bufferization and target conversion;
- backend-specific subclasses and attribute channels.

These mechanisms are effective, but a user-defined semantic operator often
needs coordinated changes across import, inference, fusion, scheduling, and
bufferization. That cost is especially visible in research targets with custom
bit widths, data formats, or compute units.

## Hypothesis

A bodyful typed fn can be the common extension unit for semantic definition,
graph optimization, loop generation, and target replacement.

Joggle tests a specific version of that hypothesis:

1. A model graph is the use-def relation of an ordinary `Fn`.
2. A semantic operator is an ordinary bodyful fn built from a small tensor
   algebra.
3. Fusion derives coordinate demand and reductions from the body instead of an
   operator name, trait, or registered fusion pattern.
4. Fusion and explicit loops remain verified forms of the same `Fn` object.
5. Physical storage is introduced only after tensor optimization.
6. A target extension supplies ordinary types and transformation fns rather
   than inheriting a compiler base class.

The strongest prospective contribution is body-derived fusion for independently
defined operators. “One IR” or “modular compiler” alone is not novel enough.

## Relationship to existing systems

[TVM Relax](https://tvm.apache.org/docs/arch/fusion.html) groups dataflow calls
using operator pattern kinds and post-dominator analysis, then merges associated
TensorIR functions. Joggle should reuse the graph-partitioning insight while
deriving access categories from portable function bodies and retaining one Fn
object model.

[nncase](https://github.com/kendryte/nncase) demonstrates typed functional graph
rewriting, e-graphs, cost extraction, and explicit fusion functions. Joggle
should not reproduce its `Fusion` class or pair/complex fusion-rule hierarchy;
an outlined fused region, when needed, remains an ordinary Fn.

[MLIR One-Shot Bufferize](https://mlir.llvm.org/docs/Bufferization/) shows why
tensor fusion and tiling should precede buffer decisions and why whole-function
use-def analysis is necessary for safe reuse. Joggle therefore keeps
`tensor.set` functional and introduces no tensor-path effect token.

[Lift](https://lift-project.github.io/publications/2017/steuwer17LiftIR.pdf)
and RISE preserve functional data-parallel patterns so transformations can
compose before imperative code generation. Joggle adopts the functional
reasoning principle but uses ordinary typed calls and CFG loops in one
representation.

## Research questions

- **RQ1 — extension effort:** How many source and C++ changes are needed to add
  a new bodyful operator and make it inferable, fusible, and loopable?
- **RQ2 — coverage:** What fraction of operators in real ONNX edge models can
  be expressed by the tensor basis and processed without per-op compiler code?
- **RQ3 — fusion quality:** Compared with TVM and a no-fusion baseline, how much
  intermediate traffic, peak live tensor storage, and execution time does the
  deterministic planner remove?
- **RQ4 — predictability:** What are compilation-time variance, generated code
  size, and worst-case planner complexity on fixed models?
- **RQ5 — target reuse:** Can two materially different edge targets reuse the
  same semantic and fusion packages while implementing independent layout and
  emission fns?

## Required artifact milestones

1. Body-derived linear-chain fusion on MatMul–Relu — implemented.
2. Explicit reduction and output loops with tensor value semantics —
   implemented.
3. Shared-producer planning using post-dominance and a documented deterministic
   cost — planned.
4. Bodyful Conv, pooling, reshape, transpose, broadcast, and Softmax — planned.
5. End-to-end numerical execution for representative ONNX models — planned.
6. Two target packages, at least one custom-format edge target — planned.
7. Reproducible comparison with TVM/nncase-style registered fusion — planned.

## Publication gate

ASPLOS is plausible only after the work demonstrates a systems result across
real models and targets: extension-effort reduction, compilation predictability,
memory-traffic reduction, and end-to-end performance or energy evidence.

TACO is plausible if the central contribution becomes a precise tensor access
algebra and composition/planning algorithm with formal semantics, correctness
argument, and substantial evaluation.

Until those gates are met, documentation must describe the work as an
implemented compiler foundation and research hypothesis, not a novel or
top-tier result.

## Kill criteria

The research direction should be reconsidered if:

- common operators still require C++ name cases;
- body-derived summaries cover too little of real model graphs;
- the single-Fn representation makes loop or target transformations materially
  harder than a split graph/loop design;
- target packages require hidden registries or duplicated schemas;
- fusion quality is consistently worse than existing deterministic compilers
  without a compensating extension or predictability advantage.
