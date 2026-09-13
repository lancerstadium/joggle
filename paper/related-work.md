# Related-work evidence

This is a claim-oriented reading matrix, not manuscript prose. Each row uses a
primary paper or official project documentation. “Joggle boundary” states the
comparison that should be tested; it is not a superiority claim.

| System | Central design | User-facing extension surface | Joggle boundary to test |
| --- | --- | --- | --- |
| MLIR | Reusable infrastructure for IRs at multiple abstraction levels and across domains | Dialects, operations, interfaces, rewrites, conversions, and passes in the MLIR ecosystem | Component-level control for isolating dialect, type, registration, and conversion plumbing; not the end-to-end NN compiler baseline |
| TVM | End-to-end tensor compiler with graph-level optimization, tensor programs, schedules, cost models, and target backends | Relax/TIR programs, schedules, tensor intrinsics, BYOC and target integration | Joggle is not an autotuner or production backend; compare the effort and observability of one controlled semantic and loop-policy change |
| ONNX-MLIR | ONNX semantics represented in an ONNX dialect and lowered through a loop-oriented dialect to native code | Generated ONNX operation definitions, shape inference, lowering patterns, accelerator-scoped dialects and passes | Primary system-level comparison: run matched extensions from ONNX ingestion through the documented pass/accelerator path to a native artifact |
| IREE / TinyIREE | Multi-level MLIR compiler and runtime spanning host orchestration, device code, deployment artifacts, and embedded configurations | Input dialects, compiler plugins, HAL/device targets, runtime modules | IREE is a deployment stack; Joggle tests a narrower research-workbench role with inspectable source and no required runtime |
| Lift | Functional data-parallel patterns, dependent types, and rewrite-rule exploration before OpenCL generation | Pattern composition and rewrite rules | Joggle adopts inspectable functions but does not require a closed map/reduce vocabulary; imported graph calls and explicit loops coexist in one representation |
| RISE & Shine | Language-oriented high- and low-level functional/imperative languages with explicit rewrite strategies | Typed functional programs and Elevate strategies | Compare predictable typed transformation with Joggle's single-representation, ordinary-module approach; do not claim that one IR is universally preferable |
| TileLang | Tile-level dataflow plus explicit memory, layout, thread binding, and scheduling controls, built through TVM IR | Python DSL, tile operators, layout inference, annotations, external instructions | TileLang is the kernel-control baseline; Joggle must show that a user can add an equivalent bounded implementation policy without baking tile concepts into the core, while acknowledging the current performance gap |
| TensorFlow Lite Micro | Interpreter-based inference for fragmented, memory-constrained microcontrollers without operating-system assumptions | C++ operator implementations and explicit resolver registration, with target-specific optimized kernels | TFLM is a deployment runtime and resource baseline; compare a bounded custom operation only when the model, target, and correctness oracle are matched, not as evidence about Joggle's whole compiler workflow |
| ncnn | Dependency-light C++ inference optimized for mobile and embedded CPU/Vulkan deployment | C++ custom `Layer` implementations, creators, runtime registration, converter support, and layer capability flags | ncnn tests the value of a mature optimized runtime and custom-kernel path; Joggle must not present transparent generated C as a faster replacement, but may compare the coupling of one cross-layer co-design task |

## Verified sources

- Lattner et al., “MLIR: Scaling Compiler Infrastructure for Domain Specific
  Computation,” CGO 2021, DOI
  [10.1109/CGO51591.2021.9370308](https://doi.org/10.1109/CGO51591.2021.9370308).
- Chen et al., “TVM: An Automated End-to-End Optimizing Compiler for Deep
  Learning,” OSDI 2018. [USENIX paper page](https://www.usenix.org/conference/osdi18/presentation/chen).
- Jin et al., “Compiling ONNX Neural Network Models Using MLIR,” 2020,
  [arXiv:2008.08272](https://arxiv.org/abs/2008.08272), together with the
  [official ONNX-MLIR documentation](https://onnx.ai/onnx-mlir/).
- Liu et al., “TinyIREE: An ML Execution Environment for Embedded Systems From
  Compilation to Deployment,” IEEE Micro 42(5), 2022, DOI
  [10.1109/MM.2022.3178068](https://doi.org/10.1109/MM.2022.3178068), together
  with the [official IREE repository](https://github.com/iree-org/iree).
- Steuwer, Remmelg, and Dubach, “Lift: A Functional Data-Parallel IR for
  High-Performance GPU Code Generation,” CGO 2017, DOI
  [10.1109/CGO.2017.7863730](https://doi.org/10.1109/CGO.2017.7863730).
- Steuwer et al., “RISE & Shine: Language-Oriented Compiler Design,” 2022,
  [arXiv:2201.03611](https://arxiv.org/abs/2201.03611).
- Wang et al., “TileLang: A Composable Tiled Programming Model for AI
  Systems,” 2025, [arXiv:2504.17577](https://arxiv.org/abs/2504.17577).
- David et al., “TensorFlow Lite Micro: Embedded Machine Learning on TinyML
  Systems,” MLSys 2021,
  [proceedings paper](https://proceedings.mlsys.org/paper_files/paper/2021/file/6c44dc73014d66ba49b28d483a8f8b0d-Paper.pdf),
  together with the official
  [`MicroOpResolver`](https://github.com/tensorflow/tflite-micro/blob/main/tensorflow/lite/micro/micro_op_resolver.h)
  interface.
- Tencent, [`ncnn`](https://github.com/Tencent/ncnn), together with the
  official
  [custom-layer guide](https://github.com/Tencent/ncnn/wiki/how-to-implement-custom-layer-step-by-step)
  and [`Net::register_custom_layer`](https://github.com/Tencent/ncnn/blob/master/src/net.h)
  interface. This row cites an implementation project rather than a peer-reviewed
  system paper and must be identified as such in the manuscript.

## Comparison discipline

The final paper should compare only matched tasks. A model-throughput table can
use ONNX Runtime as a correctness and deployment reference, but it cannot test
extension usability. TileLang can test kernel author control, but not frontend
or module packaging. ONNX-MLIR is the system-level baseline because the study
concerns a neural-network compiler workflow. TVM remains a mechanism-level
control for kernel and scheduling tasks. Bare MLIR may isolate dialect/type
plumbing but must not be presented as a full competing system. Every system
must be studied through its documented extension path and a pinned revision.
Repository line count, subjective syntax preference, and an unmatched backend
benchmark are not valid evidence of lower extension coupling.

The matrix now includes lightweight edge runtimes, but it is not a systematic
survey. Exact baseline revisions, supported task subsets, and the matched
extension protocol must be frozen before these comparisons enter Results.
