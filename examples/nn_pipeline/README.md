# NN compiler pipeline

This example keeps target vocabulary, cross-vocabulary transformation, and
compiler orchestration in separate Modules:

- `example_accel.joggle` declares only the program operation it owns;
- `nn_pipeline.joggle` declares ordinary functions over `ir.module`;
- `behavior.cpp` binds the structural rewrite and canonical source emitter.

`prepare` is written in Joggle itself. Its `bool` is Known during compiler
invocation, so the selected branch executes without creating target control
flow. The same `if` syntax over a Residual `i1` inside a program Function would
instead produce typed CFG edges. Neither case requires a `graph`, `region`,
`pass`, or `lower` declaration.

The example rewrite maps `nn.relu` calls to an independently installable
accelerator vocabulary. It is intentionally small: new analyses, transforms,
simulators, and emitters use the same typed-function mechanism rather than a
framework-specific registration hierarchy.
