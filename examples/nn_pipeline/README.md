# NN compiler pipeline

This example keeps target vocabulary, cross-vocabulary transformation, and
compiler orchestration in separate Module packages:

- `modules/example_accel/module.joggle` declares only the operation the example
  accelerator owns;
- `modules/nn_pipeline/module.joggle` declares ordinary typed compiler
  functions over Prelude `module`;
- `modules/nn_pipeline/src/behavior.cpp` binds one graph fusion, one analysis,
  and one canonical source emitter;
- `modules/nn_pipeline/tests/pipeline_test.cpp` verifies the complete example.

The two packages use the same layout and naming rules as shipped Modules. The
example root only delegates its build to the package that owns native code.

`prepare` is written in Joggle itself. Its `bool` is Known during compiler
invocation, so the selected branch executes without creating target control
flow. The same `if` syntax over a Residual `i1` inside a module Function would
instead produce typed CFG edges. Neither case requires a `graph`, `region`,
`pass`, or `lower` declaration.

The example matches a single-user `nn.batch_norm_nchw -> nn.relu` def-use pair
and replaces it with `example_accel.batch_norm_relu_nchw`. The pattern follows
SSA producer/users and carries the named `epsilon` property into the fused Op.
The op-count analysis consumes the same `module` value and observes the reduced
graph without a separate analysis framework. New imports, analyses, transforms,
simulators, and emitters use the same typed-function mechanism.
