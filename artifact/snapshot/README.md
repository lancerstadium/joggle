# EuroSys 2027 frozen evidence

The ordinary Git files in this directory contain the retained inputs and raw
records for the reported snapshot. No Release attachment or Git LFS service is
needed. `index.json` lists every archived file, its size, and its recorded
content digest. The eight compressed parts together are about 282 MiB; they
restore 2,790 files totaling about 367 MiB.

## Restore and verify

Run from the repository root, using Python 3.11 or later:

```sh
python3 artifact/snapshot.py restore
python3 -m pip install -r artifact/requirements.txt
python3 artifact/verify_exports.py --raw
```

Restoration writes into the visible `local/` directory. An existing identical
file is kept; a differing file causes an error rather than being overwritten.
`python3 artifact/snapshot.py verify` checks the archive without restoring its
files. Historical absolute paths are resolved against this checkout using the
original root recorded in the index.

## Contents

| Included | Purpose |
| --- | --- |
| Original ONNX models and numerical input fixtures | Re-run the measured workloads |
| Execution CSVs and collection records | Operator and model measurements |
| Update samples and collection records | Paired update/rebuild measurements |
| Scheduler samples and job records | Retained-graph scheduling measurements |
| Package sources, observations and admission records | Integration and maintenance comparisons |
| Agent prompts, trajectories, candidates, checkpoints and oracle reports | Inspect the stopped collection and actual outputs |
| Reference admission reports | Connect checked implementations to exported source counts |

The Agent collection is preserved as collected, including interrupted and
unstarted conditions; no missing runs have been filled in. Existing exports
and original records are not rewritten to point at a different compiler build.

## Re-running versus checking

The commands above verify saved evidence; they do not rerun timing experiments.
Use the [evaluation guides](../README.md) for collectors, workload parameters,
external compiler dependencies and plotting. A rerun requires installing those
dependencies on the target machine and will produce new timings.

The measured core revision `cc82ef114093b6d90ca94df05b54a60084585e72` and
scheduler repair `8735b0f` are ancestors of the tagged source. Clone the Git
repository without `--depth` to retain those versions. Do not substitute the
latest compiler for a recorded revision when attempting historical reproduction.

Shape-inferred ONNX copies are intentionally not duplicated in the archive.
Regenerate them from the included models with
`python3 paper/measure_scheduler.py --prepare-only` after installing the ONNX
version recorded in `paper/data/reactive-input-preparation.json`.

Not included: virtual environments, installed third-party libraries, compiled
executables, obsolete exploratory experiments, and repeatable build products.
Those local originals have not been deleted. The tag freezes the reported
evidence, not every intermediate file from project development.
