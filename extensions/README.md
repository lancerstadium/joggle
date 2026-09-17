# Out-of-tree extensions

Each directory here is a complete, runnable `.jog` module package that is loaded
from outside the bundled set: a directory holding `module.jog`, so `-M
extensions` adds all of them as a module root alongside `-M build/modules`.

They are arranged by compiler boundary rather than by increasing toy
complexity, and each doubles as a worked example of the boundary it exercises.
The extension tests load these same packages, which is why they live in a
directory named for what the test suite depends on rather than under
`examples/`.

- [`ikj`](ikj) is the smallest complete user-defined implementation module. It
  replaces the shared generic matrix-multiplication body with a different loop
  order and then uses the unchanged C path. Its source is intentionally short
  enough to read in one screen.
- [`edge`](edge) demonstrates the separate FFI case: binding an already-written
  external tensor kernel. It is not used to optimize an inspectable Conv body.
  A generic adapter forwards inferred dimensions to one bodyless `fn`; the C
  emitter derives and checks the concrete call ABI without knowing the kernel
  name.
- [`locality`](locality) is a source scheduling policy over exposed computation.
  It reorders a proven affine tensor reduction without defining or selecting a
  second convolution body.
- [`compact`](compact) selects a lower-workspace body for biased convolution.
  It deliberately records a memory/latency tradeoff instead of being presented
  as a universally faster replacement.
- [`cost`](cost) demonstrates a user-defined structural measure. The unit and
  weighting policy stay in the module rather than becoming a compiler class.

Start with `ikj` to understand extension syntax, then read
[`../examples/onnx`](../examples/onnx) to see the same mechanisms on a
conventional network. Small extensions isolate an API contract; performance or
coverage claims come only from the official-model gates described in
[`../examples/onnx/README.md`](../examples/onnx/README.md).
