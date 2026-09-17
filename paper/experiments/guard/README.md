# Guard: guard folding in C preparation

The second derivation case, in a different compiler role. A copy of `c.prepare`
has its cleanup step retargeted so that it folds interval-proved guards, which
exercises a preparation procedure rather than a storage planner.

## What is here

| File | Role |
| --- | --- |
| `module.jog` | the `guard` module: the derived preparation recipe |
| `compiler.jog` | the derived compiler definition |
| `route.sh`, `reject.sh` | the accepted and rejected routes |
| `reject.log` | what the rejected route reports |
| `result.json` | the record: the edit, the artifact, the oracle, and the same-host latency |

## Records and boundaries

`result.json` holds the raw record. Its reading is that the derived procedure
folds 18 guards, emits C that matches the unchanged recipe byte for byte, and
produces bit-identical outputs, with same-host medians of 42.905 and 43.089 ms.
No latency change is claimed, and the two medians are a single-instance
comparison rather than a speed result. [`paper/data/README.md`](README.md)
introduces the record, and [`derive-prepare.md`](../derive-prepare.md) places the
case in the derivation argument.
