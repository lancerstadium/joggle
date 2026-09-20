---
title: Emit C
description: Prepare semantic IR, plan bounded storage, emit strict C11, and run it.
---

# Emit C

C emission is an explicit artifact stage. It does not hide semantic conversion,
optimization, scheduling, or storage planning.

Start with checked semantic IR, such as `semantic.jog` from
[Import ONNX](import-onnx.md).

## Prepare and plan

```sh
./build/joggle run \
  c.prepare bounds.fold opt.fold opt.basic \
  tile.canon mem.plan \
  semantic.jog -M build/modules > prepared.jog
```

Each name is an ordinary function. `c.prepare` exposes operations required by
the C capability boundary; the optimization functions simplify the result;
`mem.plan` assigns bounded reusable storage.

Check the remaining capability frontier:

```sh
./build/joggle query c.frontier prepared.jog -M build/modules
```

An empty list means every remaining operation has a C representation. It does
not claim numerical correctness; the generated artifact must still be tested
against a reference oracle.

## Emit inspectable artifacts

```sh
./build/joggle emit c.header prepared.jog -M build/modules > model.h
./build/joggle emit c.source prepared.jog -M build/modules > model.c
```

Compile with strict warnings and a harness that calls the exported API:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic-errors \
  model.c harness.c -o model
./model
```

Programs that call scalar math functions may also require `-lm` on Linux.

## Make contracts explicit

`c.noalias` records a caller-provided disjoint-pointer contract. `c.restrict`
conservatively proves the corresponding property for eligible private
functions. Neither should be added merely to improve generated text.

Dynamic tensor shapes require a proved finite capacity for bounded storage.
The logical extents remain separate from capacity and are returned through the
generated ABI where required.

Run the maintained generated-C gates with:

```sh
ctest --test-dir build -L c --output-on-failure
```

For exact public functions, see the [`c` and `mem` catalogue entries](../reference/module-catalogue.md).
