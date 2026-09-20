---
title: tile mod
description: Analyze affine accesses and perform legality-checked loop transformations.
---

# `tile`

`tile` works on ordinary loop/control-flow graph structure. It contains no Conv,
frontend, tensor-rank, or target-name dispatch.

## Evidence API

| Evidence | Function |
| --- | --- |
| dependency and axes | `depends`, `axes` |
| indexed accesses | `reads`, `writes` |
| affine form | `affine`, `read_forms`, `write_forms` |
| loop roles | `state_axes`, `reduction_axes`, `extents` |
| normalization | `canon` |

An affine form is `[offset, coefficient0, coefficient1, ...]` in loop-axis
order. An empty inner form means an access exists but was not proved affine.

## Legality pattern

Every structural edit exposes the same decision three ways:

```jog
let reason = tile.reorder_issue(m, loop, order)
let legal = tile.can_reorder(m, loop, order)
let result = tile.reorder(m, loop, order)
```

An empty reason means legal. The mutating form reuses that decision and returns
the replacement `Op` for direct composition.

## Edit families

| Edit | Purpose |
| --- | --- |
| `split` / `peel` / `merge` | reshape adjacent iteration axes |
| `reorder` | permute axes while preserving proved dependencies |
| `scalarize` | promote eligible tensor-carried state to scalar lanes |
| `unroll` | duplicate a bounded loop factor |
| `fuse` | combine compatible producer/consumer loops |
| `canon` | rebuild private affine indices in deterministic form |

## Policy example

```jog
mod inner_state
use ir
use tile

fn order(m: Mod, loop: Op) -> list<int> {
  let state = tile.state_axes(m, loop)
  let reduction = tile.reduction_axes(m, loop)
  if len(state) == 0 || len(reduction) == 0 { return [] }
  return reduction + state
}

fn apply(m: Mod) -> bool {
  return tile.reorder(m, ir.find("inner_state.order"))
}
```

The policy decides profitability; `tile` owns correctness. If the order changes
reduction semantics or violates carried-state addresses, the candidate is
rejected.

## Direct composition

```jog
let split = tile.split(m, loop, axis, 4)
let moved = tile.reorder(m, split, [0, 2, 1])
let scalar = tile.scalarize(m, moved, 4)
```

Returned live operations avoid rediscovery scans between edits.

## Mechanism and limits

Legality uses static ranges, affine addresses, carried-state roles, injectivity,
and relative reduction order. Unsupported forms remain unchanged. Target cost
is intentionally not guessed; external policies consume the evidence.

See the complete [locality-policy example](../../../examples/locality.md).

## Before and after a split

Conceptual input:

```jog
for i in 0..10 {
  // body(i)
}
```

After splitting by four, the transformation creates an outer range and an
inner range. Because ten is not divisible by four, it preserves the tail with
a proved guard rather than dropping iterations:

```jog
for outer in 0..3, inner in 0..4 {
  let i = outer * 4 + inner
  if i < 10 {
    // body(i)
  }
}
```

The exact canonical graph may print differently, but the iteration domain and
carried-state results are preserved.

## Legality and profitability

Every major edit keeps these separate:

| Question | API style | Owner |
| --- | --- | --- |
| why is candidate invalid? | `*_issue(...) -> str` | `tile` legality |
| is candidate valid? | `can_*(...) -> bool` | `tile` legality |
| which valid candidate is desirable? | policy callback/cost | project mod |
| perform chosen edit | mutating `*` overload | `tile` transform |

This lets a hardware-specific policy evolve without duplicating dependence and
address proofs.

## Affine form interpretation

For loop axes `[i, j]`, an access `base + 8*i + j` has form:

```text
[base, 8, 1]
```

The first item is the offset; remaining items are coefficients in loop-axis
order. Empty evidence means the relation was not proved affine. It is not a
zero-stride access.

## Edit-specific contracts

### `split`

Splits one iteration axis by a positive factor and preserves a non-divisible
tail. Rejects invalid axis, factor, range, or block structure.

### `peel`

Separates prefix/tail regions around an alignment factor when constant bounds
make the partition provable.

### `merge`

Combines adjacent compatible axes while preserving the original iteration
mapping and carried state.

### `reorder`

Permutes axes only when write/read dependencies, reduction order, and carried
state allow it.

### `scalarize`

Promotes eligible tensor-carried reduction state into a bounded set of scalar
lanes. Its budget limits lane expansion explicitly.

### `unroll`

Duplicates a statically bounded innermost loop factor and retains correct tail
semantics.

### `fuse`

Combines compatible producer/consumer loops when dataflow, ranges, addresses,
and effects allow one shared iteration structure.

### `canon`

Normalizes private affine index expressions deterministically. It does not
select a target-specific profitability policy.

## Internal organization

| Fragment | Responsibility |
| --- | --- |
| `access.jog` | dependency, read, and write discovery |
| `affine.jog`, `form.jog` | affine reconstruction and public forms |
| `match.jog` | structural loop matching helpers |
| `split.jog`, `peel.jog`, `merge.jog` | axis-shape edits |
| `reorder.jog` | permutation legality and reconstruction |
| `scalarize.jog` | lane analysis and scalar rebuild |
| `unroll.jog` | bounded duplication |
| `fuse.jog` | producer/consumer fusion |
| `canon.jog` | index normalization |

## Failure guide

| Issue text/non-change | Likely reason | Next step |
| --- | --- | --- |
| non-affine access | index cannot be expressed in current loop axes | keep loop or normalize source |
| invalid permutation | not a complete axis permutation | correct order list |
| dependence/reduction conflict | order changes semantics | choose another order |
| scalar budget exceeded | lane expansion too large | lower budget/factor or keep tensor state |
| fusion unavailable | ranges, state, or dataflow incompatible | inspect `fuse_issue` and candidates |

> [!IMPORTANT]
> `tile` returning false can mean the graph was already stable or no legal
> candidate existed. Use issue/evidence APIs when a policy needs to distinguish
> those cases.
