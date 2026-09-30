# Extensions and Agent collection

[Evaluation entry point](../README.md). Run all commands from the repository root.

Validate the task contract, execute the native task oracles, and then collect
model/system trajectories for assembly into one CSV:

```sh
python3 artifact/validate_extension_specs.py
```

`run_extension_task.py` executes candidate code against the shared contract.
The primary agent/footprint set consists of the twelve tasks marked
`footprint: true` in `manifests/extension-specs.json`. Its native references
cover all three systems, including `def-quantized-op`, `vert-int4`, and
`vert-fused-op`. Vertical tasks first transform and verify the native SSA
graph, then emit C that is compiled and checked on runtime inputs. The input
tensors are not embedded in the compiler fixture. Empty starters must fail the
complete oracle; a retained negative rewrite case alone is not task completion.

The native tasks include `ana-broadcast-shape`, `ana-storage-cost`,
`ana-numeric-range`, `ana-fusion-match`, `emit-storage-plan`,
`emit-target-capability`, `emit-graph-manifest`, `rew-add-zero`, and
`rew-redundant-cast`, `rew-transpose-pair`, `con-instruction-select`, and
`con-gelu-expand`, `con-quant-expand`, `con-layout-legalize`, and
`rew-conv-bias-relu`, `emit-kernel-wrapper`, and `def-parametric-type`.
Each task directory under `extensions/` contains reference implementations for
Joggle, MLIR, and xDSL; the shared `starter.*` files contain their empty entry
points. Attribute-analysis tasks read a request from function metadata in
Joggle or a builtin module dictionary in MLIR and xDSL. The driver
only parses the input and calls the candidate. Shape analysis runs in native
extension code, and expected outputs remain in the external oracle.

```sh
python3 artifact/run_extension_task.py \
  --task ana-broadcast-shape --system Joggle \
  --source artifact/extensions/ana-broadcast-shape/reference.jog \
  --joggle build/joggle --builtin-mods build/modules \
  --build-root local/cache/artifact/extension-tasks \
  --output local/cache/artifact/extension-reference-joggle.json
```

For MLIR, pass `--system MLIR`, a `.cpp` source, and `--mlir-dir` pointing to
the pinned build's `lib/cmake/mlir`. For xDSL, pass `--system xDSL`, a `.py`
source, and `--xdsl-python` pointing to the pinned environment's interpreter.
Run each reference and its empty starter: the reference must pass every case,
and the starter must fail. Reports contain per-case process output and exact
oracle comparisons. They are task-validation records, not agent trajectories.
Candidate code from an agent runs inside its isolated execution environment.

The attribute tasks include numeric range propagation through binary64 compiler
metadata, with fractional and negative inputs. `ana-fusion-match` instead
receives a real typed SSA call graph in all three systems, with no request
dictionary. Its eight cases exercise NCHW/NHWC layout, an interleaved unrelated
call, extra consumers on either fusion edge, bias rank and channel mismatch,
and an unmatched activation. The extension reads tensor types, defining
operations, and use lists through each framework's native APIs. Match positions
refer to the actual call sequence, not a precomputed input descriptor.
Emission oracles also require byte-identical outputs across repeated runs.
These reference checks validate task execution, not agent completion rates.

`emit-graph-manifest` walks actual native SSA operations, tensor types, and
operation attributes. Eight fixtures cover linear and shared-branch graphs,
repeated operands/returns, multiple results, dynamic dimensions, rank-zero
tensors, ordered mixed attributes, and identity graphs. The expected manifests
are stored only in the oracle contract; native fixtures contain no request
dictionary or expected output. Every case checks the full manifest and repeats
emission to check byte stability.

`rew-add-zero` requires a native in-place rewrite. Its `rewrite-starter.*`
files expose `transform` rather than `analyze`. The three reference extensions
modify a common small tensor dialect represented by typed SSA calls, with
scalar splat attributes and explicit floating-point `no_signed_zeros` flags.
Nine cases cover both operand sides, scalar splats, shared constants, rank-zero
tensors, nonzero constants, signed zero, and shape-changing broadcasts.

The candidate process emits IR, not an answer dictionary. A second process
parses that IR and extracts types, operands, constants, and returns. The oracle
compares the full resulting graph and evaluates the original and rewritten
graphs on five input sets. Strict floating cases compare output bits; explicit
no-signed-zeros cases compare numerical values. No-op starters pass the four
preservation cases but fail all five required rewrites and the complete task.
Reports retain transformed IR, independent observations, and numerical bits.
NumPy evaluates these small graphs; xDSL is the common post-IR observer for
MLIR and xDSL output. Run the oracle with the pinned experiment interpreter,
or supply its path with `--xdsl-python` when using the MLIR provider.

