# ONNX-MLIR system baseline

This directory defines the primary system-level baseline for RQ2. It is a
protocol record, not a completed result. No ONNX-MLIR measurements may enter
the manuscript until the implementations, patches, commands, and raw outputs
described below are preserved here.

## Frozen revision and documented path

- repository: <https://github.com/onnx/onnx-mlir>
- revision: `42803380540dc3c8fce2e831cc77d3c87a195a70`
- observed: 2026-09-13
- system boundary: ONNX input through the `onnx-mlir` driver to a compiled
  artifact and executable oracle

The official project describes ONNX-MLIR as an ONNX-to-native compiler rather
than merely an MLIR library. Its
[operation guide](https://github.com/onnx/onnx-mlir/blob/main/docs/ImportONNXDefs.md)
documents generated ONNX operation definitions, import tables, custom
verification, and importer hooks. Its
[accelerator guide](https://onnx.ai/onnx-mlir/AddCustomAccelerators.html)
documents accelerator-scoped dialect and pass registration, build selection
through `ONNX_MLIR_ACCELERATORS`, and compilation through `--maccel`.

## Task mapping

Each implementation must consume a checked-in ONNX fixture derived from the
same frozen task input, preserve the task's forbidden shortcuts, and execute
the same numerical or diagnostic oracle.

The first such input is
[`../../fixtures/implementation`](../../fixtures/implementation), whose model,
TensorProto data, generator, dependency versions, contract digest, and file
digests are committed. Joggle already executes this fixture through its ONNX,
VM, and generated-C paths. No ONNX-MLIR result is claimed yet.

| Contract | ONNX-MLIR path to evaluate | Required end-to-end evidence |
| --- | --- | --- |
| implementation | Accelerator-scoped pass and lowering for standard matrix operations | Imported ONNX, explicit i-k-j body in preserved IR, native artifact, numerical oracle |
| policy | Accelerator-scoped analysis and transformation pass | Frozen limits, unchanged model, preserved pre/post IR, legality and numerical oracles |
| external-kernel | Accelerator conversion to the supplied C kernels | Generic shape handling, declarations, linked artifact, rejection diagnostic, unchanged C oracle |
| numeric-format | Registered dialect/type plus conversions within the ONNX-MLIR driver | Parametric type, nested conversion, collision checks, two target paths, positive and negative oracles |

If a requirement cannot be expressed through the documented operation or
accelerator paths, record the exact failing requirement and diagnostic. Do not
fall back to a standalone `mlir-opt` plugin and label it an ONNX-MLIR result.

## Measurement boundary

Record authored and generated files separately. Count framework-core,
build-system, registration, conversion, and driver changes even when the
documented workflow requires them. Preserve:

- the exact ONNX-MLIR and LLVM revisions;
- the complete patch against the pinned checkout;
- clean configure and build commands and logs;
- input ONNX files and pre/post-pass IR;
- artifact link and execution commands;
- raw outputs for every positive and negative oracle.

Bare MLIR experiments may be kept as diagnostic notes for the dialect/type
portion of this work. They are not a system baseline because they omit ONNX
ingestion, ONNX-MLIR's pass pipeline, and artifact generation.
