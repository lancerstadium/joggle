# Joggle: A Function-Oriented Compiler Workbench for Neural-Network Co-Design

Working manuscript for the FSE 2027 Research Papers track. The current file is
an argument draft, not a submission-ready paper. Pilot values are labeled and
must be replaced by frozen experiment results.

## Abstract

Neural-network co-design experiments often change more than one compiler
layer: a researcher may import a conventional model, replace an operator's
semantics, expose its loops, alter storage or data representation, and emit a
target-specific artifact. Established compiler stacks support these tasks, but
their distinct representations and native extension mechanisms can make a
small experiment depend on substantial compiler infrastructure. Joggle explores
a narrower design point: one typed function representation in which imported
calls, reusable semantic bodies, structured loops, storage decisions, and
target preparation remain inspectable, while decoders, analyses, transforms,
and emitters are ordinary distributable module functions. We evaluate whether
this design reduces extension coupling without hiding failure. The planned
study measures four matched extension tasks, transactional composition, staged
compatibility on conventional inference models, numerical correctness, and the
quality of generated artifacts. Current pilots execute eight ONNX models and
preserve explicit unsupported frontiers on two harder models, but generated C
remains substantially slower than a one-thread production runtime. The final
paper will therefore test research iteration cost and transparency rather than
claim production-level inference performance.

## 1. Introduction

The unit of change in neural-network hardware/software co-design is rarely an
isolated graph operator. A new numeric format changes types and constants; a
new accelerator primitive changes the implementation chosen for a family of
calls; a storage experiment changes lifetimes and interfaces; and a scheduling
study must observe and rewrite loops before an artifact target accepts the
program. These changes cross boundaries that production compilers introduce
for good reasons, including stable frontend semantics, target-independent
optimization, scheduling, code generation, and runtime deployment.

