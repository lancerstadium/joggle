# Literature map and comparison policy

This file is the evidence ledger for the paper's Motivation and Related Work.
It is deliberately broader than `references.bib`: an entry moves into the
bibliography only after its title, authors, venue/year, and persistent identifier
have been checked against a primary publisher, proceedings, DOI, or arXiv
record. The submission gate is **55--65 verified candidates and at least 50
works actually used to support distinct claims in the paper**. Citation count is
not a substitute for relevance.

## The argument the literature must test

The literature does not reveal a missing general-purpose inference compiler.
It reveals a recurring three-way tension:

1. **Production breadth introduces extension discontinuities.** Graph
   semantics, tensor computation, loop schedules, storage, target interfaces,
   and runtimes are separated for sound engineering reasons. A co-design idea
   that crosses them can consequently require several extension mechanisms and
   loss-prone conversions.
2. **Performance automation and author control sit on different paths.** Search
   systems and superoptimizers can discover strong kernels, while scheduling
   languages make expert decisions explicit. Neither by itself supplies a
   small, inspectable end-to-end workbench in which a researcher can introduce
   a source relation, expose its computation, rewrite loops and storage, and
   carry the result into an ordinary artifact.
3. **Edge deployment magnifies the unsupported-path cost.** Mature runtimes are
   fast on supported operators, but a new data format, operator, accelerator
   primitive, or ABI can fall off the library path. Transparent AOT code is
   easier to inspect and retarget, but without target-aware transformations its
   quality can be far below production runtimes.

Joggle's defensible hypothesis is therefore narrow: a progressive typed
function representation plus distributable module functions can reduce the
*cross-layer experimental boundary*. It does not claim that one IR is always
better, that source brevity proves usability, or that generic generated C
replaces tuned runtimes. The evaluation must measure where the hypothesis holds
and expose the performance and compatibility boundary where it does not.

## Comparison roles

| Role | Systems | Required evidence |
| --- | --- | --- |
| Primary system baseline | ONNX-MLIR | Matched cross-layer extension tasks through the documented native path |
| Tensor-compiler control | TVM | Matched semantic and scheduling tasks; compile/search cost separated from run time |
| Production runtime controls | ONNX Runtime and LiteRT | Same serialized model, input, thread count, numerical contract, and host within each frontend study |
| Lightweight edge control | ncnn | One matched model and one custom-layer/kernel task; never pooled with a different model format |
| Deployment-stack comparison | IREE/TinyIREE | Artifact/runtime/target integration and extension mechanism; performance only with an exactly matched CPU path |
| Kernel-authoring comparison | TileLang, Triton, Exo, Halide | Control surface and mechanism, not whole-model compatibility |
| Automatic optimization comparison | Ansor, Roller, Welder, Ladder, Mirage, Axon | Search space, correctness contract, tuning cost, and where user intent enters; selected kernel experiments only |
| Language/IR lineage | MLIR, Lift, RISE, Elevate, Relay/Relax, TensorIR | Design comparison and explicit non-goals, not a speed leaderboard |

## Candidate pool

`V` means metadata and central claim have been checked against a primary
record. `Q` means relevant but still queued for metadata/full-text verification;
it cannot yet be cited. `E`, `M`, and `C` mean experimental baseline,
mechanism-level comparison, and contextual citation respectively.

### A. End-to-end compiler and deployment stacks

