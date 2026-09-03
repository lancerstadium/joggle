# NN compiler pipeline

This example keeps target vocabulary, cross-vocabulary transformation, and
compiler orchestration in separate Modules:

- `example_accel.joggle` declares only the module operation it owns;
- `nn_pipeline.joggle` declares ordinary typed compiler functions over Prelude
  `module`;
- `behavior.cpp` binds one legality-checked conversion, one analysis, and one
  canonical source emitter.

`prepare` is written in Joggle itself. Its `bool` is Known during compiler
invocation, so the selected branch executes without creating target control
flow. The same `if` syntax over a Residual `i1` inside a module Function would
instead produce typed CFG edges. Neither case requires a `graph`, `region`,
`pass`, or `lower` declaration.

The example conversion maps `nn.relu` calls to an independently installable
accelerator vocabulary and rejects the result if any source `nn.relu` remains.
The instruction-count analysis consumes the same `module` value without a
separate analysis framework. It is intentionally small: new imports,
analyses, transforms, simulators, and emitters use the same typed-function
mechanism rather than a framework-specific registration hierarchy.
