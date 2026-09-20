---
title: Create an external mod
description: Author, load, run, inspect, and package a source-only graph transformation.
---

# Create an external mod

## Layout

```text
local-mods/
└── choose_lut/
    └── module.jog
```

## Source

```jog
mod choose_lut
use ir

fn apply(m: Mod) -> bool {
  var changed = false
  for op in ir.ops(m, ["call"]) {
    if ir.callee(op) == "nn.relu" {
      changed = ir.set(m, op, "implementation", "lut") || changed
    }
  }
  return changed
}
```

`ir.ops` traverses nested structure. The filter avoids testing non-call forms.
`ir.set` writes open metadata and returns whether it changed the operation.

## Input

```jog
mod demo
use nn

fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {
  let y = nn.relu(x)
  return y
}
```

## Validate and run

```sh
./build/joggle mod check choose_lut \
  -M build/modules -M local-mods
./build/joggle mod info choose_lut \
  -M build/modules -M local-mods
./build/joggle run choose_lut.apply demo.jog \
  -M build/modules -M local-mods > selected.jog
```

Output:

```jog
mod demo
use nn

fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {
  [implementation: "lut"]
  let y = nn.relu(x)
  return y
}
```

The call remains semantic `nn.relu`; the example records a decision. A real
implementation package may use `opt.apply`/`opt.instantiate` to select and
expose a compatible body.

## Grow without exposing helpers

```text
choose_lut/
├── module.jog
└── lib/
    ├── policy.jog
    └── rewrite.jog
```

Use `local fn` for helpers. `module.jog` is read first; fragments are sorted.

## Package lifecycle

```sh
joggle mod install local-mods/choose_lut installed-mods -M local-mods
joggle mod upgrade local-mods/choose_lut installed-mods -M local-mods
joggle mod uninstall choose_lut installed-mods
```

Install/upgrade validate dependencies and public compatibility before making a
staged copy visible.

## Next examples

Use [External mod examples](../examples/index.md) for analysis callbacks,
implementation selection, structural scheduling, and external kernels.