| State | Work | Role | Pain point or boundary for Joggle |
| --- | --- | --- | --- |
| V | TVM: An Automated End-to-End Optimizing Compiler for Deep Learning | E | Powerful graph/tensor/schedule split and search; matched control for extension path and compile cost |
| V | MLIR: Scaling Compiler Infrastructure for Domain Specific Computation | M | Reusable multi-level dialect infrastructure; tests whether a smaller bounded substrate reduces plumbing |
| V | Compiling ONNX Neural Network Models Using MLIR / ONNX-MLIR | E | Closest end-to-end frontend/compiler baseline |
| V | TinyIREE: An ML Execution Environment for Embedded Systems from Compilation to Deployment | M | Embedded deployment breadth and runtime contract |
| V | Glow: Graph Lowering Compiler Techniques for Neural Networks | C | Strong graph lowering and instruction IR; contrasts staged representations |
| Q | nGraph: a New Compiler for Deep Learning Frameworks | C | Framework-neutral graph compilation and backend interfaces |
| V | Tensor Comprehensions: Framework-Agnostic High-Performance Machine Learning Abstractions | M | Concise semantics plus polyhedral/code-generation path |
| Q | Relay: A New IR for Machine Learning Frameworks | M | Functional graph IR, type/shape reasoning, and compiler composition |
| V | Relax: Composable Abstractions for End-to-End Dynamic Machine Learning | M | Dynamic shape and cross-level composition in modern TVM |
| V | MNN: A Universal and Efficient Inference Engine | E | Lightweight mobile runtime and model-conversion ecosystem |
| Q | MonoNN: Enabling a New Monolithic Optimization Space for Neural Networks | M | Evidence for gains from crossing operator/kernel boundaries, with a GPU-specific design |

### B. Functional, tensor, loop, and scheduling languages

| State | Work | Role | Pain point or boundary for Joggle |
| --- | --- | --- | --- |
| V | Lift: A Functional Data-Parallel IR for High-Performance GPU Code Generation | M | Typed rewrite exploration; closed pattern vocabulary versus open functions |
| V | RISE & Shine: Language-Oriented Compiler Design | M | Explicit high/low languages and strategy control |
| Q | Elevate: A Language to Write Composable Program Optimization Strategies | M | User-authored strategy language and predictable rewrites |
| V | Halide: A Language and Compiler for Optimizing Parallelism, Locality, and Recomputation | M | Algorithm/schedule separation and expert control |
| V | Tiramisu: A Polyhedral Compiler for Expressing Fast and Portable Code | M | Explicit schedule commands over affine computations |
| Q | PolyMage: Automatic Optimization for Image Processing Pipelines | C | Pipeline fusion, tiling, and storage optimization |
| Q | TACO: A Language and Compiler for Optimizing Sparse Tensor Algebra | M | Format-aware tensor algebra and code generation |
| Q | DaCe: Data-Centric Parallel Programming and Symbolic Performance Modeling | M | Explicit data movement and stateful dataflow |
| Q | HeteroCL: A Multi-Paradigm Programming Infrastructure for Software-Defined Reconfigurable Computing | M | Custom types, schedules, and heterogeneous hardware co-design |
| V | TensorIR: An Abstraction for Automatic Tensorized Program Optimization | M | Schedulable tensor programs and tensorization boundaries |
| V | Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations | M | Programmable tiled GPU kernels |
| V | Exocompilation for Productive Programming of Hardware Accelerators | M | User-controlled scheduling with externally supplied instructions and memories |
| V | TileLang: A Composable Tiled Programming Model for AI Systems | M | Tile-level control; intentionally not an end-to-end frontend baseline |
| V | Hidet: Task-Mapping Programming Paradigm for Deep Learning Tensor Programs | M | Hardware mapping abstraction and inference latency focus |

### C. Search, synthesis, and superoptimization

