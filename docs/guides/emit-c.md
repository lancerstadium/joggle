---
title: Emit C
description: Prepare semantics, plan storage, prove the target frontier, emit strict C, and execute a harness.
---

# Emit C

## Input contract

```jog
mod kernel
use math

fn twice(x: f32) -> f32 { return x + x }

[c: {name: "affine_kernel"}]
fn affine(x: f32) -> f32 {
  let doubled = twice(x)
  return doubled + f32(1)
}
```

The open `c.name` attribute is interpreted by the C mod only.

## Prepare

```sh
./build/joggle run c.prepare input.jog \
  -M build/modules > prepared.jog
```

Preparation exposes semantic bodies the target cannot represent directly and
performs target-owned cleanup. Read `prepared.jog` before planning.

## Plan storage

```sh
./build/joggle run mem.plan prepared.jog \
  -M build/modules > planned.jog
```

Tensor intermediates may gain `mem.slot` and functions gain `mem.counts` /
`mem.types`. Logical program structure remains readable.

## Prove readiness

```sh
./build/joggle query c.frontier planned.jog -M build/modules
```

Expected output:

```text
[]
```

Any listed callee needs conversion, implementation selection, body exposure, or
explicit target support before emission.

## Emit

```sh
./build/joggle emit c.header planned.jog -M build/modules > model.h
./build/joggle emit c.source planned.jog -M build/modules > model.c
```

The header contains:

```c
float affine_kernel(float x);
```

## Compile and execute

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic-errors \
  -include model.h model.c harness.c -lm -o model
./model
```

Force-including the generated header makes source and harness use the same ABI.
A zero process status is meaningful only when the harness checks expected
values.

## Diagnose

| Failure | Meaning |
| --- | --- |
| non-empty frontier | unsupported call remains |
| unknown capacity | dynamic result lacks bounded storage proof |
| symbol collision | two definitions erase to the same C name |
| incompatible external ABI | one external name has conflicting call types |
| compiler warning/error | artifact contract is not strict-C clean |
