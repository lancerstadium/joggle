# Joggle

Joggle is being relaunched as a research compiler for deterministic,
resource-constrained neural-network inference.

The project investigates one question: can a compiler take an ordinary
pretrained neural network and a bounded finite numerical-representation
function, then synthesize a competitive fixed-cycle reduction without a
format-specific kernel, lowering, hardware template, or user schedule?

The proposed mechanism analyzes the representation function inside its
enclosing tensor reduction. It may factor repeated code-dependent work into
bounded partial state and a shared-parameter finalizer, or retain direct
decode-and-multiply execution when factoring is not profitable.

## Status

The previous implementation has been archived at the Git tag
`archive/pre-relaunch-a2a281e`. This branch intentionally contains no compiler
implementation while the reduction-synthesis hypothesis is tested.

The first gate is a disposable, frozen compiler probe evaluated against direct
execution, generic algebraic rewriting, decoder-plus-MAC synthesis, and
handwritten INT, SP2, codebook, distributed-arithmetic, and per-vector-scaling
baselines. Open-source synthesis is used only to reject weak candidates;
publishable hardware claims require vendor post-route results and real-board,
batch-one measurements on standard pretrained models.

## Non-goals

Joggle is not another graph IR, custom-datatype registry, accelerator ISA
language, device hierarchy, scheduling DSL, HLS wrapper, or portable runtime.
ONNX/TFLite import, textual modules, package loading, passes, host JIT,
cost-model integration, and artifact emission are necessary infrastructure,
not research contributions.

No public API, language, module format, or implementation from the archived
prototype should be considered current or stable.

## License

Joggle is licensed under the MIT License. See [LICENSE](LICENSE).
