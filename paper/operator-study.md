# Operator and shape study

## Purpose

This study measures generated-code quality. It does not measure parser coverage,
extension footprint, or the speed of one Joggle pass against another. The main
paper artifact is a native LaTeX speedup table in the visual grammar of the
provided Axon example. Every shaded cell is the same quantity:

```text
speedup = median latency of the named baseline / median Joggle latency
```

Thus `1.0` is parity, a value above `1.0` favors Joggle, and a value below `1.0`
is an observed Joggle slowdown. The table must not invert this convention for a
negative result.

## Matrices

Different computation arities use separate matrices. Forcing pointwise,
contraction, and convolution workloads onto one ambiguous shape axis would make
the table dense but uninterpretable.

### A. Two-dimensional and last-axis computations

Rows: Add, Multiply, ReLU, SiLU, RMSNorm, LayerNorm, Softmax, ReduceSum, and
Cumsum when supported by all compared paths.

Columns are the complete Cartesian product `M × N`, with
`M ∈ {1, 4, 16, 64, 256}` and `N ∈ {64, 128, 256, 512, 1024}`: 25 measured
shapes plus one geometric-mean column. These values cover vector-like batch-1
edge inference, small batches, token blocks, and channel/hidden widths without
copying Axon's accelerator-scale 1K–16K grid.

### B. Contractions and transformer subgraphs

Rows: MatMul, Transpose+MatMul, RMSNorm+MatMul, Softmax+MatMul, QKV projection,
and gated-MLP subgraphs only after each has one shared semantic fixture.

Columns are the complete Cartesian product `M × K × N`, with
`M ∈ {1, 16, 128}` and `K,N ∈ {128, 256, 512}`: 27 measured shapes plus one
geometric-mean column. `M=1` is decode-like, `M=16` is a short token block, and
`M=128` is prefill-like; `K,N` exercise edge-sized hidden/projection widths.

### C. Convolution families

Convolution uses an explicit tuple header
`H×W / Cin×Cout / K / stride / groups`; it is not abbreviated as `M×N`.
The initial grid contains 24 cases:

- `H=W ∈ {7, 14, 28, 56}`;
- `(Cin,Cout) ∈ {(16,16), (32,32), (32,64)}`;
- standard 3×3 convolution and depthwise 3×3 convolution;
- stride one, NCHW, batch one.

Pointwise 1×1 convolution receives a separate 20-case grid over
`H=W ∈ {7,14,28,56}` and
`(Cin,Cout) ∈ {(16,16),(16,32),(32,32),(32,64),(64,64)}`. These cases expose
the weight-packing, reduction-order, and channel-blocking problems hidden by a
single MobileNet total.

## Baseline sections

The primary table has independent row sections rather than mixing denominators:

1. **vs one-thread ONNX Runtime** on an exactly identical generated ONNX
   fixture and input;
2. **vs TVM** after a frozen tuning budget and an untuned/default row are both
   preserved;
3. **vs ONNX-MLIR** for the identical ONNX fixture and native target;
4. **vs LiteRT** only when the source is an identical TFLite FlatBuffer, never
   a separately converted model.

A system is omitted from a row only when its preserved build or compilation
record identifies an unsupported case. The cell then contains a dash; a failed
or timed-out run is not silently converted into unsupported.

## Measurement contract

- Run on one identified, otherwise-idle Linux machine with fixed affinity,
  one software thread, recorded governor/frequency state, compiler versions,
  and host-load rejection.
- Generate all fixtures deterministically and hash model, input, expected
  output, source, weights, binary, and command manifest.
- Use each system's documented native interface. Time repeated invocations
  after construction and warm-up; exclude model loading and compilation from
  inference latency, but report them separately.
- Use balanced system order, at least 20 outer trials, sufficient inner
  iterations for sub-millisecond cases, median and dispersion, and a checksum
  that depends on every output.
- Validate every candidate against the same high-precision fixture before its
  timing enters the table. Record maximum absolute and relative error.
