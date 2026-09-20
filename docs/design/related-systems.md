---
title: Related systems and design boundaries
description: A source-grounded comparison of Joggle with adjacent compiler infrastructures, model compilers, scheduling languages, runtimes, and rewriting libraries.
---

# Related systems and design boundaries

Joggle overlaps several mature systems, but no single comparison captures its
scope. This page separates compiler infrastructure, model compilation, runtime
deployment, scheduling, and rewriting so that similarities do not become false
equivalences.

> [!WARNING]
> This is an architectural comparison, not an experimental result. It does not
> establish that Joggle is faster, easier, or more scalable. Those statements
> require controlled measurements with matched tasks and versions.

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
| Joggle | typed graph handles and graph-scoped `mod` packages | ordinary typed `.jog` functions for query, transform, conversion, and emission | explicit C/VM preparation and emission | one extension language across compiler roles |

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
  organization. Joggle's `mod` graph is a package/dependency unit rather than a
  claim to replace MLIR's representational depth.

An honest evaluation should compare the code and files needed for the same
extension task, not count framework concepts in isolation.

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

Do not imply that TVM transformations are limited to operators. A defensible
comparison instead measures:

- how a new operator implementation and its selection policy are registered;
- how a project-specific analysis is packaged and passed into a transform;
- how many components and files a change touches;
- whether the same source-level abstraction also defines a converter/emitter;
- update cost when only a small part of the model or policy changes.

Performance comparisons must match generated code, target, tuning budget, and
operator coverage. TVM is a mature optimizing compiler; Joggle's organizational
claims do not imply generated-code superiority.

## IREE

[IREE](https://iree.dev/docs/developers/general/developer-overview/) is an
MLIR-based end-to-end compiler and runtime. Its main compiler driver consumes
supported inputs and produces deployable artifacts through staged compiler
pipelines. IREE exposes numerous MLIR dialects; its documentation notes that
these dialects are implementation details usable by plugins and advanced
integrations. Its compiler C API is organized around sessions, invocations,
sources, and outputs.

### Relationship to Joggle

IREE demonstrates the value of a complete compiler/runtime boundary and broad
hardware deployment. Joggle currently has a smaller target surface and should
not present itself as a substitute for IREE's runtime ecosystem.

The useful comparison is developer control:

- IREE exposes a production-oriented staged compiler built on MLIR.
- Joggle keeps preparation functions explicitly named in the user-selected
  sequence and makes the resulting mod printable between stages.
- Joggle's C and VM paths are evidence that emitters share the same function/mod
  mechanism; they are not evidence of IREE-equivalent target coverage.

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
at a high level. It is a typed source-mod adapter followed by explicit C
emission, not an ONNX Runtime execution provider. Compare the two only on a
carefully scoped task such as adding one custom kernel binding.

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

Both systems value programmable analysis and rewrite policy, but their execution
models differ. Joggle currently performs verified graph edits and typed
implementation selection. It does not maintain a saturation e-graph or claim
egg-style global equality search. An e-graph could become a native or external
analysis/selection component, but that integration must preserve explicit
ownership and verification boundaries.

## What is actually distinctive in Joggle

Joggle should make three narrow claims, each tied to implemented mechanisms:

1. **Uniform authoring surface.** Ordinary typed `.jog` functions can define and
   compose operator bodies, graph queries, rewrite policies, converters, and
   emitters.
2. **Graph-scoped package organization.** `mod` and `use` form explicit
   dependency boundaries for independently developed compiler functionality.
3. **Observed-dependency reuse.** Query caches and reactive schedules validate
   reuse against revisions and dependencies rather than assuming an unchanged
   whole pipeline.

These statements describe design. Claims such as “easier,” “more controllable,”
or “faster to update” need the experiments below.

## Evidence needed for stronger claims

| Intended claim | Suitable evidence | Invalid shortcut |
|---|---|---|
| easier to extend | matched extension tasks, files/tokens touched, completion and error analysis | counting only API names |
| smaller change scope | repository-level change sets across multiple representative tasks | one hand-picked patch |
| faster incremental update | whole-model suite, controlled edits, cold/warm breakdown, correctness checks | timing three tiny graphs |
| competitive generated code | operator and model suites, identical inputs/targets, tuning-budget disclosure | comparing unmatched backends |
| suitable for small coding models | frozen prompts/tasks, syntax-valid and semantics-valid rates, perplexity only as secondary evidence | perplexity alone |

## How to use this page

- If implementing a feature, use the comparisons to locate the correct Joggle
  ownership boundary.
- If designing experiments, use the evidence table to avoid overclaiming.
- If writing a paper later, re-check every external-system statement against a
  pinned version and primary source; do not copy this page as a related-work
  section without that audit.

The project architecture remains documented independently in
[Compiler internals](../compiler/index.md), [Unified metaprogramming](../metaprogramming/index.md),
and [Performance internals](../performance/index.md).
