---
title: bounds mod
description: Infer integer ranges, query fit/capacity evidence, and fold expressions proven constant.
---

# `bounds`

`bounds` computes conservative integer facts used by dynamic-shape capacity,
guard elimination, and policy decisions.

```mermaid
flowchart LR
    G[typed graph] --> I[bounds.infer]
    I --> K[dict of facts]
    K --> Q[get / fits / report]
    K --> F[bounds.fold]
    F --> G2[revised graph]
```

## API

```jog
fn infer(m: Mod) -> dict;
fn get(known: dict, value: Val) -> list<int>;
fn fits(known: dict, value: Val, type: Ty) -> bool;
fn report(m: Mod) -> dict;
fn fold(m: Mod) -> bool;
```

## Inspect facts

```jog
mod range_policy
use bounds
use ir

fn narrow(m: Mod, value: Val) -> bool {
  let known = bounds.infer(m)
  let range = bounds.get(known, value)
  return len(range) == 2 && range[0] >= 0 && range[1] <= 255
}
```

An empty list means “not proved,” not `[0, 0]`.

For a proved value, the two integers are inclusive lower and upper bounds:

| Result | Interpretation |
| --- | --- |
| `[]` | no sound finite interval was proved |
| `[7, 7]` | exact constant seven |
| `[0, 255]` | any integer from zero through 255 |

This distinction prevents “unknown” from becoming an accidentally precise
zero during capacity planning.

## Report and fold

```sh
joggle query bounds.report model.jog -M build/modules > bounds.attr
joggle run bounds.fold model.jog -M build/modules > folded.jog
```

Read `bounds.attr` before selecting the mutating step. `fold` replaces only
expressions whose value follows from the same conservative analysis.

Example report shape:

```text
{
  "count": 3,
  "integers": 3,
  "values": {
    "limit": [16, 16],
    "offset": [0, 15],
    "width": [16, 16]
  }
}
```

The exact value names come from the input graph. Consumers should use
dictionary keys, not depend on pretty-print order.

## Type fit

```jog
fn can_store(m: Mod, value: Val, storage: Ty) -> bool {
  return bounds.fits(bounds.infer(m), value, storage)
}
```

This is useful before selecting a narrower integer representation. It is not a
cast and does not change `ir.type(value)`.

## Mechanism and limits

`infer` seeds integer constants and traverses operations in structured order.
Supported operations read facts already available for their operands; branch
yields join the selected arms. Unsupported producers retain unknown bounds.
The analysis does not iterate arbitrary loop-carried recurrences to a fixed point.

```mermaid
flowchart TD
  C[integer constants] --> W[structured traversal]
  P[recognized induction ranges] --> W
  W --> T[operation transfer / branch join]
  T --> S[SSA-keyed dictionary]
  U[unsupported call] --> X[unknown]
  X --> S
```

Structured loops seed induction-variable ranges from recognized `range`
inputs. Branch results are joined conservatively. Arithmetic uses checked
interval operations so overflow cannot turn into an unsound proof.

Integer division uses truncation toward zero. Endpoint pairs bound a quotient
when the divisor interval excludes zero; a possible signed overflow also
leaves the result unknown. For example, `[-7, 8] / [2, 3]` yields `[-3, 4]`,
whereas division by `[-1, 1]` yields `[]`. Reads from a scalar tensor filled
with a bounded integer preserve that interval, as do supported reads from
one-dimensional shape vectors. This lets dynamic `tensor.range` lengths use
the same analysis as other allocation sizes.

## Using bounds in a transform

Keep analysis and mutation separate:

```jog
fn narrow_safe_values(m: Mod) -> bool {
  let known = bounds.infer(m)
  var changed = false
  for value in ir.vals(m) {
    if bounds.fits(known, value, ty("i8")) {
      changed = ir.set(m, value, "project.narrowable", true) || changed
    }
  }
  return changed
}
```

The analysis dictionary describes one graph revision. After the first edit,
do not reuse it to justify a second structural change whose inputs may have
changed. Re-run `infer` or structure the pass so every decision refers to the
same pre-edit snapshot.

## Failure guide

| Observation | Meaning | Next step |
| --- | --- | --- |
| `get` returns `[]` | unsupported or unproved value | keep the wide representation |
| `fits` is false | interval exceeds storage or is unknown | do not narrow |
| `fold` returns false | no additional exact values | continue; this is not an error |
| report changes after an edit | dependent analysis was invalidated | recompute for the new revision |

> [!WARNING]
> A finite interval is a storage/correctness fact, not a probability
> distribution. It cannot justify “usually fits” optimizations.
