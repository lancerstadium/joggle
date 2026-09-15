# Vertical evolution implementations

This directory contains stage implementations for the frozen contract in
[`../evolution-study.md`](../evolution-study.md). `common/` is supplied task
code shared by every system; system directories contain only framework-specific
extensions and reproduction records.

Stages are preserved as separate snapshots because the experiment measures
which interfaces must change from the preceding correct artifact. A later
stage may copy unchanged source for direct reproduction, but the paper counts
only the patch between adjacent stages and reports each independently governed
boundary rather than file or line totals.

Current status:

- deterministic eligible and fallback ONNX fixtures: frozen and verified;
- neutral cross-system import preflight: passed for Joggle, TVM/Relax, and
  ONNX-MLIR on the explicit `Conv -> Add -> Relu` graph;
- Joggle S0: functional preflight passes both frozen numerical oracles with
  zero maximum absolute error; its source and
  [`result.json`](joggle/s0/result.json) are preserved, but the stage is not
  cross-system-frozen and contains no publication timing;
- ONNX-MLIR S0: a pinned compiler-core patch for `krnl.call` integer-array attributes
  and an OMTensor ABI bridge now produce bitwise-oracle-checked `.so` artifacts
  for both shapes; [`result.json`](onnx-mlir/s0/result.json) records the initial
  ABI boundary, the separate host rebuild scope, and pre-patch LLVM-conversion failure;
- TVM S0: pinned LLVM-enabled Relax/TensorIR implementation exports `.so`
  artifacts for both shapes; a fresh process reloads both and passes the
  frozen oracle bitwise; [`result.json`](tvm/s0/result.json) preserves the
  before/after IR and initial extension surface;
- S1 and S2: intentionally not implemented until all S0 artifacts are frozen.

Generated IR, C, binaries, and timing logs belong under the ignored
`build-study/evolution/` tree, not in this source directory.
