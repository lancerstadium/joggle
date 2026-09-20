---
title: Control flow
description: Write branches, loops, returns, carried state, and short-circuit expressions.
---

# Control flow

## Branches

```jog
fn absolute(x: i32) -> i32 {
  if x < 0 {
    return -x
  }
  return x
}
```

An `if` owns structured child blocks. Early returns remain explicit and every
path is verified against the function result types.

## Carried state

```jog
fn choose(x: i32, flag: bool) -> i32 {
  var result = x
  if flag {
    result += 1
  }
  return result
}
```

The readable source mutates `result`. Internally the branch receives and
returns carried values; graph transforms can inspect those edges directly.

## Loops

```jog
fn sum(values: list<int>) -> int {
  var total = 0
  for value in values {
    total += value
  }
  return total
}
```

Ranges and lists are iterable. Multiple variables form a nested Cartesian
product:

```jog
for i in 0..M, j in 0..N {
  out[i, j] = left[i, j] + right[i, j]
}
```

This syntax exposes loop order to structural transforms; it is not opaque
kernel text.

## Short circuit

```jog
fn valid(a: i64, b: i64) -> bool {
  return a > i64(0) && (b > i64(0) || a == b)
}
```

Most operators normalize to calls. `&&` and `||` normalize to branches so the
right operand is evaluated only when selected.

Continue with [Metadata](metadata.md).