MLIR makes such multi-level systems reusable through extensible dialects and
passes [Lattner et al. 2021](https://doi.org/10.1109/CGO51591.2021.9370308).
TVM combines graph optimization, tensor programs, schedules, and target-aware
search [Chen et al. 2018](https://www.usenix.org/conference/osdi18/presentation/chen).
ONNX-MLIR represents ONNX and loop-level computation in distinct dialects
[Jin et al. 2020](https://arxiv.org/abs/2008.08272), while IREE extends an MLIR
stack through deployment artifacts and runtimes that scale to embedded systems
[Liu et al. 2022](https://doi.org/10.1109/MM.2022.3178068). These systems target
capabilities and deployment breadth beyond the scope of a small research
workbench.

This paper asks a different question: what is the smallest compiler substrate
that remains useful when a co-design researcher must change several of these
boundaries at once? “Small” alone is not a contribution. Removing distinct IRs
can merely move complexity into special cases, weaken verification, or produce
poor code. A useful answer must preserve typed structure, permit independent
extensions, compose deterministically, expose unsupported programs, and
produce executable artifacts whose limitations can be measured.

Joggle tests one answer. Its program state consists of functions, blocks,
operations, values, structural types, and open attributes. An operation may
initially call a source-schema function such as an ONNX operation, later call
an inspectable neural-network function, and finally contain explicit loops and
scalar operations after function expansion. It does not migrate between graph,
tensor, loop, and target IR object models. Module functions inspect or mutate
this state through the same public API; reading a model, inferring types,
selecting an implementation, planning storage, and emitting C are explicit
invocations rather than privileged pipeline stages.

This design is influenced by the function and rewrite orientation of Lift
[Steuwer et al. 2017](https://doi.org/10.1109/CGO.2017.7863730) and RISE & Shine
[Steuwer et al. 2022](https://arxiv.org/abs/2201.03611), but it does not impose
a closed data-parallel pattern vocabulary. It also differs from tile-level
languages such as TileLang, which expose GPU memory, layout, thread binding,
and scheduling concepts to kernel authors
[Wang et al. 2025](https://arxiv.org/abs/2504.17577). Joggle leaves such
concepts to user modules and asks whether the core function representation is
sufficient to host them. The evaluation must determine where that choice
succeeds and where a dedicated representation is necessary.

The paper makes three candidate contributions:

1. A function-oriented compiler design that progressively exposes conventional
   neural-network models from schema calls to structured loop bodies without a
   mandatory representation boundary.
2. A source module mechanism in which frontend bridges, semantic
   implementations, analyses, transformations, capability checks, and artifact
   generation use ordinary typed functions and a transactional editing API.
3. An empirical evaluation of extension effort, composition failure,
   conventional-model coverage, numerical correctness, and generated-artifact
   quality, including negative compatibility and performance results.

The first two items are implemented design candidates. The third remains the
submission-critical study; no contribution claim is final until its protocol
and results are frozen.

## 2. Research questions

- **RQ1, progressive representation:** Can imported model calls, reusable
  semantics, explicit loops, storage decisions, and target preparation remain
  understandable and verifiable in one function representation?
- **RQ2, extension cost:** For matched co-design tasks, how many concepts,
  files, native registrations, generated definitions, core changes, and build
  dependencies does a developer encounter?
- **RQ3, composition:** Do independently defined modules compose with stable
  output, transactional failure, and useful unsupported-frontier diagnostics?
- **RQ4, artifact quality:** What correctness, code size, workspace,
  compilation, and latency costs result, and how much can a user-defined
  implementation improve them without modifying the core or C emitter?

## 3. Design

### 3.1 One progressive function representation

The representation has seven public concepts: `Mod`, `Fn`, `Blk`, `Op`, `Val`,
`Ty`, and `Attr`. Calls and structured control flow are operations; tensor
shapes and numeric formats are structural types; source-schema fields and
target facts are open attributes. This is not a claim that every compiler
should use one IR. It is a testable constraint intended to keep a co-design
experiment readable while it moves from model-scale dataflow toward explicit
computation.

### 3.2 Semantics are executable functions

A frontend decoder preserves source operators and metadata. A separate bridge
can retarget a source call to a shared function once its schema conditions are
met. The shared function has a typed body, so a later pass can inspect the call,
replace it with another compatible implementation, or expand its body into the
caller. The artifact target handles only scalar, memory, call, and control-flow
forms. For example, variadic ONNX Min and Max are first converted into binary
chains of ordinary broadcast-capable `nn.minimum` and `nn.maximum` functions;
the C emitter has no Min, Max, or ONNX case.

### 3.3 Extensions are module functions

A module is a source package with typed functions, dependencies, visibility,
and optional native bindings. Metadata selects functions for conventions such
as inference or conversion, but it does not create a second pass language.
Read-only queries, mutating transforms, decoders, and artifact functions share
the invocation model. The study will test whether this uniformity lowers real
extension cost or merely shifts complexity into module code.

### 3.4 Capability-driven exposure and failure

Targets publish an ordinary predicate over operations. Exposure repeatedly
expands or selects implementations until the target accepts the program or a
fixed point leaves an explicit frontier. Mutating invocations are transactional:
failure must not expose a half-rewritten program. Partial ONNX models are
therefore reported by stage and remaining source calls rather than counted as
supported end to end.

## 4. Evaluation status

The repository currently records pilots, not publication measurements. Eight
ONNX models execute against stored reference outputs. DenseNet completes
structural conversion, while TinyYOLOv3 retains a type frontier and
SSD-MobileNetV1 retains 710 source calls after conversion. Across executed
models, stored maximum absolute differences range from approximately
`1.34e-7` to `2.10e-5`. These checks establish numerical paths, not task-level
accuracy.

The structural compatibility table is now generated directly from eleven
pinned Zoo tests rather than transcribed from test output. Nine models complete
semantic conversion and canonical round trip. TinyYOLOv3 retains 219 unknown
results after inference; SSD-MobileNetV1 infers all result types but retains
710 source calls after conversion. Models absent from the configured cache are
omitted, not counted as passes. Structural completion is kept separate from
the eight-model numerical execution claim above.

Generated C is presently the main negative result. Depending on the model, the
recorded unisolated pilots are about 7--101 times slower than one-thread ONNX
Runtime. An out-of-tree spatial convolution body improves five matched C
variants by 1.36--6.29 times without frontend or emitter changes, but does not
close the production-runtime gap. Final experiments require isolated repeated
runs, dispersion, fixed revisions and flags, task-level accuracy where
applicable, and at least one second machine.

The extension-cost study and controlled performance study are not complete.
Until they are, this manuscript must not claim that Joggle is easier to extend,
more compatible, or faster than another compiler.

The first reproducible extension-footprint pilot freezes four tasks and runs
their named tests. Its three out-of-tree tasks contain 20, 69, and 210 source
lines; the bundled numeric-format task contains 284 source lines across a
source semantic module, a native binding, and two target companions. These are
descriptive implementation footprints, not usability or productivity results.
Matched baseline implementations and recorded implementation time remain
necessary for RQ2.

## 5. Required remaining sections

- Motivating extension task and trace through the representation
- Implementation and module-loading boundary
- Frozen study design and baseline versions
- Results generated from committed raw data
- Related work synthesized from [related-work.md](related-work.md)
- Threats to validity and explicit limitations
- Conclusion
- Data Availability
- Research-use AI disclosure in Methods
- Author contributions, funding, conflicts, and ethics declarations for the
  camera-ready package
