---
title: sat.c mod
description: Prepare saturating semantic operations for the C target.
---

# `sat.c`

`sat.c` is the C representation companion for `sat`. It does not define the
type's arithmetic meaning.

## API

```jog
fn prepare(m: Mod) -> bool;
```

## Pipeline

```sh
joggle run sat.select model.jog -M build/modules > selected.jog
joggle run sat.materialize selected.jog \
  --arg '[8, 16]' --arg '[i8, i16]' \
  -M build/modules > materialized.jog
joggle run sat.c.prepare c.prepare mem.plan materialized.jog \
  -M build/modules > planned.jog
joggle query c.frontier planned.jog -M build/modules
```

An empty final frontier proves representation coverage for the prepared graph.

Expected query result:

```text
[]
```

If the list still contains a saturating helper, stop before emission and inspect
whether its width was included in the materialization policy.

## Ownership

| Concern | Owner |
| --- | --- |
| saturation arithmetic | `sat` |
| width-to-storage policy | caller + `sat.materialize` |
| C-specific representation rewrite | `sat.c` |
| ABI and source emission | `c` |

Changing C spelling does not change `sat<W>` or VM behavior.

## Internal mechanism

`sat.c.prepare` recognizes the concrete helpers produced by `sat.materialize`
and attaches or rewrites the C-facing representation needed by the generic `c`
mod. It does not emit a translation unit itself.

```mermaid
flowchart LR
  S[sat materialized graph] --> P[sat.c.prepare]
  P --> C[c.prepare]
  C --> M[mem.plan]
  M --> E[c.source]
```

This ordering matters. Running `c.prepare` first cannot anticipate a
project-defined semantic type that has not been materialized.

## Inspectable input and output

Before preparation, a function may still call a width-specific saturating
helper. After preparation, the helper has a C-representable signature and body
or binding, while the surrounding model remains ordinary `.jog`. Query
`c.frontier` between stages instead of inferring readiness from textual names.

## Failure guide

| Symptom | Cause | Fix |
| --- | --- | --- |
| `sat.c.prepare` returns false | nothing eligible or graph already prepared | inspect selected/materialized graph |
| saturating call stays on frontier | width helper missing | fix `sat.materialize` mapping |
| generated C type is unexpected | storage policy chose a different type | review explicit limit/type arguments |
| compile failure in helper | representation contract incomplete | inspect emitted declaration and definition |

> [!IMPORTANT]
> Keep semantic overflow behavior in `sat` and C representation in `sat.c`.
> Duplicating clamping policy in both makes cross-target behavior drift.
