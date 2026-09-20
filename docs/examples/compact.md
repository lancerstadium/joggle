---
title: Compact Conv example
description: Select a fused biased-activation convolution under an explicit intermediate-memory policy.
---

# `compact`

## Problem

A canonical biased Conv path may materialize full-size intermediate results.
`compact` offers one compatible fused body and a policy based on visible extra
element count.

## Minimal selection

```sh
joggle run compact.apply semantic.jog \
  -M examples/mods -M build/modules > selected.jog
```

Only calls matching the biased-and-activated signature are candidates.

## Budgeted selection

```sh
joggle run compact.apply semantic.jog \
  --arg '{max_extra_elems: 262144}' \
  -M examples/mods -M build/modules > selected.jog
```

For each compatible call, the policy inspects inferred result shape and
candidate metadata. It returns at most one candidate; an empty result preserves
the shared `nn` implementation.

## Read the output

Compare `semantic.jog` and `selected.jog`. A selected call gains the external
mod dependency and exposes/retargets the chosen body transactionally. Unmatched
calls remain unchanged.

## Boundary

This example demonstrates a memory/latency tradeoff, not universal speedup.
`compact` changes neither frontend conversion nor C emission. Copy its policy
shape when an implementation choice has a measurable, explicit resource rule.
