---
title: IKJ MatMul example
description: Select an alternative matrix-multiplication body through ordinary overload matching.
---

# `ikj`

## Problem

The shared `tensor.matmul` body is semantically correct, but another loop order
may be preferable. `ikj` supplies an alternative `i-k-j` body without adding a
schedule object or target registry.

## Package

```text
examples/mods/ikj/
├── module.jog  # alternative function and apply policy
├── model.jog   # typed MatMul input
└── main.c      # standalone numerical caller
```

`module.jog` tags body-bearing functions as candidates, asks normal overload
matching for compatibility, and exposes the selected body.

## Run and inspect

```sh
mkdir -p build/examples/ikj
./build/joggle run ikj.apply c.prepare examples/mods/ikj/model.jog \
  -M examples/mods -M build/modules > build/examples/ikj/model.jog
```

Open the result. The semantic MatMul call is replaced by visible `i-k-j` loops;
there is no hidden backend schedule.

```sh
./build/joggle emit c.header build/examples/ikj/model.jog \
  -M examples/mods -M build/modules > build/examples/ikj/model.h
./build/joggle emit c.source build/examples/ikj/model.jog \
  -M examples/mods -M build/modules > build/examples/ikj/model.c
cc -std=c11 -Wall -Wextra -Werror \
  -include build/examples/ikj/model.h build/examples/ikj/model.c \
  examples/mods/ikj/main.c -o build/examples/ikj/model
build/examples/ikj/model
```

## Ownership

`tensor` owns MatMul meaning; `ikj` owns one alternative body and selection;
`opt` owns typed matching/expansion; `c` owns the artifact.
