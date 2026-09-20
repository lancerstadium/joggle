---
title: Cost policy example
description: Build a configurable graph measurement and reuse it as a transformation policy.
---

# `cost`

## Problem

The compiler cannot know whether “cost” means calls, cycles, joules, bytes, or a
device-specific score. `cost` keeps the unit in an external source mod.

## Package

```text
examples/mods/cost/
├── module.jog   # measurement callbacks and public queries
└── model.jog    # tiny input with one call and one return
```

Input:

```jog
mod model

fn main(x: i32) -> i32 {
  let y: i32 = opaque(x)
  return y
}
```

The callback assigns four to a call and one to other operations. `stat.sum`
owns traversal; the external mod owns weights.

## Run

```sh
./build/joggle query cost.total examples/mods/cost/model.jog \
  -M examples/mods -M build/modules
```

Output:

```text
5
```

Configured run:

```sh
./build/joggle query cost.total examples/mods/cost/model.jog \
  --arg 7 --arg 2 -M examples/mods -M build/modules
```

Output: `9`. The configuration is an invocation argument, not hidden metadata.

## Transformation policy

`cost.profitable(Mod, list<Op>, dict) -> bool` can be passed to `tile.fuse`.
It sees only legal candidates and decides whether visible extent/call-count
budgets admit them.

```sh
joggle run cost.fuse prepared.jog \
  --arg 65536 --arg 100 \
  -M examples/mods -M build/modules > selected.jog
```

## What to copy

Copy the callback structure and typed configuration boundary. Replace the toy
weights with calibrated data; do not present the example score as latency.