| State | Work | Role | Pain point or boundary for Joggle |
| --- | --- | --- | --- |
| V | Ansor: Generating High-Performance Tensor Programs for Deep Learning | E | Broad automatic search but substantial tuning budget |
| Q | FlexTensor: An Automatic Schedule Exploration and Optimization Framework for Tensor Computation on Heterogeneous System | M | Template/search-space construction burden |
| V | Roller: Fast and Efficient Tensor Compilation for Deep Learning | M | Constructive performance modeling lowers tuning cost |
| V | Welder: Scheduling Deep Learning Memory Access via Tile-graph | M | Cross-operator memory traffic and tile fusion |
| V | Ladder: Enabling Efficient Low-Precision Deep Learning Computing through Hardware-aware Tensor Transformation | M | Data-format/layout transformations as first-class performance decisions |
| V | Mirage: A Multi-Level Superoptimizer for Tensor Programs | M | Uniform multi-level search plus probabilistic equivalence |
| V | Axon: A Synthesizing Superoptimizer for Tensor Programs | M | Semantic specification, ISA synthesis, SMT equivalence, tiling and fusion |
| V | Pure Tensor Program Rewriting via Access Patterns (Glenside) | M | Equality saturation and layout discovery without operator-name rules |
| V | Equality Saturation for Tensor Graph Superoptimization (Tensat) | M | Graph rewrite saturation and extraction cost |
| V | TASO: Optimizing Deep Learning Computation with Automatic Generation of Graph Substitutions | M | Generated graph substitutions and formal equivalence conditions |
| V | egg: Fast and Extensible Equality Saturation | C | General rewrite infrastructure used by tensor optimizers |
| V | BOLT: Bridging the Gap between Auto-tuners and Hardware-native Performance | M | Fast generated kernels versus vendor libraries |
| V | AStitch: Enabling a New Multi-dimensional Optimization Space for Memory-Intensive ML Training and Inference on Modern SIMT Architectures | M | Whole-subgraph fusion and memory scheduling |

### D. Whole-graph, dynamic-shape, and JIT execution

| State | Work | Role | Pain point or boundary for Joggle |
| --- | --- | --- | --- |
| V | Rammer: Enabling Holistic Deep Learning Compiler Optimizations with rTasks | M | Cross-operator scheduling requires hardware-neutral execution abstractions |
| V | Nimble: Efficiently Compiling Dynamic Neural Networks for Model Inference | M | Ahead-of-time planning under input-dependent execution |
| V | DISC: A Dynamic Shape Compiler for Machine Learning Workloads | M | Dynamic shape specialization and production deployment |
| Q | DietCode: Automatic Optimization for Dynamic Tensor Programs | M | Schedule reuse across dynamic shapes |
| V | Cortex: A Compiler for Recursive Deep Learning Models | C | Model structure and runtime dynamism beyond static DAGs |
| Q | Brainstorm: Fast End-to-End Deep Learning Compiler for Dynamic Neural Networks | C | Runtime-statistics-guided dynamic optimization |
| V | DNNFusion: Accelerating Deep Neural Networks Execution with Advanced Operator Fusion | M | Graph fusion profitability and generated kernels |

### E. Edge, microcontroller, and heterogeneous deployment

| State | Work | Role | Pain point or boundary for Joggle |
| --- | --- | --- | --- |
| V | TensorFlow Lite Micro: Embedded Machine Learning for TinyML Systems | E | Small runtime, explicit operator resolver, target kernels |
| V | MCUNet: Tiny Deep Learning on IoT Devices | E | Network/runtime co-design under SRAM and flash constraints |
| Q | MCUNetV2: Memory-Efficient Patch-based Inference for Tiny Deep Learning | E | Spatially scheduled inference and peak-memory reduction |
| V | DORY: Automatic End-to-End Deployment of Real-World DNNs on Low-Cost IoT MCUs | M | Deployment, tiling, and heterogeneous memory |
| V | PULP-NN: Accelerating Quantized Neural Networks on Parallel Ultra-Low-Power RISC-V Processors | M | Low-bit kernels and ISA-aware deployment |
| Q | CMSIS-NN: Efficient Neural Network Kernels for Arm Cortex-M CPUs | E | Hand-optimized library cliff for generated code |
| Q | MATCH: A Compiler for Deployment of CNNs on Heterogeneous Platforms | M | Pattern-to-accelerator mapping and fallback execution |
| V | VTA: An Open Hardware-Software Stack for Deep Learning | M | Extensible accelerator ISA and compiler co-design |
| V | Timeloop: A Systematic Approach to DNN Accelerator Evaluation | C | Explicit mapping-space and hardware cost modeling |
| V | MAESTRO: A Data-Centric Approach to Understand Reuse, Performance, and Hardware Cost of DNN Mappings | C | Dataflow cost models suitable for optional policy modules |
| V | Interstellar: Using Halide's Scheduling Language to Analyze DNN Accelerators | C | One schedule notation spanning algorithms and accelerators |

