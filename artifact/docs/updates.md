# Compiler updates

[Evaluation entry point](../README.md). Run all commands from the repository root.

The production comparison uses Joggle, TVM, and ONNX-MLIR. Each worker applies
a hash-bound ONNX node edit, builds a replacement executable, and checks its
outputs against an isolated semantic oracle. `update` first builds the original
model in the same worker; `rebuild` builds only the edited model in a fresh
worker. Both start their measured interval before applying the edit and stop
after numerical validation. Oracle generation is outside that interval.

```sh
cmake --build build --target joggle-artifact-reactive -j4
python3 artifact/run_baseline_benchmarks.py \
  --worker update --backend joggle \
  --inputs PATH/inputs --case-id CASE \
  --model PATH/model.onnx --edit-json PATH/edit.json
```

Repeat with `--worker rebuild`, or select `--backend tvm` /
`--backend onnx-mlir` with the corresponding compiler configuration. The JSON
sample records edit/input/output hashes, absolute wall time, executable-ready
time, validation time, backend stages, and retained state. Joggle retains its
environment and evaluator plans but parses a fresh source graph. Add
`--joggle-reuse prepared` to retain prepared function bodies as well. Matching
specializations enter preparation as declarations; only referenced bodies are
materialized before scalarization and model-wide storage planning. The matched
rebuild starts with an empty body cache. Emission, native compilation, and
binding remain in both measured paths.
Its entry signature is derived from the same fixed inputs as the end-to-end
collector. Building first refreshes the copied mod files in `build/modules`.

The resident server publishes C source, a header, ABI metadata, and an immutable
binary constant artifact through the existing `c.source`, `c.header`, `c.api`,
and `c.data` interfaces. Constants are not expanded into C byte-array literals.
The runner owns the aligned constant buffer for the lifetime of its executable;
the ABI places this pointer after model inputs and before result pointers.
Graph lowering, constant serialization, native compilation, data loading, and
binding remain charged to executable-ready time. External data does not itself
cache object files: the current adapter still invokes the host compiler for
each replacement. The compiler identity records this storage mode, so these
runs must not be pooled with the earlier embedded-constant measurements.

Each sample fingerprints the native compiler, collector sources, and protocol
before and after measurement. Joggle also fingerprints the resident server,
loaded mod files, and host C compiler. A changed fingerprint rejects the
sample. Fingerprinting is outside the timed interval. A stable sample is a
measurement record, not a completed repeated model/edit population.

Repeated production collection uses the same collector, without a separate
benchmark script:

Numerical collectors record the latest commit affecting files outside
`paper/`. Manuscript-only edits and commits leave that experimental revision
unchanged, so typesetting can continue during a run. Changes to compiler,
mod, or collector sources still change the recorded experimental state.

```sh
python3 artifact/run_baseline_benchmarks.py --group updates --backend joggle \
  --joggle-reuse prepared \
  --inputs local/cache/artifact/release-data/inputs --model-root local/cache/onnx-zoo \
  --edit-manifest artifact/manifests/production-node-edits.json \
  --iterations 10 --output local/cache/artifact/production-updates-joggle.csv
```

Select `--backend tvm` or `--backend onnx-mlir` with the same manifest and
inputs for the external compilers. Each repetition launches a fresh worker
for each policy; update retains the state created by its original-model build.
Pair order and model/edit/repetition order are randomized with the recorded
seed. No compiler caches are cleared or synthesized. `--smoke --iterations 1`
and optional `--case-id` filters mark integration runs as partial.

Production collection gives an update worker twice the `--case-timeout`
allowance because it builds and validates both the original and replacement;
a rebuild worker builds only the replacement. `--worker-timeout` overrides
the whole-worker limit for both policies. These process limits include untimed
oracle/setup work and do not change the reported replacement interval. They
are not per-build wall-time enforcement. Collection records retain both the
case setting and effective policy-specific worker limits. A timeout is an
infrastructure outcome, not a numerical mismatch.

The frozen node-edit population selects the first, middle, and last eligible
Relu/Add sites per model, deduplicating coincident sites. It contains 37 edits
over 13 models: Relu becomes LeakyRelu and Add becomes Sub with the same
operands. The two models without these sites are recorded as missing coverage;
their quantized edit population remains to be added. Each edit is checked
against the source-model hash and ONNX schema before workers start.

Collection writes absolute timing CSV, full worker samples in `.samples.jsonl`,
and a hash-bound `.json` record. Failed samples retain diagnostics and have no
timing values. Successful samples must match the requested edit, edited model,
input index, benchmark specification, oracle, and timing boundaries. Mixed
compiler identities invalidate a collection. These records are not Figure 6
release artifacts until the cross-system assembly and population gates pass.

`merge_update_rows.py --production --allow-partial` reconciles the production
CSV with its hash-bound raw samples and collection record. Pass `--spec`,
`--population`, `--inputs-dir`, and `--model-root` to identify the frozen
protocol. The assembler checks edit/model/input identity, update/rebuild pairs,
compiler revisions, timing boundaries, and failure records. Its output retains
absolute nanosecond timings, retained state, and backend-specific
`stage_<name>_ns` columns. Unreported stages stay empty, rather than becoming
zero. Different backend stage names do not imply equivalent work boundaries.
The assembled sample CSV is marked partial and is not a release result.

`validate_reactive.py` and `figure_06_update.py`
still consume the earlier stage-counter provider schema; they do not yet
assemble `production-update-sample/v1`. Do not pass production samples through
that schema or mix them with metadata diagnostics. The production CSV still
needs its population release gate and Figure 6 rendering path.

`run_reactive.py` currently runs a metadata-propagation diagnostic over model
topologies. Its five stages construct derived dictionaries; they do not lower
operators or allocate storage. Its provider record identifies
`metadata-propagation/v1`, which the Figure 6 assembler and final release gate
reject. Raw diagnostics keep evaluator instruction counts separate from graph
visits. For this topology-preserving diagnostic, graph visits equal the
traversed operation-list size times the number of executed stages; counting
is outside the timed region. Older rows without that counter cannot be reused
as graph-visit measurements.
