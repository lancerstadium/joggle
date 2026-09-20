---
title: External kernel example
description: Bind shared semantic calls to existing C functions through typed declarations.
---

# `edge`

## Problem

Use `edge` when a computation already exists in an external library or device
runtime. It is an ABI boundary, not the path for structurally optimizing an
exposed function body.

## Package

```text
examples/mods/edge/
├── module.jog  # typed candidates and selection
├── model.jog   # model with shared semantic calls
├── kernel.c    # external implementations
└── main.c      # caller/oracle
```

The source mod declares compatible body-less functions. Metadata such as
`[c: {name: "edge_matmul"}]` binds the selected declaration to a C symbol.

## End-to-end

```sh
mkdir -p build/examples/edge
./build/joggle run edge.apply c.prepare examples/mods/edge/model.jog \
  -M examples/mods -M build/modules > build/examples/edge/model.jog
./build/joggle emit c.header build/examples/edge/model.jog \
  -M examples/mods -M build/modules > build/examples/edge/model.h
./build/joggle emit c.source build/examples/edge/model.jog \
  -M examples/mods -M build/modules > build/examples/edge/model.c
cc -std=c11 -Wall -Wextra -Werror \
  -include build/examples/edge/model.h build/examples/edge/model.c \
  examples/mods/edge/kernel.c examples/mods/edge/main.c \
  -o build/examples/edge/model
build/examples/edge/model
```

The generated header is force-included for implementation and caller so all
three compile against one derived ABI.

## Mechanism

Typed overload matching selects candidates. The C mod derives erased concrete
prototypes from call sites, deduplicates equal signatures, and rejects one
external symbol used with incompatible ABIs.

## Boundary

The sample convolution kernel assumes NCHW/OIHW. Another layout needs another
predicate/overload. Neither the core nor frontend gains a layout special case.
