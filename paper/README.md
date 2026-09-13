# FSE 2027 paper workspace

This directory contains the manuscript, frozen study contracts, baseline
implementations, and raw pilot records for the Joggle paper. Implemented
features are not automatically treated as research contributions. A result may
enter the paper only when its inputs, command, environment, and raw output are
preserved here.

## Target and thesis

The current target is the
[FSE 2027 Research Papers track](https://conf.researchr.org/track/fse-2027/fse-2027-papers).
The official deadline is October 2, 2026, Anywhere on Earth. Initial
submissions use the ACM `acmsmall` format and allow 18 pages of text and
figures plus 4 pages of references. Review is double anonymous and requires a
Data Availability section.

The paper studies one narrow claim: a compiler workbench can let neural-network
co-design researchers change semantics, loop structure, storage, numeric
formats, and artifact generation through typed module functions without adding
a central lowering registry. It does not claim that Joggle replaces production
runtimes or that fewer source lines imply better usability.

## Current evidence status

| Question | Evidence present | Blocking work |
| --- | --- | --- |
| Progressive representation | ONNX/TFLite decoding, one `Fn`/`Blk`/`Op`/`Val` IR, semantic expansion, explicit loops, storage planning, C and VM paths | Freeze and record model-level stage traces |
| Extension surface | Four frozen contracts; all Joggle implementations pass; the exact MatMul contract also passes through Joggle's ONNX, VM, and C paths; pinned TVM controls pass three contracts | Execute the pinned ONNX-MLIR system baseline; keep bare-MLIR observations component-level |
| Composition and safety | Transactional edits, rollback, verifier, stable printing, installation consumer, and deterministic language mutation tests | Freeze a fault and diagnostic matrix |
| Artifact quality | Ten numerical ONNX paths and several reproducible pilots; one structural block policy has paired exact-tile MobileNetV2 and non-exact SqueezeNet diagnostics | Isolated multi-model repetitions, dispersion, task accuracy, second machine, and a materially smaller generated-C gap |

The current generated-C pilots remain roughly 7--101 times slower than
one-thread ONNX Runtime, depending on the model. This is a blocking result, not
a hidden caveat. The manuscript must not claim superior speed, compatibility,
or extensibility until the corresponding controlled study is complete.

## Repository map

- [`manuscript.md`](manuscript.md): evidence-bounded working paper text.
- [`related-work.md`](related-work.md): claim-oriented comparisons grounded in
  primary papers and official documentation.
- [`extension-study.md`](extension-study.md): frozen extension protocol,
  fairness rules, and threats.
- [`extension-tasks.json`](extension-tasks.json) and [`tasks/`](tasks/):
  machine-readable task contracts and inputs.
- [`fixtures/`](fixtures/): reproducibly generated ONNX inputs shared by
  Joggle and system-level baselines; these are not model benchmarks.
- [`baselines/tvm/`](baselines/tvm/): pinned TVM build record and matched
  mechanism-level controls.
- [`baselines/onnx-mlir/`](baselines/onnx-mlir/): pinned protocol for the
  end-to-end neural-network compiler baseline; implementation pending.
- [`model-study.md`](model-study.md): model selection and staged compatibility
  protocol.
- [`data/`](data/): raw pilot records and provenance.

## Reproduction entry points

Measure the Joggle extension implementations from a configured build:

```sh
python3 paper/measure_extensions.py \
  --manifest paper/extension-tasks.json \
  --repo . \
  --tool build-san/joggle \
  --build build-san \
  --module-path examples \
  --module-path build-san/modules \
  --output paper/data/extension-footprint-pilot.csv
```

Verify the checked-in matched ONNX fixture with its pinned generation
dependencies:

```sh
python3.12 -m venv .venv-fixtures
.venv-fixtures/bin/python -m pip install -r paper/fixtures/requirements.txt
.venv-fixtures/bin/python paper/fixtures/generate.py --check
```

Measure the pinned TVM implementations after following
[`baselines/tvm/README.md`](baselines/tvm/README.md):

```sh
TVM_ROOT=/path/to/tvm \
TVM_LIBRARY_PATH=/path/to/tvm/build/lib \
PYTHONPATH=/path/to/tvm/python \
python3 paper/measure_baselines.py \
  --record paper/baselines/tvm/record.json \
  --contracts paper/extension-tasks.json \
  --repo . \
  --output paper/data/extension-tvm-pilot.csv
```

Other scripts in this directory each regenerate the correspondingly named CSV.
The provenance and interpretation boundary for every record is documented in
[`data/README.md`](data/README.md).

## Submission gate

An FSE submission is justified only if all of the following are complete:

1. every reported comparison task has a passing implementation or a preserved,
   documented unsupported outcome;
2. the selected model suite has controlled correctness, artifact, workspace,
   compilation, and isolated latency records;
3. tables and figures are generated from raw records rather than copied from
   prose;
4. an anonymized artifact reproduces on a second machine;
5. the ACM manuscript passes claim-to-evidence, citation, disclosure, and
   double-anonymity audits.

If these conditions are not met by the venue deadline, the correct outcome is
to continue the study for a later venue rather than weaken the task contracts
or overstate the pilots.