`rew-redundant-cast` uses the same mutation and independent observation path.
Ten fixtures cover identity, signed integer and finite floating-point widening
round trips, shared intermediate users, narrowing, float/integer conversions,
shape changes, and single widening casts. Input sets include signed zero,
fractional values, and integer wraparound boundaries. Required rewrites and
required preservation each account for five cases; a no-op does not pass the
task. Reference results validate these oracles, not an agent's success rate.

`rew-transpose-pair` cancels inverse permutations on native SSA calls. Typed
callee names encode the element type and source/result shapes so all three
systems use the same monomorphic signatures; `perm` attributes carry the
actual permutations. Eleven cases cover rank-four and self-inverse pairs,
shared intermediates, rank-zero and empty tensors, integers, non-inverse pairs
including equal dimensions, and malformed permutations. Valid results are
independently parsed and checked structurally and bitwise on five input sets.
Malformed permutations must produce the specified diagnostic and no output
IR. A no-op starter fails all required eliminations and rejection cases.

`con-instruction-select` retargets native matrix-product calls to a declared
tiled target and attaches the selected tile counts. Ten cases cover single
and multiple tiles, repeated result users, each ragged dimension, non-f16
inputs, zero extents, and batched products. The observer checks the complete
post-conversion graph, preserved metadata, and tile order; the numerical oracle
checks f32-accumulation semantics on five input sets. This task measures compiler
extension correctness, not GPU instruction throughput.

`con-gelu-expand` replaces a native GELU call with arithmetic and erf calls.
Seven cases cover f32/f64 tensors, scalars, repeated output users, empty tensors,
and rejected integer/half inputs. The oracle accepts equivalent target graphs
without prescribing node order or parenthesization. It requires GELU removal,
checks output types, and compares the independently observed graph against
binary64 GELU semantics on seven input sets using the task's dtype-specific
tolerances. An unchanged GELU call or an arithmetically incorrect expansion
fails. Constant values remain explicit IR attributes, not driver-side answers.

`con-quant-expand` replaces native qadd with dequantize, floating add, and
quantize operations. Ten cases cover mixed scales, ties-to-even, odd output
zero points, independent input zero points, both saturation limits, repeated
outputs, scalars, and nonpositive scales. Target graphs are checked on five
deterministic input pairs and the case's explicit values. The latter also have
hand-specified integer answers in the task contract. Integer outputs must match
exactly. Zero points are added after rounding, and source data are not embedded
in the native IR presented to the candidate.

`con-layout-legalize` transforms NHWC convolution into NCHW convolution with
explicit input, weight, and output transposes. Seven cases cover non-square
shapes, asymmetric padding, strides, dilation, pointwise and single-channel
convolution, repeated users, and channel-mismatch rejection. The observer
checks the emitted graph and bitwise output equivalence on six input sets,
including a fixed-seed nonperiodic set that distinguishes spatial permutations.
The spatial reference is also regression-tested against ONNX Runtime on all
six valid shapes and against a hand-calculated cross-correlation example.

`rew-conv-bias-relu` performs native Conv/BiasAdd/ReLU fusion with complete
attribute and output-use preservation. Nine cases cover NCHW/NHWC, stride,
asymmetric padding, dilation, repeated final outputs, shared intermediates,
an incompatible activation, and a shape-compatible but incorrect bias axis.
The latter four must remain unchanged. Both the exact observed graph and
bitwise f32 outputs on six input sets are checked. Fused semantics preserve
the original convolution accumulation, then bias addition, then ReLU; this
task measures extension correctness, not fused-kernel throughput.

`emit-kernel-wrapper` emits a C99 source artifact from the kernel name, without
seeing runtime vectors. Seven cases exercise ReLU and affine transforms with
empty, singleton, and non-vector-multiple lengths. The oracle compiles each
artifact with a separately generated fixed driver and the recorded `--cc`
compiler (`-O2 -fno-fast-math -ffp-contract=off`). It checks exact f32 output
bits in separate-buffer and in-place modes, input preservation, guard writes,
and null-pointer handling at zero count. Repeated emission must be identical.
Compiler/build/run diagnostics remain available in public agent feedback.

`def-parametric-type` defines native `fx<width,frac>` types. Eleven cases cover
interior values, both width limits, fractional limits, and five invalid
parameter combinations. Joggle exercises a callable generic Ty constructor
and an explicit mod verifier; MLIR/xDSL register native dialect types with
parsing, printing, and parameter verification. A fixed observer reads the
type after text round-trip. Candidate-returned answer dictionaries are not
used. `storage_bits` records logical width, not ABI allocation size. Definition
tasks use the shared `definition-starter.*` entry points.

### Local agent execution

`run_extension_agent.py` runs an installed Ollama model with four actions:
`inspect`, `edit`, `test`, and `finish`. Each action and response is saved with
token counts, candidate hashes, tool feedback, and the final patch. The public
test action exposes the first positive and first negative fixture (when present).
Final scoring executes the complete task after the trajectory ends; its results
are not returned to the model.

