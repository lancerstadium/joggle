---
title: Related systems and design boundaries
description: A source-grounded comparison of Joggle with adjacent compiler infrastructures, model compilers, scheduling languages, runtimes, and rewriting libraries.
---

# Related systems and design boundaries

Joggle overlaps several mature systems, but no single comparison captures its
scope. This page separates compiler infrastructure, model compilation, runtime
deployment, scheduling, and rewriting so that similarities do not become false
equivalences.

> [!NOTE]
> This page compares architecture and extension boundaries. Quantitative results
> belong to matched tasks, versions, targets, and workloads.

## Comparison dimensions

| Dimension | Question asked |
|---|---|
| Extension unit | What does a developer add or replace? |
| Transformation authoring | In what representation are analyses and edits expressed? |
| Organization | How are independently developed components named and composed? |
| Artifact boundary | Is deployment output explicit and inspectable? |
| Update model | What can be reused after an input or graph change? |

These dimensions reflect Joggle's three central concerns: unified
metaprogramming, graph-scoped organization, and dependency-aware updates.

## Systems map

| System | Primary documented abstraction | Transformation/control surface | Deployment emphasis | Closest point of comparison |
|---|---|---|---|---|
| MLIR | extensible operations grouped in dialects | passes, rewrite patterns, dialect conversion, Transform dialect | lowers toward target representations such as LLVM IR | extensible multi-level compiler infrastructure |
| TVM | `IRModule` with Relax and TensorIR functions | module transforms, schedules, MetaSchedule | optimized model/tensor programs across hardware | end-to-end model and tensor optimization |
| IREE | MLIR-based compiler plus runtime | staged dialect lowering and compiler pipelines | deployable runtime artifacts for heterogeneous targets | complete compilation/deployment stack |
| ONNX Runtime | ONNX graph plus execution providers | graph transformers and provider partitioning | production inference sessions | runtime optimization and hardware delegation |
| Halide | functional imaging algorithm plus separate schedule | scheduling directives and autoschedulers | compiled image/tensor pipelines | algorithm/schedule separation |
| egg | e-graph and rewrite system | equality saturation, analyses, extraction cost | library rather than deployment stack | extensible rewrite search |
| OpenXLA/XLA | StableHLO/HLO computations and backend pipelines | built-in analysis/optimization passes and backend code generation | native executable paths for supported architectures | closed linear-algebra operation set and end-to-end optimization |
| Glow | high-level graph IR lowered to a low-level instruction IR | graph lowering and backend-specific compilation | object code for accelerator backends | typed two-phase neural-network lowering |
| Triton | blocked programs for GPU kernels | Python kernel language plus compiler analysis | GPU kernels | productive custom-kernel authoring |
| Tensor Comprehensions | mathematical tensor comprehension DSL | polyhedral JIT compilation and autotuning | specialized CUDA kernels | concise operator definition plus search |
| Joggle | typed graph handles and graph-scoped `mod` packages | ordinary typed `.jog` functions for query, transform, conversion, and emission | artifact production is supplied by replaceable mods | one extension language across compiler roles |

## MLIR

