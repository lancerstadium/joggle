# Link an external kernel

[`module.jog`](module.jog) declares monomorphic tensor functions without
bodies. Those ordinary `Fn` signatures are the complete ABI contract. One has
a tensor result and the other has two scalar results, exercising the same
structural output-pointer rule used by local functions. The portable C module
emits qualified prototypes and calls them; no operator registration or
C-emitter edit is required. [`kernel.c`](kernel.c) provides the implementations.
The test harness contains no handwritten model prototype; the generated
`model.h` is force-included for both the implementation and its callers.

From the repository root:

```sh
cmake -S . -B build
cmake --build build
mkdir -p build/examples/edge
./build/joggle run c.prepare examples/edge/model.jog \
  -M examples -M build/modules > build/examples/edge/model.jog
./build/joggle emit c.source build/examples/edge/model.jog \
  -M examples -M build/modules > build/examples/edge/model.c
./build/joggle emit c.header build/examples/edge/model.jog \
  -M examples -M build/modules > build/examples/edge/model.h
cc -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror \
  -include build/examples/edge/model.h \
  build/examples/edge/model.c examples/edge/kernel.c examples/edge/main.c \
  -o build/examples/edge/model
build/examples/edge/model
```

External functions must be monomorphic and use scalar or fixed-shape tensor
types representable by the C ABI. Their generated symbol includes the module
name (`edge.matmul` becomes `jog_edge_matmul`) so dependencies do not collide
with local model functions. The emitter checks every resulting C name and
rejects an ambiguous normalization instead of silently linking the wrong
function.