```sh
python3 artifact/run_extension_agent.py \
  --model qwen3.5:9b --system Joggle --task ana-fusion-match \
  --seed 1701 --run 0 \
  --joggle build/joggle --builtin-mods build/modules \
  --output local/cache/artifact/agent-joggle-fusion-1701
```

Native candidates run under macOS Seatbelt: toolchain files are read-only,
writes are confined to the trial directory, network access is denied, and
credentials are removed from the child environment. MLIR additionally needs
`--sandbox-read /path/to/llvm-project` for its build-tree headers and libraries.
The agent has no shell action. `--allow-dirty` marks an integration run against
uncommitted harness changes; otherwise the collector requires a clean checkout.

Each request disables history truncation and generation-time context shifting
with Ollama's `truncate: false` and `shift: false`. Responses must report valid
token counts within the requested generation and context limits. A structured
native context-size rejection ends the trajectory at its budget boundary; it
does not execute an action or retry with shortened history. Records retain the
request hash, overflow token counts, context policy, and server version. Other
provider errors remain infrastructure failures. Before formal collection,
verify these controls on every pinned model/server pair; token counts alone
cannot establish that an older server honored the flags.
Public and final oracle reports must cover their assigned fixture populations.
Provider, oracle, and identity-check exceptions are retained in the trajectory
with `stop_reason: agent_error`; final-check failures still produce the CSV and
patch record, with unknown compiler phases left empty.

For unattended continuation of an existing primary manifest, use the batch
runner. It retains completed conditions, continues only unanswered transport
interruptions, and visits other conditions when one reaches its retry limit.
The manifest records progress and the shared transport amendment. Keep native
tools and non-paper sources unchanged during collection.

```sh
python3 artifact/run_extension_batch.py COLLECTION_DIRECTORY \
  --max-continuations 6 --max-hours 24 \
  --export paper/data/figure-04-extension.csv
```

The time limit is checked between trajectories, allowing an active trajectory
to finish within its original action/token budget. The complete matrix passes
the existing release gate before CSV export and figure generation; an incomplete
matrix does not replace paper data. This command does not rewrite manuscript
claims or submit the paper.

The collector emits one CSV and an adjacent provenance record per trajectory.
A clean run with stable source/tool/model identities, a complete final oracle,
and no infrastructure error is release-eligible whether the candidate passes
or fails. `--allow-dirty` runs are integration checks and remain ineligible.
The collector does not infer parsing or typing failures from diagnostic text;
unobserved phase fields and reference likelihoods remain empty. Assemble the
complete primary population with the existing release gate:

```sh
python3 artifact/merge_agent_rows.py \
  local/cache/artifact/agent-primary/*/result.csv \
  --output local/cache/artifact/figure-04-extension.csv
```

Replace `agent-primary` with the designated collection directory; do not merge
independent collections through a broad glob. Each provider CSV contains one condition.
Its `agent-provider/v1` record binds the CSV to the complete trajectory and
isolated final oracle. The primary matrix contains 72 rows: two pinned models,
three systems, and the twelve preselected tasks, with zero demonstrations,
seed 1701, and run index zero. The assembler rejects missing or duplicate
conditions, changed hashes, and infrastructure-invalid records. Semantic
failures remain in the completion denominator.

Render only the completed assembly, not individual provider files:

```sh
python3 artifact/figures/figure_04_extension.py \
  local/cache/artifact/figure-04-extension.csv \
  --output local/cache/artifact/figure-04-extension.pdf
```

The renderer rechecks all 72 conditions, native/model identities, provider
usage, and final-oracle hashes. Its six panels show family success percentages,
completion tokens, and tool calls for each model. Costs average successful
tasks only; an empty success set is a dash, while an observed zero is a hollow
circle at zero. Linear axes share limits within columns. The adjacent summary
CSV retains both task and success counts, since conditional cost populations
can differ across systems. The plot record also exports overall 95% task-bootstrap
intervals by enumerating all 4,096 two-task-within-family resamples with paired
indices across systems. These intervals describe task variation, not repeated
model runs. Incomplete inputs produce no figure.

## Full-collection release

This gate targets the original four-figure collection protocol, including a
complete Agent matrix. It is not the validator for the revised paper exports
listed at the top of this README: those use their corresponding renderers and
schemas. The stopped Agent collection does not satisfy this full-collection
gate. Use `make -C paper figures` to regenerate the reported data figures.

Individual validators and plots never execute Joggle. Once all four CSVs and
their provenance records are present:

```sh
python3 artifact/check_release.py \
  --data-dir local/cache/artifact/release-data \
  --output-dir local/cache/artifact/release
```

The command validates all pairings and correctness gates, verifies hashes,
renders PDF and PNG figures, and publishes one `evaluation-release/v2`
manifest. It never overwrites an existing release directory.
