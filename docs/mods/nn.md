# Neural-network functions

`nn@2` is the shared semantic library for neural-network programs. Its members
are ordinary Joggle fns; the compiler core has no list of NN operation names.

`matmul`, `relu`, and `dropout` currently have source bodies. MatMul uses
pure tensor construction, indexed `reduce`, multi-index `[]`, and scalar
overloads. Relu uses rank-polymorphic `map`. Calls remain compact until a user
explicitly expands their bodies.

Conv, pooling, concatenation, reshape, flatten, and Softmax currently have
typed declarations and compile-time shape semantics so real static models can
be represented without an ONNX operation layer. They remain implementation
leaves. A compiler or device module must either replace them or reject them;
successful import does not imply executable support.

The shape helpers are ordinary compiler fns in the same module. They are used
by result type expressions and are not an additional IR or public pass system.
