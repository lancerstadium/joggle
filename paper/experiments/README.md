# Cross-system experiments

Each schema-2 JSON file freezes one process-level comparison. `system` is the
identity of an independently implemented compiler or runtime, not a variant
label. The runner requires every system identity to appear exactly once, so a
Joggle/Joggle configuration comparison cannot enter this experiment path.
Pass regressions and ablations use separate diagnostic scripts. This guard
prevents accidental reuse of one declared identity; it does not prove that two
differently named commands are independently implemented, which remains a
provenance-review obligation. Every command must perform its own untimed setup
and warm-up, time exactly one inference, validate its output, and write one CSV
row with:

```text
iteration,seconds,checksum
0,0.001234567,0123456789abcdef
```

`measure_systems.py` launches a fresh process for every subject in every
trial, balances rotated and reversed execution order over complete cycles,
forces common one-thread environment variables, hashes all declared artifacts,
and records repository and host state. It
rejects malformed output, failed numerical validation, a dirty tree when
`--require-clean` is set, unavailable requested CPU affinity, and a busy host
when `--max-load1` is exceeded. Commands are killed after 900 seconds by
default; `--timeout-seconds` changes that bound and records it with the run.

The manifest does not make a run controlled by itself. Publication records
must use a clean checkout, an explicit host-specific load threshold, stable
power and thermal conditions, the same subject manifest on a second machine,
and raw task-accuracy checks where the model has a dataset-level metric.

The immutable schema-1 manifests under `pilot/` reproduce the manifest hashes
stored by the two historical pilot records. They are provenance snapshots, not
inputs accepted by the current runner.

Run a comparison after building the corresponding ONNX application gate. For
example, the MobileNetV2 study is:

```sh
python3 paper/measure_systems.py \
  --manifest paper/experiments/mobilenetv2.json \
  --repo . \
  --output paper/data/mobilenetv2-systems.csv \
  --record paper/data/mobilenetv2-systems.json \
  --max-load1 2 --require-clean
```

The same protocol has a separate checked-in manifest for the official MNIST
model. It deliberately names its own model, generated artifact, input, and
reference rather than treating another Joggle configuration as a subject:

```sh
python3 paper/measure_systems.py \
  --manifest paper/experiments/mnist.json \
  --repo . \
  --output paper/data/mnist-systems.csv \
  --record paper/data/mnist-systems.json \
  --max-load1 2 --require-clean
```

Linux experiment hosts may additionally pass `--cpu N`. Unsupported affinity
requests fail rather than silently producing an unpinned record.

The repository's `linux-performance` workflow is the reproducible CI smoke
run of this protocol. It uses a GitHub-managed Linux runner, a Release build,
the dependencies frozen in `requirements.txt`, one dynamically selected CPU,
and uploads the raw CSV and provenance JSON. Its numbers are useful for
cross-revision diagnosis, but a shared virtual runner is not a controlled
publication machine. Final latency claims still require an identified,
otherwise-idle Linux host and replication on a second machine.

`mobilenetv2-block.json` is a second independent-system comparison whose
Joggle subject is produced by the out-of-tree `spatial.block` source policy.
The `linux-policy` workflow starts from the preserved `canonical.jog`, composes
the public `tile` transforms, rebuilds the ordinary C artifact, and validates
it. On one pinned CPU it then runs the ordinary and blocked manifests
consecutively and reports both Joggle medians and both adjacent ONNX Runtime
medians. This same-job diagnostic avoids comparing different GitHub runners
while preserving the runner's rule that each manifest contains independent
systems rather than two configurations of Joggle.

Correctness thresholds belong in each subject command. The checked-in
MobileNetV2 manifest uses the same `1e-4 + 1e-4 * abs(reference)` elementwise
bound for generated C and ONNX Runtime. Checksums expose nondeterminism but are
not required to match across systems whose legal floating-point evaluation
orders differ.
