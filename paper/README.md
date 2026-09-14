# EuroSys 2027 paper workspace

This directory contains the manuscript, frozen study contracts, baseline
implementations, and raw pilot records for the Joggle paper. Implemented
features are not automatically treated as research contributions. A result may
enter the paper only when its inputs, command, environment, and raw output are
preserved here.

## Target and thesis

Working title: **Joggle: Whole Compiler Extensions as Typed Modules**.

The current target is the
[EuroSys 2027 fall cycle](https://2027.eurosys.org/cfp.html). Titles and
abstracts are due September 17, 2026, and full papers are due September 24,
2026; both deadlines are Anywhere on Earth. A submission has at most 12 pages
of technical content plus references, uses a two-column 10-point-or-larger
format, and is double blind. Repository links must therefore point to an
anonymous snapshot rather than this development repository. EuroSys also
requires disclosure of AI-tool use under ACM policy.

The paper studies **whole compiler extensions**: cross-role research ideas made
into typed, distributable modules with extension continuity. A module may add program vocabulary, analysis,
transformation, target choice, or artifact behavior through the same dependency,
invocation, installation, rollback, and upgrade model. Progressive exposure
lets consumers request additional implementation detail without forcing the
entire program through a fixed lowering sequence. Emerging hardware, inference,
and generated optimization policies are demanding instances of this problem,
not definitions of the system.

The claim is not that one IR is universally superior, that all compiler concepts
are semantically identical, or that fewer source lines prove usability. The
required evidence includes cross-role module composition, clean package
lifecycle and diagnostics, controlled revision studies, chooser substitution,
generic target policies that improve complete artifacts, and explicit
representation/failure boundaries. Inference and emerging hardware provide the
main stress domain because they exercise every role at once; the mechanism is
not operator- or device-specific.

## Current evidence status

| Question | Evidence present | Blocking work |
| --- | --- | --- |
| Progressive representation | ONNX/TFLite decoding, one `Fn`/`Blk`/`Op`/`Val` IR, semantic expansion, explicit loops, storage planning, C and VM paths; ten models complete semantic conversion, while XCiT-Tiny infers all 1,333 initially unknown results and leaves seven dynamic positional-embedding calls | Close or preserve the seven-call XCiT conversion boundary, then reproduce the frozen frontier and executable subset in the anonymous artifact |
| Extension surface | Four frozen contracts; all Joggle implementations pass; pinned TVM controls pass three contracts and preserve an unsupported custom-type boundary; ONNX-MLIR passes the implementation and policy tasks, has a preserved unsupported external-call boundary, and carries the numeric-format fixture through a parameterized type to exact native execution before stopping at the required second executable target | Repeat the ONNX-MLIR build without interruption from a clean checkout |
| Composition and safety | Transactional edits, rollback, verifier, stable printing, installation consumer, deterministic mutation tests, and byte-identical CSE/analysis scaling pilots | Freeze a fault and diagnostic matrix; do not expand parser/printer internals unless a case exposes a correctness defect |
| Artifact quality | Ten numerical ONNX paths; clean-revision, 20-trial MobileNetV2 and MNIST records compare generated C with ONNX Runtime on two shared-runner CPU classes; internal rewrite diagnostics remain separate; a TFLite-to-C application study and matched LiteRT diagnostic are reproducible in `linux-tflite` | Validate the TFLite study on Linux, then add an identified isolated host with dispersion and task accuracy, or narrow the paper claim explicitly to artifact correctness and transformation reach |

The same-job fusion diagnostic at revision `f8ade71` builds unfused and fused
MobileNetV2 artifacts from one canonical IR and validates both against the
pinned reference. Greedy `tile.fuse` reduces represented loops from 156 to 110
and C source from 192,209 to 188,244 bytes, but increases the generated-C
median from 356.360 to 384.927 ms on the pinned runner CPU. The adjacent ONNX
Runtime medians are 10.359 and 10.447 ms. This negative result motivates a
target-owned profitability policy; it is not evidence that fusion improves
latency.

The `linux-tflite` application gate at revision `03f8e25` completes the second
frontend through strict generated C. Its output agrees with the LiteRT 2.2.0
oracle within `1.0132789611816406e-6`. On one pinned shared-runner CPU, the
generated-C and one-thread LiteRT medians are 225.667 and 6.424 ms, a 35.13x
gap. This is a reproducible correctness and artifact result plus a negative
runtime diagnostic, not isolated-host performance.

The current generated-C pilots remain slower than one-thread ONNX Runtime. At
revision `418a34e`, the balanced GitHub Linux smoke run reports MobileNetV2 at
167.315 versus 10.346 ms (16.17x) and MNIST at 0.530 versus 0.050 ms (10.55x)
over 20 fresh-process trials. A separate same-runner MobileNetV2 diagnostic
reports 139.679 ms for the ordinary artifact and 102.638 ms after generic loop
reordering and affine-index canonicalization, with adjacent ONNX Runtime
medians of 8.387 and 8.382 ms. Thus the module policy reduces latency by 26.5%
and generated source by 20.8% within that runner, but remains 12.24x slower
than the adjacent production runtime. A separate dispatch reproduces
byte-identical artifacts and correct outputs on an Intel Xeon runner; its
MobileNetV2 and MNIST gaps are 23.66x and 10.59x. These are blocking results,
not hidden caveats: the shared runners lack a host-load bound, controlled
thermal state, and an isolated independently managed host. The manuscript must not claim
superior speed, compatibility, or extensibility until the corresponding study
is complete.

## Repository map

- [`manuscript.md`](manuscript.md): evidence-bounded working paper text.
- [`references.bib`](references.bib): source-verified bibliography for every
  related-work citation currently used by the manuscript.
- [`literature-map.md`](literature-map.md): the claim-oriented evidence map,
  strongest competing explanations, falsifiable experiments, and reading queue
  used to grow the bibliography beyond 50 actually cited works.
- [`operator-study.md`](operator-study.md): exact shape grids, speedup
  convention, baseline fairness, LaTeX layout, and optimization gate for the
  dense operator table.
- [`operator_suite.py`](operator_suite.py) and
  [`prepare_operator_case.py`](prepare_operator_case.py): deterministic
  generation of 441 operator/subgraph cases (216 contraction and Transformer
  cases plus 225 pointwise/normalization cases), followed by generated-C,
  independent-weight, correctness, and balanced cross-system manifests for
  every table cell.
- [`extension-study.md`](extension-study.md): frozen extension protocol,
  fairness rules, and threats.
- [`extension-tasks.json`](extension-tasks.json) and [`tasks/`](tasks/):
  machine-readable task contracts and inputs.
- [`fixtures/`](fixtures/): reproducibly generated ONNX inputs shared by
  Joggle and system-level baselines; these are not model benchmarks.
- [`baselines/tvm/`](baselines/tvm/): pinned TVM build record and matched
  mechanism-level controls.
- [`baselines/onnx-mlir/`](baselines/onnx-mlir/): pinned protocol, sources,
  artifacts, passing tasks, and preserved unsupported boundaries for the
  end-to-end neural-network compiler baseline.
- [`model-study.md`](model-study.md): model selection and staged compatibility
  protocol.
- [`data/`](data/): raw pilot records and provenance.
- [`experiments/`](experiments/): frozen cross-system command manifests. The
  runner requires one subject per independent system, rotates process trials,
  and rejects a busy host when a publication run supplies a load threshold.
- [`prepare_tflite.py`](prepare_tflite.py),
  [`make_tflite_fixture.py`](make_tflite_fixture.py), and
  [`bench_litert.py`](bench_litert.py): the second-frontend application gate,
  deterministic LiteRT oracle, and matched one-thread runtime command.

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

The builder gives every variant the same external ABI contract.  After
storage planning, `c.noalias` records disjoint exported tensor arguments and
`c.place` assigns storage.  The CSV keeps the no-alias stage's time, byte
count, and hash separate so the contract is auditable rather than hidden in
placement.

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

With the abstract deadline on September 17 and the paper deadline on September
24, work is ordered by claim risk rather than component completeness. As of
September 14, the abstract/title freeze has three days and the full evidence
package has ten days:

1. freeze the title, abstract, author list, conflicts, and scope of the artifact
   claim for abstract registration;
2. run the frozen whole-model manifests on an identified, otherwise-idle Linux
   host and report latency dispersion, peak workspace, compile time, executable
   size, weight size, and numerical/task correctness;
3. fill the dense operator matrices against one-thread ONNX Runtime first, then
   retain TVM and ONNX-MLIR only where identical model, input, thread, target,
   and timing boundaries can be reproduced;
4. close the extension matrix with a passing result or preserved unsupported
   outcome for every retained comparison task;
5. generate the model-level small multiples and native-LaTeX speedup tables
   from raw records, then complete the 12-page anonymous manuscript and audits;
6. defer parser/printer/verifier refactoring and new frontend or backend breadth
   unless they block one of the preceding experiments.

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
