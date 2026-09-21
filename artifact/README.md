# EuroSys artifact

This directory contains measurement programs for the paper. It is independent
of `test/`: tests reject implementation regressions, whereas artifact programs
produce versioned observations for statistical analysis.

`PROTOCOL.md` is the frozen claim-to-measurement contract. Figure-ready CSV
headers live under `templates/`, and `figures/` contains one plotting script per
data figure. A plotting script reads only its named CSV and writes both vector
PDF and review PNG; it never invokes Joggle or changes measurements.

| Figure | Evidence | CSV | Plotting entry |
| --- | --- | --- | --- |
| 4 | extension completion | `figure-04-extension.csv` | `figure_04_extension.py` |
| 5 | paired patch footprint | `figure-05-footprint.csv` | `figure_05_footprint.py` |
| 6 | model-backed reactive updates | `figure-06-model-update.csv` | `figure_06_model_update.py` |
| 7 | generated-graph scaling | `figure-07-scaling.csv` | `figure_07_scaling.py` |
| 8 | operator artifact quality | `figure-08-operators.csv` | `figure_08_operators.py` |
| 9 | model artifact quality | `figure-09-models.csv` | `figure_09_models.py` |

## Reactive-update experiment

`run_reactive.py` builds two Release configurations, runs the four policies
from Section 4.4, merges their rows, checks graph equivalence, and writes a JSON
run record beside the CSV.

| Policy | Execution rule |
| --- | --- |
| `full` | execute all five compiler stages |
| `reactive` | validate recorded entity dependencies |
| `whole-mod` | use the same stages with one mod-wide observation |
| `no-plan-cache` | use reactive selection but decode plans again |

The generated subject contains independent affected and unrelated chains. Both
edits occur in the same function, so function- or mod-granular policies rerun
the pipeline; entity-granular validation can reject the unrelated edit without
executing a stage. Every policy begins with the same properties initialized on
the complete function. Timed Full stages rescan that function, while timed
Reactive stages retain their recorded root and traverse only its affected cone.
The CSV records actual IR operation counts rather than the requested generator
size. Total nodes, affected nodes, fan-out, and stage count are separate
generator parameters.

The same executable also accepts pinned ONNX files. It decodes the complete
model, selects pre-registered early, middle, or late single-output computations,
and chooses a same-function operation outside each computation's affected cone.
The current edit classes are `no_op`, which measures stable scheduling and
cache overhead; `operation_metadata`; and `value_type`. The latter two operate
at either the affected or unrelated scope. Every row records the model SHA-256
supplied by the runner and the exact selected sites.

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
  --output .cache/artifact/figure-07-scaling-smoke.csv \
  --nodes 1000 --affected 1 8 64 \
  --fanout 1 --stages 5 \
  --warmups 1 --iterations 3 --allow-dirty
```

Run the model-backed track over an already downloaded pinned corpus:

```sh
python3 artifact/run_reactive.py \
  --output .cache/artifact/figure-06-model-update.csv \
  --model-manifest artifact/manifests/reactive-models.csv \
  --model-root .cache/onnx-zoo \
  --sites early middle late \
  --stages 5 --warmups 10 --iterations 100
```

Run the planned scaling matrix from a clean revision:

```sh
python3 artifact/run_reactive.py \
  --output .cache/artifact/figure-07-scaling.csv \
  --nodes 1000 10000 100000 1000000 \
  --affected 1 8 64 512 \
  --edit-classes operation_metadata --scopes affected \
  --fanout 1 --stages 5 \
  --warmups 10 --iterations 100
```

Validate an existing result independently:

```sh
python3 artifact/validate_reactive.py .cache/artifact/figure-07-scaling.csv
```

Render a validated result without modifying it:

```sh
python3 artifact/figures/figure_06_model_update.py \
  .cache/artifact/figure-06-model-update.csv \
  --output .cache/artifact/figure-06-model-update.pdf

python3 artifact/figures/figure_07_scaling.py \
  .cache/artifact/figure-07-scaling.csv \
  --output .cache/artifact/figure-07-scaling.pdf
```

Figures 4, 5, 8, and 9 use the shared structural validator; `--allow-partial`
is reserved for collection-time checks and is not accepted by the release gate:

```sh
python3 artifact/validate_figure.py 4 .cache/artifact/figure-04-extension.csv
python3 artifact/validate_figure.py 5 .cache/artifact/figure-05-footprint.csv
python3 artifact/validate_figure.py 8 .cache/artifact/figure-08-operators.csv
python3 artifact/validate_figure.py 9 .cache/artifact/figure-09-models.csv
```

The committed schema is `schemas/reactive.schema.json`. Raw CSV and JSON run
records remain outside Git until the hardware, operating system, compiler, and
CPU policy are frozen for the paper's reported run.