- Freeze tuning budgets. Plot best-so-far against measured candidates before
  comparing a tuned Joggle result with an untuned baseline.

## LaTeX contract

The table is emitted directly as `table*`/`tabular`, with rotated shape headers,
`booktabs` group rules, compact numeric cells, and one baseline label spanning
each row section. Cell color is a monotonic function of speedup and never of
support or correctness. A restrained four-bin legend is fixed before results
are inspected: `<0.5×`, `0.5–0.9×`, `0.9–1.1×`, and `>1.1×`. Exact values remain
printed in every cell. The geometric mean excludes unsupported cells and its
sample count appears in the caption.

No table is rendered from placeholder or synthetic values. Until the matched
records exist, this document is the evidence contract rather than a mock result.

## Executable path

The contraction matrix is now executable rather than only specified. Generate
all 27 deterministic MatMul cases into the ignored build tree with:

```sh
.venv-fixtures/bin/python paper/operator_suite.py
```

Each case has one runtime input, one constant ONNX initializer, one reference
output, and hashes in `build/operator-study/fixtures/manifest.json`. This avoids
coupling the study to Joggle's current single-input ONNX application while
preserving an ordinary ONNX model that every baseline can consume. A case is
prepared with independent weights and a balanced Joggle/ONNX Runtime manifest:

```sh
python3 paper/prepare_operator_case.py \
  --fixture build/operator-study/fixtures/matmul-m1-k128-n128 \
  --output build/operator-study/prepared/matmul-m1-k128-n128 \
  --app build/joggle-onnx-app --tool build/joggle \
  --modules build/modules --cc /usr/bin/cc \
  --ort-python /path/to/python-with-onnxruntime \
  --module-root examples --pass spatial.apply
```

`paper/measure_systems.py` then consumes the emitted `systems.json`; it balances
execution order, checks matching output hashes, and records artifacts, versions,
host state, and latency without changing the speedup definition above.

`--pass` is repeatable and names an ordinary module function. The runner applies
the requested policy to the canonical body, then runs the same cleanup, memory,
alias, placement, and emission sequence used by the baseline path. This keeps
policy choice outside the ONNX frontend and C emitter.

The first local smoke shapes exposed the optimization target, but are not
publication measurements. The independent weight artifact first removed a
per-inference initializer copy. The shared tensor implementation now represents
contraction as one explicit state-and-reduction loop, allowing the existing
operator-independent access policy to see the real body. Dense multidimensional
addresses are linearized by the same affine-form utility used for all tensor
accesses; no operator name participates. On the development host, the policy
selects `i,k,j` from `i,j,k` for the 128-cubed case, preserves the exact output,
and reduces a paired diagnostic from roughly 1.1 ms to 0.083 ms. The remaining
gap to the adjacent one-thread ONNX Runtime observation is roughly one order of
magnitude. These figures justify the mechanism and the next optimization step;
they must not enter the paper's result table until repeated on the controlled
Linux host from a clean revision.

## Optimization gate before publication measurement

The current portable C path is not yet a competitive performance candidate.
Publication measurement begins only after generic mechanisms, not operator-name
cases, cover the following sequence. Items marked complete describe mechanism
coverage, not a performance claim:

1. **complete:** canonical affine loop/access form and contiguous-axis analysis;
2. **partial:** legal interchange over explicit loop bodies; multi-level tiling
   still needs a profitable policy and matched measurements;
3. constant-weight packing with artifact provenance;
4. **partial:** no-alias facts are emitted; alignment and vector-width facts
   remain target-owned work;
5. **partial:** the contiguous inner loop is compiler-vectorized on the
   development toolchain, but the contract and Linux evidence are not frozen;
6. fusion profitability based on residency/traffic, not loop-count reduction;
7. bounded candidate generation, target measurement, selection, and cache.

This gate explains the present ONNX Runtime gap rather than hiding it. The first
general transformation has removed the largest scalar-loop loss in one shape;
packing, cache blocking, vector contracts, fusion profitability, and bounded
selection remain the mechanisms that determine whether the full matrix becomes
competitive.
