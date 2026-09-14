# EuroSys 2027 paper workspace

This directory contains the manuscript, frozen study contracts, baseline
implementations, and raw pilot records for the Joggle paper. Implemented
features are not automatically treated as research contributions. A result may
enter the paper only when its inputs, command, environment, and raw output are
preserved here.

## Target and thesis

Working title: **Joggle: One Function IR for Extensible Inference
Compilation**.

The current target is the
[EuroSys 2027 fall cycle](https://2027.eurosys.org/cfp.html). Titles and
abstracts are due September 17, 2026, and full papers are due September 24,
2026; both deadlines are Anywhere on Earth. A submission has at most 12 pages
of technical content plus references, uses a two-column 10-point-or-larger
format, and is double blind. Repository links must therefore point to an
anonymous snapshot rather than this development repository. EuroSys also
requires disclosure of AI-tool use under ACM policy.

The paper studies one compiler claim: a progressive function IR can make
inference compilation malleable across source semantics, loop structure,
storage policy, and executable artifacts. Ordinary module functions expose and
rewrite those layers without adding a central lowering registry or rebuilding
the compiler. Here, *malleable* has a narrow testable meaning: a separately
distributed module can discover a represented decision, replace it through the
public IR API, and carry the edit to executable output without adding a native
IR kind or a central dispatch case. Extension surface, compile cost, code size,
correctness, workspace, and latency all belong to that claim. Joggle does not
claim to replace production runtimes or equate fewer source lines with better
usability.

## Current evidence status

| Question | Evidence present | Blocking work |
| --- | --- | --- |
| Progressive representation | ONNX/TFLite decoding, one `Fn`/`Blk`/`Op`/`Val` IR, semantic expansion, explicit loops, storage planning, C and VM paths | Freeze and record model-level stage traces |
| Extension surface | Four frozen contracts; all Joggle implementations pass; pinned TVM controls pass three contracts; the exact MatMul implementation passes through native ONNX-MLIR; its documented external-call option has a preserved unsupported MatMul outcome | Finish the policy and numeric-format system tasks or preserve their unsupported outcomes; repeat the ONNX-MLIR build from a clean checkout |
| Composition and safety | Transactional edits, rollback, verifier, stable printing, installation consumer, deterministic mutation tests, and byte-identical CSE/analysis scaling pilots | Freeze a fault and diagnostic matrix |
| Artifact quality | Ten numerical ONNX paths; clean-revision, 20-trial MobileNetV2 and MNIST pilots compare generated C with ONNX Runtime; internal rewrite diagnostics remain separate | Broaden the independent-system matrix, add ONNX-MLIR where executable, task accuracy, an isolated second machine, and a materially smaller generated-C gap |

The current generated-C pilots remain slower than one-thread ONNX Runtime. At
revision `418a34e`, the balanced GitHub Linux smoke run reports MobileNetV2 at
167.315 versus 10.346 ms (16.17x) and MNIST at 0.530 versus 0.050 ms (10.55x)
over 20 fresh-process trials. A separate same-runner MobileNetV2 diagnostic
reports 139.679 ms for the ordinary artifact and 102.638 ms after generic loop
reordering and affine-index canonicalization, with adjacent ONNX Runtime
medians of 8.387 and 8.382 ms. Thus the module policy reduces latency by 26.5%
and generated source by 20.8% within that runner, but remains 12.24x slower
than the adjacent production runtime. These are blocking results, not hidden
caveats: the shared runners lack a host-load bound, controlled thermal state,
and isolated second-machine replication. The manuscript must not claim
superior speed, compatibility, or extensibility until the corresponding study
is complete.

## Repository map

- [`manuscript.md`](manuscript.md): evidence-bounded working paper text.
- [`references.bib`](references.bib): source-verified bibliography for every
  related-work citation currently used by the manuscript.
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
  end-to-end neural-network compiler baseline; the first native task is
  preserved and the remaining contracts are pending.
- [`model-study.md`](model-study.md): model selection and staged compatibility
  protocol.
- [`data/`](data/): raw pilot records and provenance.
- [`experiments/`](experiments/): frozen cross-system command manifests. The
  runner requires one subject per independent system, rotates process trials,
  and rejects a busy host when a publication run supplies a load threshold.

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

The remaining paired-variant scripts are internal pass diagnostics. They test
structural mutation, numerical preservation, and code-growth bounds; they must
not be used as the paper's system-performance comparison. The publication
runner is [`measure_systems.py`](measure_systems.py), whose subjects are
independent external commands declared under [`experiments/`](experiments/).

For pass regression work only, build matched original and rewritten sources
from an already prepared model, then run the alternating harness:

```sh
python3 paper/measure_reorder.py \
  --tool build/joggle \
  --modules build/modules \
  --examples examples \
  --model UltraFace=build-study/ultraface-block/canonical.jog \
  --out-dir build-study/reorder/UltraFace-study \
  --output paper/data/reorder-ultraface-pilot.csv

python3 paper/measure_pair.py \
  --model UltraFace --cc /usr/bin/clang \
  --baseline build-study/reorder/UltraFace-study/UltraFace/baseline/model.c \
  --candidate reorder=build-study/reorder/UltraFace-study/UltraFace/reorder/model.c \
  --candidate canon=build-study/reorder/UltraFace-study/UltraFace/canon/model.c \
  --input build-matrix/ultraface-rfb-320/input.bin \
  --weights build-study/ultraface-block/weights.bin \
  --output-count 8840 --output-count 17680 --repetitions 20 \
  --out-dir build-study/reorder/UltraFace-study/UltraFace/paired \
  --output paper/data/reorder-ultraface-runtime-pilot.csv
```

Measure the structural-cost frontier of an already prepared model without
placing artifacts in a temporary directory:

```sh
python3 paper/measure_block_frontier.py \
  --tool build/joggle \
  --modules build/modules \
  --examples examples \
  --model UltraFace=build-study/ultraface-block/canonical.jog \
  --budgets 0,500,1500,1000000 \
  --out-dir build-study/block-frontier \
  --output paper/data/block-frontier-pilot.csv
```

`paper/measure_pair.py` then strictly compiles one baseline and any number of
candidates, generates an alternating harness for the declared result arity,
and writes every raw timing observation. The caller supplies managed input and
weight artifacts; neither script downloads or invents model data.

```sh
python3 paper/measure_pair.py \
  --model UltraFace \
  --cc /usr/bin/clang \
  --baseline build-study/block-frontier/UltraFace/0/model.c \
  --candidate 500=build-study/block-frontier/UltraFace/500/model.c \
  --candidate 1500=build-study/block-frontier/UltraFace/1500/model.c \
  --candidate 1000000=build-study/block-frontier/UltraFace/1000000/model.c \
  --input build-matrix/ultraface-rfb-320/input.bin \
  --weights build-study/ultraface-block/weights.bin \
  --output-count 8840 --output-count 17680 --repetitions 10 \
  --out-dir build-study/block-frontier/UltraFace/paired \
  --output paper/data/block-frontier-runtime-pilot.csv
```

## Submission gate

A EuroSys submission is justified only if all of the following are complete:

1. every reported comparison task has a passing implementation or a preserved,
   documented unsupported outcome;
2. the selected model suite has controlled correctness, artifact, workspace,
   compilation, and isolated latency records;
3. tables and figures are generated from raw records rather than copied from
   prose;
4. an anonymized artifact reproduces on a second machine;
5. the 12-page manuscript passes claim-to-evidence, citation, AI-disclosure,
   and double-anonymity audits.

If these conditions are not met by the venue deadline, the correct outcome is
to continue the study for a later venue rather than weaken the task contracts
or overstate the pilots.
