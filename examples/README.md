# Examples

The examples are arranged by compiler boundary rather than by increasing toy
complexity. Each directory is a complete, runnable module or application.

- [`onnx`](onnx) is the real-network path. The official ONNX Zoo MNIST case
  imports, converts, exposes, executes in the VM, plans storage, emits C and a
  public header, compiles the C, and checks all outputs. MobileNetV2 runs the
  same driver at application scale. Both leave the transformed `model.jog`,
  self-contained `model.c`, executable external-weight `model-blob.c` plus
  `model-blob.h` and `model.bin`, VM image, analysis report, and numerical
  result in the build tree.
- [`ikj`](ikj) is the smallest complete user-defined implementation module. It
  replaces the shared generic matrix-multiplication body with a different loop
  order and then uses the unchanged C path. Its source is intentionally short
  enough to read in one screen.
- [`edge`](edge) demonstrates external tensor kernel selection. A generic
  adapter forwards inferred dimensions to one bodyless `fn`; the C emitter
  derives and checks the concrete call ABI without knowing the kernel name.
- [`spatial`](spatial) selects one portable convolution implementation family
  across compact, explicit-layout, and fused signatures. Its loop order
  exposes consecutive output columns to an optimizing C compiler.
- [`cost`](cost) demonstrates a user-defined structural measure. The unit and
  weighting policy stay in the module rather than becoming a compiler class.

Start with `ikj` to understand extension syntax, then run `onnx` to inspect the
same mechanisms on a conventional network. Small examples isolate an API
contract; performance or coverage claims come only from the official-model
gates described in [`onnx/README.md`](onnx/README.md).