[MLIR](https://arxiv.org/abs/2002.11054) is a reusable, extensible compiler
infrastructure organized around operations and dialects. Its ecosystem includes
pass management, declarative and imperative rewrites, dialect conversion, and
many target paths.

The [Transform dialect](https://mlir.llvm.org/docs/Dialects/Transform/)
represents fine-grained control in transform IR that operates on payload IR.
Its official documentation explicitly positions it as complementary to pass and
pattern infrastructure, not a replacement for them. Transform operations can be
extended and can track handles to payload operations.

### Relationship to Joggle

Both systems make compiler structure inspectable and support typed references to
program entities. The important boundary is authoring uniformity:

- MLIR intentionally offers several specialized extension mechanisms—dialects,
  operation definitions, patterns, passes, conversion, interfaces, and
  Transform dialect extensions.
- Joggle attempts to make operator bodies, analyses, selection policies,
  converters, and emitters ordinary typed functions in one source language.
- MLIR dialect nesting and symbol structure provide broad multi-level IR
  organization. Joggle's `mod` graph is a package/dependency unit with a
  different responsibility.

Extension-cost measurements use the same task and count the code, files, build
steps, and affected components in each system.

## TVM

[TVM](https://arxiv.org/abs/1802.04799) addresses end-to-end deep-learning
optimization, including graph-level fusion, tensor program optimization, and
hardware mapping. Current TVM documentation describes transformations as
functions from one `IRModule` to another and shows Relax and TensorIR functions
coexisting in a module; see the official
[`IRModule` tutorial](https://tvm.apache.org/docs/get_started/tutorials/ir_module.html)
and [architecture guide](https://tvm.apache.org/docs/arch/).

### Relationship to Joggle

TVM is the closest comparison for model/tensor compilation breadth. Joggle's
distinct design question is whether one typed metaprogramming surface can cover
more compiler roles while keeping each intermediate mod printable.

TVM transformations extend beyond operators. The relevant comparison axes are:

- how a new operator implementation and its selection policy are registered;
- how a project-specific analysis is packaged and passed into a transform;
- how many components and files a change touches;
- whether the same source-level abstraction also defines a converter/emitter;
- update cost when only a small part of the model or policy changes.

Generated-code comparisons match the target, tuning budget, operator coverage,
input shapes, and measurement protocol.

## IREE

[IREE](https://iree.dev/docs/developers/general/developer-overview/) is an
MLIR-based end-to-end compiler and runtime. Its main compiler driver consumes
supported inputs and produces deployable artifacts through staged compiler
pipelines. IREE exposes numerous MLIR dialects; its documentation notes that
these dialects are implementation details usable by plugins and advanced
integrations. Its compiler C API is organized around sessions, invocations,
sources, and outputs.

### Relationship to Joggle

IREE provides a complete compiler/runtime boundary and broad hardware
deployment. Joggle's core instead concentrates on compiler authoring and
organization; artifact support comes from installed mods.

The useful comparison is developer control:

- IREE exposes a production-oriented staged compiler built on MLIR.
- Joggle keeps preparation functions explicitly named in the user-selected
  sequence and makes the resulting mod printable between stages.
- Joggle's bundled artifact mods use the same function/mod mechanism and remain
  outside the core.

## ONNX Runtime

[ONNX Runtime's architecture](https://onnxruntime.ai/docs/reference/high-level-design.html)
loads an ONNX graph, applies provider-independent optimization, partitions it,
and assigns subgraphs to execution providers. Its
[optimization documentation](https://onnxruntime.ai/docs/performance/model-optimizations/graph-optimizations.html)
distinguishes basic, extended, and layout optimizations and supports online and
offline optimization. Execution providers expose device capabilities and run
assigned subgraphs; see the official
[Execution Providers guide](https://onnxruntime.ai/docs/execution-providers/).

### Relationship to Joggle

ONNX Runtime is principally a production inference runtime with an extensible
hardware-provider boundary. Joggle is principally a compiler workbench whose
intermediate representation and compiler functions are meant to stay visible.

The external-kernel `edge` example resembles provider capability matching only
at a high level. It is a typed source-mod adapter followed by an artifact mod,
not an ONNX Runtime execution provider. The common scoped task is adding one
custom kernel binding.

## Halide

[Halide](https://halide-lang.org/) separates an image-processing algorithm from
its schedule. The original work defines scheduling decisions such as tiling,
fusion, recomputation versus storage, vectorization, and parallelism separately
from the functional algorithm; see the
[SIGGRAPH paper](https://people.csail.mit.edu/jrk/halide12/halide12.pdf).

### Relationship to Joggle

Joggle shares the goal of separating meaning from implementation choice. The
`tensor`/`nn` semantic mods and the `ikj`/`locality` examples make that boundary
visible. The systems differ in scope and representation:

- Halide specializes in image/tensor pipelines with a dedicated scheduling
  model and mature code generation.
- Joggle treats scheduling policy as one kind of typed compiler function among
  analysis, conversion, and emission.
- Joggle's current loop transformation set is much smaller; uniformity is not a
  substitute for Halide's scheduling maturity.

## egg and equality saturation

[egg](https://arxiv.org/abs/2004.03082) is a high-performance e-graph library
for equality saturation. Users define a language, rewrites, analyses, and an
extraction cost; the e-graph compactly represents many equivalent expressions
before extraction chooses one.

### Relationship to Joggle

Both systems expose programmable analysis and rewrite policy, but their
execution models differ. Joggle performs verified graph edits and typed
implementation selection; it does not maintain a saturation e-graph. An e-graph
integration belongs in a native or external analysis/selection mod with explicit
ownership and verification boundaries.

## OpenXLA and XLA

The official [XLA architecture](https://openxla.org/xla/architecture) describes
a compiler that accepts StableHLO, performs target-independent HLO optimization
and memory analysis, then delegates target-specific optimization and code
generation to backends. StableHLO supplies a versioned portability layer and HLO
uses a deliberately selected linear-algebra operation set.

### Relationship to Joggle

XLA is important for generated-code and backend-pipeline comparisons, but it is
not the closest authoring-model comparison. Its objective is high-performance
compilation of supported linear algebra to machine instructions. Joggle's core
objective is a uniform way to author and organize compiler functionality.

- End-to-end measurements use graphs and hardware supported by both systems.
- Extension measurements distinguish an HLO/backend feature from a project
  `mod`; these change different layers.
- XLA includes native code generation and device runtimes; Joggle's current core
  does not.

## Glow

[Glow](https://arxiv.org/abs/1805.00907) presents graph-lowering compiler
techniques for neural networks and lowers a high-level dataflow graph into a
two-phase strongly typed intermediate representation. Its architecture is a
useful historical comparison for separating high-level neural-network meaning
from lower-level instruction-oriented compilation.

### Relationship to Joggle

Glow uses explicit lowering and strong typing. Joggle expresses representation
changes as functions and mods inside one extensible language. Joggle graphs can
contain different structural vocabularies, while Glow's two levels carry
deliberate optimization responsibilities. The concrete comparison traces where
a new operation, lowering, backend rule, and diagnostic are implemented.

## Triton

[Triton](https://triton-lang.org/) is a language and compiler for parallel
programming aimed at productive custom DNN kernels on modern GPUs. Its
programming model represents blocked programs and relies on compiler data-flow
analysis to schedule work; see the official
[programming-model introduction](https://triton-lang.org/main/programming-guide/chapter-1/introduction.html).

### Relationship to Joggle

Triton and Joggle operate at different layers. Triton is a candidate kernel
implementation technology; Joggle is a compiler-extension and organization
mechanism. A Joggle mod can select or emit calls to Triton-generated kernels
without making Triton part of the core. Kernel throughput measures the selected
implementation; extension cost measures the `mod` integration.

## Tensor Comprehensions

[Tensor Comprehensions](https://arxiv.org/abs/1802.04730) combines a concise
mathematical tensor DSL with polyhedral JIT compilation, specialization, a
compilation cache, and autotuning for CUDA kernels.

### Relationship to Joggle

It covers concise operator definition and JIT specialization. The boundary with
Joggle is explicit:

- Tensor Comprehensions focuses on synthesizing and tuning a kernel from a
  tensor expression.
- Joggle's current evaluator executes compiler functions and caches evaluation
  plans; that is not a machine-code JIT.
- Joggle's evaluator is not a machine-code JIT because it does not generate and
  link native code.

The measurement dimensions are authoring effort, compiler-function update
latency, kernel compilation latency, tuning budget, and generated-kernel runtime.

## Joggle's three defining properties

Joggle combines three implemented properties:

1. **Uniform authoring surface.** Ordinary typed `.jog` functions can define and
   compose operator bodies, graph queries, rewrite policies, converters, and
   emitters.
2. **Graph-scoped package organization.** `mod` and `use` form explicit
   dependency boundaries for independently developed compiler functionality.
3. **Observed-dependency reuse.** Query caches and reactive schedules validate
   reuse against revisions and dependencies rather than assuming an unchanged
   whole pipeline.

## Quantitative comparison matrix

| Dimension | Measurement | Controls |
|---|---|---|
| extension effort | completion, errors, files, tokens, and components changed across matched tasks | same specification and acceptance tests |
| change scope | repository-level touched regions across representative extensions | normalized generated/vendor files |
| incremental update | cold and warm latency across whole-model suites and localized edits | identical machine, cache state, and correctness checks |
| generated program | operator and model runtime plus memory | identical inputs, target, compiler flags, and tuning budget |
| small-model assistance | syntax-valid, type-valid, semantics-valid, and task-complete rates | frozen prompts, models, sampling settings, and test oracle |

## How to use this page

- If implementing a feature, use the comparisons to locate the correct Joggle
  ownership boundary.
- For quantitative evaluation, pin every system version and apply the controls
  in the comparison matrix.

The project architecture remains documented independently in
[Compiler internals](../compiler/index.md), [Unified metaprogramming](../metaprogramming/index.md),
and [Performance internals](../performance/index.md).
