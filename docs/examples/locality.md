---
title: Locality policy example
description: Consume affine loop evidence and apply generic reordering/blocking without naming an operator.
---

# `locality`

## Problem

Once a semantic body is exposed as loops, a target/project may prefer a
different legal order. `locality` contains profitability policy while `tile`
retains correctness.

## Inspect first

```sh
joggle query locality.plan canonical.jog \
  -M examples/mods -M build/modules > plan.attr
```

The report contains function identity, current/selected axes, extents, affine
read/write forms, locality scores, and scalar-promotion cost.

## Apply

```sh
joggle run locality.apply canonical.jog \
  -M examples/mods -M build/modules > scheduled.jog
```

The policy uses `tile.state_axes`, `tile.reduction_axes`, and affine forms. It
does not look for `nn.conv2d` or assume seven axes.

## Block

```sh
joggle run locality.block canonical.jog \
  --arg '[4, 7]' --arg 4000 \
  -M examples/mods -M build/modules > blocked.jog
```

Factors are attempted in order. The final argument bounds a structural source-
duplication proxy. The policy may compose `split`, `peel`, `reorder`, and
`scalarize`; each edit returns its live replacement for the next step.

## Boundary

Affine legality, state injectivity, capacity, and reduction-order preservation
belong to `tile`. Scores and budgets belong to `locality`. A backend-specific
model can replace only the policy.
