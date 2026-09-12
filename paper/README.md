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
- The first operator-neutral loop transform is executable rather than a syntax
  sketch: a removable module splits a selected dynamic range, preserves
  carried state, guards partial tiles, round-trips, emits C, and matches exact
  results for negative, empty, exact, short, and partial ranges. Core and the
  emitter contain no tile case.
- The same module sequentially unrolls an exactly divisible static innermost
  range without target pragmas or a new IR form. An order-sensitive multi-axis
  recurrence and a tensor update compile as strict C99 and preserve results;
  dynamic and non-divisible ranges fail before mutation.
- An operator-name-independent fusion function merges an explicitly selected
  same-range pointwise producer/consumer pair. Its gate reduces a three-loop
  tensor chain to two loops, removes a private intermediate tensor through
  scalar forwarding, and retains it when separately returned. The same
  mechanism eliminates the sum tensor in an exposed `add -> relu` chain,
  emits strict C99 through the unchanged backend, and preserves numerical
  output; a shifted-index consumer and an intervening observable call are
  rejected.
- The same legality and rewrite functions support a greedy adjacent-loop
  traversal without adding a model or operator catalogue. On the pinned
  MobileNetV2 expanded body it reduces loops from `374` to `328`, local tensor
  initializers from `155` to `109`, and BatchNorm intermediates from `53` to
  `7`. The fused external-data C passes strict C99 compilation and all 1,000
  official outputs with maximum absolute error `2.0980835e-05`; its weight
  blob is byte-identical to the unfused one.
- A repository benchmark harness now times repeated inference calls in one
  process and records raw CSV rows plus an output checksum. A local Apple M4
  diagnostic with Clang `-O3`, three warm-ups, and 30 repetitions found the
  unfused mean/median at `206.480/206.939 ms` and the fused mean/median at
  `215.612/215.927 ms`: legal fusion regressed latency by about 4.4%. Clang's
  optimization remarks show the fused BatchNorm-ReLU loops falling from
  interleave count four to one. This negative pilot motivates a separate,
  target-aware profitability policy; it is not a paper benchmark.
- Dynamic invocation now accepts a typed `list<Op>` candidate, and `tile.fuse`
  can consume an ordinary `fn(Mod, list<Op>) -> bool` policy or its configured
  three-parameter form. Regression gates show the same three-loop chain
  remaining unchanged under a zero budget and collapsing to one loop under a
  permissive budget.
  A removable `cost` example supplies a structural extent/call-budget policy
  without naming neural-network operations. On the same MobileNetV2 pilot it
  selects 17 of 46 legal pairs, produces byte-identical weights, passes all
  1,000 outputs, and reduces the unrestricted regression from about 4.4% to
  about 1.1% by mean while remaining slower than baseline. The mechanism is
  therefore exercised at application scale; a controlled study still has to
  supply and evaluate a genuinely predictive target policy.
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
- The fusion slice is deliberately one-dimensional and conservative. Its
  application-scale structural and correctness gates are closed, but
  multi-axis dependence tests, profitability, and controlled latency and
  memory-traffic evidence remain open.
- Extensibility needs a controlled study: implement the same custom format,
  operation, transform, and target boundary in Joggle and selected baselines;
  report changed core files, extension code, build/runtime dependencies, and
  failure diagnostics.
- Performance evaluation needs conventional edge models, multiple shapes and
  formats, peak workspace, code and payload size, compile time, latency, and
  deterministic-output checks against production baselines.

## Next experiment order

1. Measure the proven MobileNetV2 path with an isolated repeated-run protocol,
   recording intermediate bytes, memory traffic, workspace, and latency while
   retaining the unfused model as a matched baseline.
2. Generalize the executable split and pointwise fusion functions to
   multi-axis dependence checks and explicit profitability policy; keep
   reorder and unroll removable.
3. Add vectorizable C emission facts (`restrict`, alignment, and selected
   unrolling) only through explicit module policy, then compare generated code
   and compiler optimization reports.
4. Freeze the experimental protocol and only then draft the paper.