### F. Correctness, testing, and compiler reliability

| State | Work | Role | Pain point or boundary for Joggle |
| --- | --- | --- | --- |
| V | NNSmith: Generating Diverse and Valid Test Cases for Deep Learning Compilers | M | Valid graph generation exposes optimizer semantic bugs |
| V | Fuzzing Deep Learning Compilers with HirGen | M | Hierarchical IR fuzzing and coverage |
| V | An Empirical Study on Common Bugs in Deep Learning Compilers | C | Failure taxonomy and the cost of complex lowering stacks |
| V | Metamorphic Testing of Deep Learning Compilers | C | Oracle-free differential properties |

### G. Workloads that must bound the evaluation

| State | Work | Role | Why it matters |
| --- | --- | --- | --- |
| V | Attention Is All You Need | C | Establishes attention's contraction, normalization, and shape patterns |
| V | BERT: Pre-training of Deep Bidirectional Transformers for Language Understanding | C | Encoder workload with embeddings, normalization, attention, and dynamic sequence axes |
| V | An Image Is Worth 16x16 Words: Transformers for Image Recognition at Scale | C | ViT adds attention to a conventional static vision task |
| Q | Language Models are Unsupervised Multitask Learners (GPT-2) | C | Autoregressive language-model workload and state/cache boundary |

The pool currently contains 64 candidate works: 49 primary-record checks and
15 queued checks. The counts are intentionally visible so a partially verified
search cannot be mistaken for a finished bibliography.

## Pain-point synthesis for Section 2

Section 2 should not be an RQ list. It should establish four observations with
one concrete running extension (for example a packed low-precision contraction
plus an external edge kernel):

1. **The semantic-to-artifact path is the experiment.** Show where the same
   change appears as source relation, reusable tensor body, loop/layout choice,
   storage contract, and exported ABI in representative systems.
2. **Existing abstractions optimize locally but compose through boundaries.**
   Compare graph/dialect conversion, schedule language, BYOC/external kernels,
   and runtime registration. The problem is not that any boundary is bad; it is
   the accumulated experimental coupling across them.
3. **Automation does not remove the need for an inspectable control plane.**
   Axon/Mirage/Ansor motivate optional synthesis and search, while Lift/RISE,
   TileLang, Exo, and Halide motivate explicit author control. Joggle should be
   the substrate on which either policy can be packaged, not another mandatory
   search engine.
4. **Transparent artifacts expose a real performance cliff.** Existing results
   already show correct generated C can remain an order of magnitude behind a
   production runtime. This negative evidence motivates target-aware access,
   layout, packing, vectorization, and profitability modules rather than a
   claim of immediate performance superiority.

The section ends with the hypothesis and measurable predictions, after which
the research questions become evaluation subheadings rather than the section's
content.

## Priority order

1. **P0 — Motivation and bibliography:** verify the 17 locked records, resolve
   at least 38 queued records, and rewrite Section 2 around the four observations.
2. **P0 — Workload spectrum:** keep the official CNN/detection suite, complete
   TFLite MobileNetV2 execution, add one pinned ViT and one compact language
   model/attention workload, and record exact unsupported frontiers.
3. **P0 — Artifact evidence:** compare Joggle with the native runtime for each
   serialized model; add ONNX-MLIR and TVM only on matched tasks; add ncnn on one
   edge model/custom-layer task if the conversion is reproducible.
4. **P1 — Mechanism evidence:** build an operator/shape matrix for contraction,
   elementwise, reduction, normalization, pooling, and attention subgraphs.
   Replace pass-status tables with measured transformation reach, compile cost,
   code size, workspace, correctness, and latency.
5. **P1 — Figures:** reproduce the provided visual grammar: compact grouped
   headings, restrained cell shading for dense matrices, shared legends, and
   aligned small multiples. Missing and unsupported cells remain explicit.
6. **P2 — Internal refactoring and broad frontend/backend additions:** perform
   only when a P0/P1 experiment reveals a correctness or maintainability blocker.
