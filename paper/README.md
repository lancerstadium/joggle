# Joggle paper

Target: FSE 2027 Research Papers. The submission deadline is 2 October 2026
AoE. The manuscript is not yet ready to draft: this directory records the
question and evidence that must hold before prose is written.

The submission contract is 18 pages of text and figures plus 4 pages of
references, heavy double-anonymous review, and a `Data Availability` statement.
The replication package must be curated and anonymized for review. Because AI
tools have materially participated in research design, implementation, testing,
and analysis, the Methods section must describe those uses in detail; no result,
dataset, or citation may enter the paper without an independently reproducible
artifact or source.

## Research question

Can a small compiler workbench let AI software/hardware co-design researchers
move a conventional neural network from imported graph calls to explicit loops,
storage, custom formats, target execution, and emitted code while retaining one
inspectable function IR and user-defined transformation mechanism?

This is a software-engineering question about extensibility, semantic coverage,
and research iteration cost. It is not currently a claim that the reference C
emitter outperforms production inference compilers.

## Candidate contributions

1. One `Fn/Blk/Op/Val` representation covers graph calls, function
   composition, structured control, explicit tensor computation, and target
   calls. Frontends and targets do not introduce durable secondary IRs.
2. Import, analysis, transformation, planning, simulation, and emission are
   ordinary typed module functions. They share transactional editing,
   reflection, overload resolution, and revision-scoped queries instead of a
   pass class hierarchy or generated extension headers.
3. Progressive exposure lets an experiment retain a high-level call, replace it
   with a user implementation, or expose its existing body down to loops under
   one explicit capability predicate.

These remain candidate claims until the related-work comparison and controlled
extension study distinguish them from TVM, TileLang, IREE, ONNX-MLIR, MLIR, and
small embeddable compiler frameworks.

## Evidence already reproduced

- Official ONNX MNIST: VM, inline-data C, and external-data C agree on all 10
  outputs; maximum absolute error is `1.90734863e-05`.
- Official ONNX MobileNetV2: the same three paths agree on all 1,000 outputs;
  maximum absolute error is `2.00271606e-05`.
- Pure static structured control reduces MobileNetV2 VM work from
  `98,167,456,513` to `95,592,386,975` deterministic interpreter steps.
- Separating payloads reduces MobileNetV2 generated C from `56,911,938` to
  `246,085` bytes plus a `14,156,560`-byte raw weight blob.
- Re-importing MobileNetV2 with typed tensor constants leaves three writable
  C workspace arrays (`10.91 MiB`) instead of 267 arrays that mixed activations
  and weights. The external-data source contains 266 aligned read-only weight
  views and no per-inference weight `memcpy`; all 1,000 outputs still agree,
  with maximum absolute error `2.0980835e-05`.
- Default, sanitizer, ONNX, TFLite, generated-C, VM, installation, and external
  module gates exercise the same public interfaces.

The timings above and in local build output are engineering diagnostics, not
paper measurements. The benchmark protocol still needs isolated machines,
warm-up, repeated trials, confidence intervals, pinned toolchains, and
comparable optimization settings.

## Blocking evidence

- Generated C still uses scalar untiled loops and is substantially slower than
  ONNX Runtime on the current CPU diagnostic. It is a reference backend, not
  yet evidence of efficient edge inference.
- Elementwise activation and producer loops are not yet fused at body level.
- The IR editor lacks the general clone-with-value-remapping primitive needed
  for clean loop split, reorder, fuse, and unroll modules.
- Extensibility needs a controlled study: implement the same custom format,
  operation, transform, and target boundary in Joggle and selected baselines;
  report changed core files, extension code, build/runtime dependencies, and
  failure diagnostics.
- Performance evaluation needs conventional edge models, multiple shapes and
  formats, peak workspace, code and payload size, compile time, latency, and
  deterministic-output checks against production baselines.

## Next experiment order

1. Add general block cloning with explicit value remapping, then implement loop
   split/reorder/fuse/unroll as a removable module on the existing IR.
2. Normalize fused activations to ordinary function composition and implement
   producer/consumer loop fusion without operator-name cases.
3. Add vectorizable C emission facts (`restrict`, alignment, and selected
   unrolling) only through explicit module policy, then compare generated code
   and compiler optimization reports.
4. Freeze the experimental protocol and only then draft the paper.
