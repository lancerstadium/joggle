# ONNX-MLIR system baseline

This directory defines the primary system-level baseline for RQ2. The matched
implementation task has a preserved native pilot. The external-kernel task now
has a reproducible unsupported outcome at its first mandatory matrix case; the
policy and numeric-format contracts remain incomplete. These records do not
support a broad extensibility claim.

The policy input now has a documented-path probe under [`policy/`](policy/).
Default ONNX-MLIR fusion lowers the unchanged three-`Add` model to one affine
loop; `--disable-krnl-op-fusion` preserves three. This establishes the two
structural endpoints, but the global Boolean does not implement the contract's
caller-defined operation weights, maximum extent, or maximum call count. The
task therefore remains incomplete pending an accelerator-scoped policy
implementation; the existing flag is not counted as a pass.

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
digests are committed. Joggle executes this fixture through its ONNX, VM, and
generated-C paths. The preserved ONNX-MLIR implementation and result are under
[`implementation/`](implementation/).

| Contract | ONNX-MLIR path to evaluate | Required end-to-end evidence |
| --- | --- | --- |
| implementation | Accelerator-scoped pass and lowering for standard matrix operations | Imported ONNX, explicit i-k-j body in preserved IR, native artifact, numerical oracle |
| policy | Accelerator-scoped analysis and transformation pass | Frozen limits, unchanged model, preserved pre/post IR, legality and numerical oracles |
| external-kernel | Documented `--ops-for-call` path, then accelerator conversion if required | Generic shape handling, declarations, linked artifact, rejection diagnostic, unchanged C oracle |
| numeric-format | Registered dialect/type plus conversions within the ONNX-MLIR driver | Parametric type, nested conversion, collision checks, two target paths, positive and negative oracles |

If a requirement cannot be expressed through the documented operation or
accelerator paths, record the exact failing requirement and diagnostic. Do not
fall back to a standalone `mlir-opt` plugin and label it an ONNX-MLIR result.

## Native implementation pilot

The implementation task was run natively on arm64 macOS rather than through a
container. ONNX-MLIR was checked out at the revision above with recursive
submodules. Its documented LLVM revision
`1053047a4be7d1fece3adaf5e7597f838058c947` was built with MLIR and Clang,
Release mode, assertions, RTTI, and the host target. StableHLO was disabled
because the task does not use that input path.

The exact added tree is preserved in [`implementation/`](implementation/).
Reproduction copies its `src/` and `test/` subtrees into a clean ONNX-MLIR
checkout, then configures with `ONNX_MLIR_ACCELERATORS=IKJ` and the five empty
instrumentation/reporting macros required by `Accelerator.hpp`:

```sh
cmake -S . -B build-ikj -G Ninja \
  -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_ASSERTIONS=ON \
  -DMLIR_DIR=/path/to/llvm-project/build/lib/cmake/mlir \
  -DONNX_MLIR_ACCELERATORS=IKJ \
  -DONNX_MLIR_ENABLE_STABLEHLO=OFF \
  '-DCMAKE_CXX_FLAGS=-DINSTRUMENTSTAGE_ENUM_IKJ= -DINSTRUMENTSTAGE_CL_ENUM_IKJ= -DPROFILEIR_CL_ENUM_IKJ= -DOPTREPORT_ENUM_IKJ= -DOPTREPORT_CL_ENUM_IKJ='
cmake --build build-ikj --target onnx-mlir
```

The accelerator registers a higher-benefit rank-two float32 `MatMul`
conversion. It initializes the result and emits explicit `i-k-j` loops with
loads, multiply, add, and store; it does not call ONNX-MLIR's existing matrix
kernel. The accelerator must also delegate the complete host pipeline and
provide the runtime compatibility symbol expected by generated entry points.

The following commands preserve the lowered affine IR and compile the shared
library from the same ONNX input:

```sh
build-ikj/Release/bin/onnx-mlir --maccel=IKJ --O0 --EmitMLIR \
  -o results/implementation/ikj \
  /path/to/joggle/paper/fixtures/implementation/model.onnx
build-ikj/Release/bin/onnx-mlir --maccel=IKJ --O0 \
  -o results/implementation/ikj-native \
  /path/to/joggle/paper/fixtures/implementation/model.onnx
```

With the ONNX-MLIR source runtime and built Python runtime on `PYTHONPATH`, the
checked-in [`oracle.py`](oracle.py) reports `[58, 64, 139, 154]` with zero
maximum absolute error. [`implementation/ikj.mlir`](implementation/ikj.mlir)
preserves the emitted loop nest, while
[`implementation/result.json`](implementation/result.json) records revisions,
digests, configuration, extension surface, and the oracle. The dependency
build was resumed incrementally, so no clean-build duration is reported for
this pilot.

## External-kernel unsupported boundary

The frozen external-kernel contract first requires one generic adapter to bind
the supplied C matrix kernel at two static shapes. The first shape is identical
to the checked-in `2x3` by `3x2` MatMul fixture. At the pinned revision, the
driver documents `--ops-for-call=Conv MatMul` as its example for replacing
loop lowering with `krnl.call`. Running that exact option on the fixture exits
successfully, but the preserved output contains three `affine.for` operations
and no `krnl.call`.

Source inspection explains the result without modifying the baseline. Conv
registers a generic call pattern and receives the `opsForCall` option. MatMul
registers only its ordinary lowering pattern and does not receive the option.
Consequently the first mandatory case cannot reach the supplied external
kernel through the documented path. The exact command output, emitted MLIR,
digests, source observations, and boundary decision are preserved under
[`external-kernel/`](external-kernel/). This is an unsupported outcome, not a
failed build and not evidence that ONNX-MLIR cannot be extended through a new
accelerator or source change.

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
