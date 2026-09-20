---
title: Transform a program
description: Read graph source, compose transforms transactionally, query results, and separate deterministic reports from timing.
---

# Transform a program

## Input

```jog
mod demo
use base

fn add_zero(x: i32) -> i32 {
  let y = x + 0
  return y
}
```

The addition is an ordinary call, `y` is its result, and return uses `y`.

## Check

```sh
./build/joggle check input.jog -M build/modules > checked.jog
```

Always begin with verified canonical input. This separates parse/type failures
from transform failures.

## Run one transform

```sh
./build/joggle run opt.fold_add_zero checked.jog \
  -M build/modules > folded.jog
```

Output:

```jog
mod demo
use base

fn add_zero(x: i32) -> i32 {
  return x
}
```

The use was redirected first; the now-dead call/result was removed.

## Compose functions

```sh
./build/joggle run opt.fold_add_zero opt.basic checked.jog \
  -M build/modules > optimized.jog
```

Both functions execute left-to-right inside one transaction. If either fails or
final verification fails, no partial graph is printed.

## Query the result

```sh
./build/joggle query opt.unresolved optimized.jog -M build/modules
./build/joggle query opt.untyped optimized.jog -M build/modules
```

Expected for this input:

```text
[]
[]
```

The first list concerns symbol resolution; the second concerns open result
types. Neither is a numerical test.

## Capture evidence

```sh
./build/joggle run opt.basic checked.jog \
  --report run.attr --timing timing.attr \
  -M build/modules > optimized.jog
```

Compare `run.attr` structurally. Treat `timing.attr` as measurement requiring
repetitions, warmup control, and distribution reporting.

## Diagnose

| Failure | Next inspection |
| --- | --- |
| unresolved function | `opt.unresolved` on input and `mod info` for provider |
| open type | `opt.untyped`, then inference stage |
| transaction failure | structured diagnostics; input remains unchanged |
| unexpected no-op | inspect exact callee/type and transform preconditions |
