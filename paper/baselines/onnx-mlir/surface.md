# Documented extension surface at the pinned revision

This note fixes the path to evaluate before task code is authored. It is source
inspection, not a completed comparison result.

At commit `4a13c34aa695b228599d637cdb772c19b4b18dba`, the documented accelerator
path has the following observable boundary:

- an accelerator is an in-tree directory under `src/Accelerators`;
- `ONNX_MLIR_ACCELERATORS` causes CMake to add that directory, link its
  accelerator library, and generate the global `Accelerators.inc` list;
- the accelerator subclasses `onnx_mlir::accel::Accelerator` and implements ten
  pure virtual hooks covering versioning, driver passes, dialect/pass
  registration, configuration, tensor conversion, ONNX-to-Krnl conversion, and
  Krnl-to-LLVM conversion;
- the driver selects built accelerators through `--maccel`;
- the documented conversion hooks are located at ONNX-to-Krnl and
  Krnl-to-LLVM; accelerator-owned dialects and passes may run between them.

The implementation contract already uses a standard ONNX `MatMul`, so its
baseline must not edit ONNX operation generation merely to inflate the change
surface. Operation-generation files enter a task measurement only when the
task genuinely adds or customizes an ONNX schema operation. The external-
kernel and numeric-format tasks may require more of the accelerator boundary,
but that must be established by their implementations rather than assumed from
this audit.

Authoritative source locations:

- `src/Accelerators/Accelerator.hpp`
- `src/Accelerators/CMakeLists.txt`
- `src/Accelerators/CreateAcceleratorsInc.cmake`
- `docs/AddCustomAccelerators.md`
- `docs/ImportONNXDefs.md`
