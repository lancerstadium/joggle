# Loop-locality study: a source-defined policy across ten models

September 17. Two questions: does a source-defined compiler procedure deliver
real code quality, and does it do so on more than one model?

## Why the earlier fixtures could not answer this

The first same-host comparison used
`build-study/derivation/ultraface/original/model.dylib`, the storage-study
artifact, which is ordinary `c.prepare` output with no loop policy applied.
Reusing the other fixtures from that earlier campaign would not have fixed the
problem, because they are not comparable to each other. That campaign's tree has
since been reclaimed; the findings below are why its fixtures were not used:

- `squeezenet1.1-7/prepared.jog` and `resnet18-v1-7/prepared.jog` come from an
  older pipeline whose convolutions are split into separate output and
  reduction nests. `locality.apply` does not fire on them at all.
- `squeezenet1.0-13-qdq/prepared.jog` depends on a `spatial` module that is now
  an empty directory.
- `tinyyolov2-8/prepared.jog` is zero bytes.
- `mobilenet-block/canonical.jog` already has the output-width axis innermost,
  so using it as a baseline would understate the policy.

Measuring across those fixtures would have compared fixture ages, not policies.
This study therefore rebuilds every model from its pinned ONNX file through one
chain at one compiler revision.

## Pipeline

```sh
# one Release build that also carries the ONNX native module
cmake -S . -B build -DJOGGLE_BUILD_ONNX=ON -DJOGGLE_TEST_ONNX_ZOO=.cache/onnx-zoo
cmake --build build -j8

# onnx.read -> onnx.nn.convert opt.basic -> [opt.instantiate] -> c.prepare
python3 paper/experiments/locality/prepare_model.py \
  .cache/onnx-zoo/<model>.onnx build-study/locality-matrix/<model> [--batch 1]

# locality.apply, then the identical plan/place/emit/compile chain per variant
python3 paper/experiments/locality/build_variants.py \
  build-study/locality-matrix/<model>/prepared.jog build-study/locality-matrix/<model>

# deterministic input and its ONNX Runtime reference
python3 paper/experiments/locality/gen_reference.py \
  .cache/onnx-zoo/<model>.onnx build-study/locality-matrix/<model>

# pinned ONNX-MLIR at -O3, then a sidecar naming the library and its revisions
python3 paper/experiments/locality/build_onnxmlir.py

# balanced alternating fresh processes, one thread, per-process validation
python3 paper/experiments/locality/matrix_driver.py \
  build-study/locality-matrix/<model> ... --trials 20
```

The driver measures the external columns of every model that carries the
matching sidecar: `tvm.json`, `onnxmlir.json`, and the pinned ONNX file that
gives ONNX Runtime its input. A subject that fails is recorded with its reason
in the run metadata and produces no timing for that model; the remaining models
still run. An earlier version aborted the whole job on the first failure, which
is why the ten-model matrix previously had a TVM column for one model only: the
`-O3` ONNX-MLIR library is built per model, and a build that is missing is
indistinguishable from a column that was never wanted unless the sidecar is
checked.

The ONNX-MLIR column has two recorded gaps. `squeezenet1.0-13-qdq` aborts the
compiler itself at the pinned revision with

```
Assertion failed: (isScalarValue(operands[i]) && "unary expected scalar
additional values"), function operator(), file Elementwise.cpp, line 2176.
```

so that model has no ONNX-MLIR bar. `tvm` on the same model fails the stored
reference instead, at a maximum absolute error of `2.065900e-02` with 30 of 1000
values outside the study's `1e-4` tolerance, so no timing is reported for a
variant that does not validate. Both are properties of the pinned external
tools, not of Joggle, and both are stated rather than smoothed over.

`--batch 1` is required by ResNet18 and TinyYOLOv2, whose ONNX batch axis is a
free variable; without it `c.prepare` stops at `opt.specialize requires finite
static ranges`. The step binds the entry's generic through `opt.instantiate`.

The timing subject takes the artifact's ABI from its own `c.api` descriptor, so
no model-specific signature is written anywhere in this study. That is also a
direct exercise of the claim that the C path derives its interface from one
signature model.

## What the policy is

`locality.choose` is an ordinary source-defined function in `extensions/locality`.
It scores candidate axis orders and reorders through `tile.reorder`. It is
applied, not derived: this study shows that code quality is reachable from
source without rebuilding the compiler, and it is not a third derivation case.

On UltraFace it moves the output-width axis innermost in 18 convolution nests,
changing `n,m,oh,ow,q,r,s` into `n,m,oh,q,r,s,ow`, which makes the innermost
access contiguous instead of strided by a channel plane.

## Boundaries

- Ten models, one host, one policy, one input per model.
- Inputs are drawn from a fixed seed, so results establish agreement with ONNX
  Runtime on one recorded input, not task accuracy.
- ONNX Runtime is a tuned runtime with hand-written kernels; it bounds the
  remaining gap and is not a like-for-like comparison with compiler output.
- Packing, vectorized microkernels, epilogue fusion, and threading are absent
  from both Joggle variants.
- Results per model are never pooled into a single aggregate figure.
