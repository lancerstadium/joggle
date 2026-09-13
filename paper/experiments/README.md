# Cross-system experiments

Each JSON file freezes one process-level comparison. A subject is an external
system command, not a variant label inside Joggle. Every command must perform
its own untimed setup and warm-up, time exactly one inference, validate its
output, and write one CSV row with:

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

Run the MobileNetV2 comparison after building the ONNX application gate:

```sh
python3 paper/measure_systems.py \
  --manifest paper/experiments/mobilenetv2.json \
  --repo . \
  --output paper/data/mobilenetv2-systems.csv \
  --record paper/data/mobilenetv2-systems.json \
  --max-load1 2 --require-clean
```

Linux experiment hosts may additionally pass `--cpu N`. Unsupported affinity
requests fail rather than silently producing an unpinned record.

Correctness thresholds belong in each subject command. The checked-in
MobileNetV2 manifest uses the same `1e-4 + 1e-4 * abs(reference)` elementwise
bound for generated C and ONNX Runtime. Checksums expose nondeterminism but are
not required to match across systems whose legal floating-point evaluation
orders differ.
