---
title: Transform a model
description: Compose checked Joggle transformations and inspect deterministic execution reports.
---

# Transform a model

Complete [Get started](index.md) first. This guide explains the execution
contract behind the `run` and `query` commands; it does not introduce a second
pipeline abstraction.

## Compose ordinary functions

Pass several function names to one `run` invocation:

```sh
./build/joggle run \
  opt.fold_add_zero opt.basic \
  test/data/matmul.jog -M build/modules > optimized.jog
```

The functions execute from left to right in one transaction. If any function
fails or the final program does not verify, Joggle publishes none of the edits.

Use `-` as the input when shell composition is clearer:

```sh
./build/joggle run opt.fold_add_zero test/data/matmul.jog -M build/modules |
  ./build/joggle query opt.untyped - -M build/modules
```

Named intermediate files are preferable when a stage boundary must remain
inspectable or reproducible.

## Separate structure from execution evidence

```sh
./build/joggle run opt.fold_add_zero test/data/matmul.jog \
  --report run.attr --timing run-timing.attr \
  -M build/modules > transformed.jog
```

The transformed IR stays on standard output. `--report` records deterministic
function-call and cache-hit counts. `--timing` records phase timings separately
so timing noise cannot change the structural report.

## Use analysis without mutation

```sh
./build/joggle query opt.unresolved transformed.jog -M build/modules
```

A query observes the same `Mod/Fn/Blk/Op/Val` graph and resolver as a
transformation, but returns an `Attr` value and does not publish edits.

## What is tested

- `workflow` checks transactional invocation, reports, ordered execution, and
  dependency-indexed reuse and invalidation.
- `cli-run` and `cli-report` check the corresponding command-line contracts.

Run the nearby gates with:

```sh
ctest --test-dir build -L cli --output-on-failure
ctest --test-dir build -L unit --output-on-failure
```

Next: [Write a mod](write-mod.md).
