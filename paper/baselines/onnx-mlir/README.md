# ONNX-MLIR system baseline

This directory defines the primary system-level baseline for RQ2. It is a
protocol record, not a completed result. No ONNX-MLIR measurements may enter
the manuscript until the implementations, patches, commands, and raw outputs
described below are preserved here.

## Frozen revision and documented path

- repository: <https://github.com/onnx/onnx-mlir>
- revision: `4a13c34aa695b228599d637cdb772c19b4b18dba`
- observed: 2026-09-13
- compiler image: `ghcr.io/onnxmlir/onnx-mlir`, amd64 manifest
  `sha256:989e82c5cef0a0fbaf128a2d0a54d828beb4e3e9e5e3228f845fbab4539b288a`
- development image: `ghcr.io/onnxmlir/onnx-mlir-dev`, amd64 manifest
  `sha256:7cac84ffca2989699bb8267043d700b4679c6c90b119ea6540a50155b1f6aa60`
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

Both pinned image manifests declare the revision above in their OCI labels.
The newer repository `main` observed on the same date is intentionally not the
baseline because no matching successful image was available. Source and
executable revisions must not be mixed. The retrieved manifest, config, size,
and label fields are preserved in [`images.json`](images.json); the documented
source integration boundary is summarized in [`surface.md`](surface.md).

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
