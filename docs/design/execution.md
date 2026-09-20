---
title: Execution and updates
description: Transactions, dependency tracking, reusable plans, and verification fallbacks.
---

# Execution and updates

`query`, `run`, and `emit` use the same resolver and evaluator. They differ in
what they are allowed to publish.

## Transaction boundary

A `run` invocation may call several functions. Edits remain staged until the
whole sequence succeeds and the resulting graph verifies. Failure preserves
the original graph and produces no successful output or report.

Queries do not publish structural edits. Emitters produce artifacts from the
checked state they receive; they do not silently run conversion or planning.

## Recorded dependencies

An invocation records the functions, collections, operations, values,
packages, intrinsics, and arguments it observes. Revisions invalidate cached
work whose observations changed. Unaffected execution plans and memoized
results may be reused.

Plans and results are separate: a decoded function plan can remain valid when
an argument-dependent result must be recomputed.

## Conservative fallback

Local value and metadata edits can follow precise dependency edges. Package,
symbol, or unsupported structural edits invalidate more broadly and use full
verification. Correctness does not depend on an incremental fast path.

## Evidence and timing

```sh
joggle run opt.basic model.jog \
  --report run.attr --timing run-timing.attr \
  -M build/modules > optimized.jog
```

The deterministic report records calls and cache hits. Timing is stored
separately because wall-clock values are not stable structural output.

The current executor is not a native-code JIT or incremental object linker.
See [Transform a model](../tutorials/transform.md) for the user workflow.
