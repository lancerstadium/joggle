---
title: API shape and cost
description: Decide which representations belong in public Joggle APIs and which should remain typed implementation details.
---

# API shape and cost

Joggle has two deliberately different public data families:

| Family | Meaning | Required property |
| --- | --- | --- |
| `Attr` | owned recursive data for arguments, metadata, reports, and artifacts | serializable and lifetime-independent |
| `Mod/Fn/Blk/Op/Val/Ty` | typed compiler graph and handles | ownership, liveness, identity, invariants |

This distinction is healthy. An `Op` should not become an `Attr` dictionary:
doing so would lose store identity, generation checks, typed access, and safe
mutation.

## The telemetry boundary

Earlier iterations exposed separate query, run, step, function, and reactive
telemetry structs plus miss enums. The current API instead returns every public
report/profile as `Attr`. Typed counter and invalidation records remain private
to the evaluator.

This removes concrete costs from the public surface:

- a new internal counter does not add another public C++ member;
- tools persist the result without a second public conversion API;
- query, run, and reactive telemetry share one data vocabulary;
- users learn graph handles plus `Attr`, not evaluator record classes;
- implementation profiling fields can evolve under a documented schema.

## Implemented boundary

The clean boundary is:

```text
internal typed counters/dependency records
        ↓ one boundary conversion
public Attr profile/report
        ↓
CLI file, logs, tests, or application UI
```

Internal records should remain typed and private because they are hot,
mutation-heavy implementation state. Public observations should be `Attr`
with a documented schema because they are optional, open, and serialized.

## Current public shape

The implemented API has one overload family per operation, not a Cartesian
product of report/args/timing combinations:

```cpp
bool run(Env&, std::span<const std::string_view> functions, Mod&,
         std::span<const Attr> args = {}, Attr* report = nullptr,
         Attr* profile = nullptr);

bool query(Env&, std::string_view function, const Mod&, Attr& result,
           std::span<const Attr> args = {}, Attr* profile = nullptr);

bool ReactiveSchedule::run(Env&, Mod&,
                           std::span<const Attr> args = {},
                           Attr* report = nullptr);
```

Convenience overloads are justified only when they preserve one obvious
parameter order and do not duplicate implementation.

## Why three small records remain

The C++ surface still has `Loc`, `Diag`, and `Source`. They are not new compiler
entities parallel to `Attr` or graph handles:

| Record | Why it remains typed | Why it is not a general extension point |
|---|---|---|
| `Loc` | diagnostics need a compact file/line/column contract | no graph identity or evaluator state |
| `Diag` | callers need severity, message, and location without parsing text | fixed error transport only |
| `Source` | multi-file parsing needs text paired with a stable filename view | parse input only |

Turning these into dictionaries would weaken the basic embedding API and add
string-key lookup to every parse/diagnostic consumer. Conversely, adding a
public struct for each cache miss, stage timer, backend option, or mod-specific
concept would recreate the inconsistency this boundary removes.

The rule is therefore semantic, not cosmetic: retain a type when it is a small,
stable core contract; use `Attr` for open serializable data; use graph handles
for live identity; keep hot implementation records private.

## Schema principles

- Use stable semantic keys (`succeeded`, `cached`, `miss`, `steps`).
- Put optional low-level counters under a nested `counters` dictionary.
- Encode durations with an explicit unit suffix such as `_ns`.
- Add fields without making readers depend on dictionary order.
- Keep handles out of persisted reports; project stable names/paths.
- Version the schema only when key meaning changes incompatibly.

Example shape:

```text
{
  "succeeded": true,
  "snapshot_ns": 0,
  "steps": [{
    "function": "opt.basic",
    "evaluation_ns": 18340,
    "verification_ns": 4210,
    "counters": {"plan_hits": 7, "dispatch_misses": 1}
  }]
}
```

## What should remain private structures

| Internal structure | Why not `Attr` in the hot path |
| --- | --- |
| plan key and plan blocks | frequent typed indexing/hash lookup |
| frame/register window | dense execution state |
| dispatch entry | typed function/generic/revision relation |
| mutation undo journal | exact rollback ownership and variants |
| query dependency snapshots | generation/revision comparisons |
| reactive stage state | live cache state, not a serialized report |

“Fewer structs” is not itself the goal. The goal is fewer *public concepts* and
one coherent serialization/report boundary without replacing efficient private
state with string-keyed dynamic data.

## Review checklist for a new public type

1. Does it represent a durable compiler concept or only current telemetry?
2. Does it need identity/liveness/typed mutation like a graph handle?
3. Must applications persist or exchange it?
4. Would adding one field force source recompilation for no semantic reason?
5. Could an `Attr` schema express it more consistently?
6. If kept typed, can it be hidden behind `Impl` or returned as a view?

This checklist prevents implementation convenience from becoming permanent API
surface.
