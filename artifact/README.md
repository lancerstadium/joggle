# EuroSys artifact

This directory contains measurement programs for the paper. It is independent
of `test/`: tests reject implementation regressions, whereas artifact programs
produce versioned observations for statistical analysis.

## Reactive-update experiment

`run_reactive.py` builds two Release configurations, runs the five policies
from Section 4.4, merges their rows, checks graph equivalence, and writes a JSON
run record beside the CSV.

| Policy | Execution rule |
| --- | --- |
| `full` | execute all five compiler stages |
| `suffix` | execute the conservative affected suffix |
| `reactive` | validate recorded entity dependencies |
| `whole-mod` | use the same stages with one mod-wide observation |
| `no-plan-cache` | use reactive selection but decode plans again |

The generated subject contains independent affected and unrelated chains. Both
edits occur in the same function, so function- or mod-granular policies rerun
the pipeline; entity-granular validation can reject the unrelated edit without
executing a stage. The CSV records actual IR operation counts rather than the
requested generator size. Total nodes, affected nodes, fan-out, and stage count
are separate generator parameters.

The same executable also accepts pinned ONNX files. It decodes the complete
model, selects pre-registered early, middle, or late single-output computations,
and chooses a same-function operation outside each computation's affected cone.
The current edit classes are `no_op`, which measures stable scheduling and
cache overhead, and `operation_metadata`, which changes metadata at either the
affected or unrelated scope. Every row records the model SHA-256 supplied by
the runner and the exact selected sites.

Fetch the 15 standard cases and the separately gated heavy case. The download
script checks every file against the shared pinned manifest:

```sh
cmake -DOUT=.cache/onnx-zoo -P test/tools/fetch_onnx_zoo.cmake
cmake -DOUT=.cache/onnx-zoo -DMODELS=bidaf-9 \
  -P test/tools/fetch_onnx_zoo.cmake
```

Run a short end-to-end check:

```sh
python3 artifact/run_reactive.py \
  --output .cache/artifact/reactive-smoke.csv \
  --nodes 1000 --affected 1 8 64 \
  --fanout 1 --stages 5 \
  --warmups 1 --iterations 3 --allow-dirty
```

Run the model-backed track over an already downloaded pinned corpus:

```sh
python3 artifact/run_reactive.py \
  --output .cache/artifact/reactive-models.csv \
  --models .cache/onnx-zoo/*.onnx \
  --sites early middle late \
  --stages 5 --warmups 10 --iterations 100
```

Run the planned scaling matrix from a clean revision:

```sh
python3 artifact/run_reactive.py \
  --output .cache/artifact/reactive.csv \
  --nodes 1000 10000 100000 1000000 \
  --affected 1 8 64 512 \
  --fanout 1 --stages 5 \
  --warmups 10 --iterations 100
```

Validate an existing result independently:

```sh
python3 artifact/validate_reactive.py .cache/artifact/reactive.csv
```

The committed schema is `schemas/reactive.schema.json`. Raw CSV and JSON run
records remain outside Git until the hardware, operating system, compiler, and
CPU policy are frozen for the paper's reported run.
