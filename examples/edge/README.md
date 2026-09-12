# Select external kernels

[`model.jog`](model.jog) contains no target call or target dependency. It calls
the shared `tensor.matmul` function and a model-local `measure` declaration.
[`module.jog`](module.jog) provides compatible functions whose names identify
those source functions. `edge.apply` selects them by the ordinary overload
rules. The matrix implementation is a generic, inspectable adapter that passes
its inferred dimensions to one generic external declaration; the
multiple-result implementation is selected directly as a bodyless function.
The convolution adapters use the same mechanism. One external declaration and
one C implementation cover ordinary, grouped, and depthwise fixed-shape f32
convolutions. A second ordinary `nn.conv2d` overload composes that declaration
with the shared `nn.bias` and `nn.activate` functions, so models do not need a
shape-specific registration or a C-emitter branch.

The selected declarations are the complete ABI contracts. One has a tensor
result and the other has two scalar results, exercising the same structural
output-pointer rule used by local functions. The portable C module emits their
prototypes and calls; no operator registration, model rewrite, or C-emitter
case is required. [`kernel.c`](kernel.c) provides the implementations. The test
harness contains no handwritten model prototype; the generated `model.h` is
force-included for both the implementation and its callers.

From the repository root:

```sh
cmake -S . -B build
cmake --build build
mkdir -p build/examples/edge
./build/joggle run edge.apply c.prepare examples/edge/model.jog \
  -M examples -M build/modules > build/examples/edge/model.jog
./build/joggle emit c.source build/examples/edge/model.jog \
  -M examples -M build/modules > build/examples/edge/model.c
./build/joggle emit c.header build/examples/edge/model.jog \
  -M examples -M build/modules > build/examples/edge/model.h
cc -std=c11 -Wall -Wextra -Wstrict-prototypes -Werror \
  -include build/examples/edge/model.h \
  build/examples/edge/model.c examples/edge/kernel.c examples/edge/main.c \
  -o build/examples/edge/model
build/examples/edge/model
```

Every selected call must have scalar or fixed-shape tensor types representable
by the C ABI. An external declaration itself may remain generic: the C module
derives its concrete erased prototype from the calls, deduplicates equal
prototypes, and rejects two call sites that would give one symbol incompatible
ABIs. Thus one declaration covers all compatible matrix dimensions without a
shape-specific declaration per model. The module passes its ordinary function
list directly; no implementation tag or registry is required.
`[c: {name: "edge_matmul"}]` binds an existing C symbol. Core IR does not
interpret that target-owned attribute, and no C-emitter table names this
kernel. Omitting it restores the collision-checked qualified default.
Selection adds the implementation module dependency and retargets the call
transactionally, so failure leaves neither a partial call rewrite nor a stray
`use edge`.

This portable example deliberately implements NCHW input/output and OIHW
weights. Its `accepts` function receives both the semantic call and each
type-compatible candidate. It admits the full convolution adapter only when
all three layout lists are constant `[0, 1, 2, 3]`; another-layout probe in the
example remains on the shared semantic implementation. A target supporting
NHWC can provide another ordinary overload and predicate. Neither the core nor
the ONNX frontend contains a layout-specific selection branch.
