---
title: opt mod
description: Generic folding, cleanup, specialization, capability worklists, and implementation selection.
---

# `opt`

`opt` contains reusable graph algorithms. It operates on calls, types,
capabilities, and supplied policy functions; it does not own a target pipeline.

## API map

| Area | Functions |
| --- | --- |
| folding/cleanup | `fold`, `fold_identity`, `fold_add_zero`, `dce`, `cse`, `hoist`, `basic`, `fix` |
| specialization | `specialize`, `bind`, `signature`, `instantiate`, `copy` |
| health | `count`, `unresolved`, `untyped` |
| capability | `supports`, `frontier`, `legalize`, `expose`, `expand` |
| implementation | `candidates`, `apply`, `instantiate` |
| structural update | `update`, `rename`, `fuse` |

## Cleanup

Input:

```jog
fn add_zero(x: i32) -> i32 {
  let y = x + 0
  return y
}
```

```sh
joggle run opt.fold_add_zero opt.basic model.jog \
  -M build/modules > clean.jog
```

Output:

```jog
fn add_zero(x: i32) -> i32 {
  return x
}
```

`basic` removes only operations already classified safe; effects are not
dropped merely because a result is unused.

## Health queries

```sh
joggle query opt.count model.jog --arg '"nn.relu"' -M build/modules
joggle query opt.unresolved model.jog -M build/modules
joggle query opt.untyped model.jog -M build/modules
```

These queries are useful stage contracts and do not edit the graph.

Example output:

```text
$ joggle query opt.count model.jog --arg '"nn.relu"' -M build/modules
3
$ joggle query opt.untyped model.jog -M build/modules
[]
```

`count` counts matching calls; an empty `untyped` result means every inspected
result has a closed type. Neither says the target supports the graph.

## Capability frontier

```jog
fn ready(m: Mod, accepts: Fn) -> bool {
  return len(opt.frontier(m, accepts)) == 0
}
```

`supports` checks one operation. `frontier` reports distinct unsupported calls.
`expand` exposes bodies rejected by a capability; `legalize` repeats supplied
capabilities up to an explicit limit.

## Implementation selection

```jog
fn apply(m: Mod) -> bool {
  let impls = ir.where(ir.fns("my_impl"), "role", "implementation")
  return opt.apply(m, impls)
}
```

Candidate matching uses normal overload resolution. Policy overloads may rank
the compatible list but must return members of that list and remain read-only.

The selection process is intentionally inspectable:

```mermaid
flowchart TD
  O[source call] --> C[typed compatible candidates]
  C --> P{policy supplied?}
  P -->|no| S[most-specific match]
  P -->|yes| R[policy ranks compatible list]
  S --> A[clone/bind implementation]
  R --> A
  A --> V[verify replacement]
  V --> E[redirect uses and erase source]
```

A policy cannot nominate a function that was absent from the compatible list.
That keeps profitability extensible without moving type safety into policy code.

### Instance lookup

`opt.instantiate(m, impls)` binds generic implementations into concrete local
functions. Calls with the same specialization key reuse a body; calls with
different shapes or bound constants produce separate bodies.

| Key component | What it distinguishes |
| --- | --- |
| qualified function and signature | implementations and overloads |
| applied generic arguments | concrete element types and shapes |
| static argument positions, types, and values | bound constants |

At the start of each invocation, `instantiate` indexes the existing local
functions carrying `opt.instance` metadata. New bodies enter that index as
they are created. Subsequent calls look up their key instead of enumerating
all functions again. The lookup checks the stored key before retargeting.
For example, 48 added calls alternating between two already-instantiated
shapes continue to use the same two bodies, including after a print/parse
round trip.

The index lives only for the current invocation. Selection policies cannot
mutate the graph, and the next invocation reconstructs the index from the
current functions. This lookup optimization does not track changes to source
implementation bodies or replace source-to-derived invalidation.

## Mechanism and limits

Worklists re-check live operations after structural edits and stop at explicit
limits. Failure rolls back the enclosing run. `opt` never assumes that every
model should follow one fixed order of functions.

### Fixed points are explicit

Functions that may repeat accept or enforce a limit. A limit is part of the
observable compiler configuration: it prevents a pair of valid rewrites from
oscillating forever and makes failure reproducible.

```jog
fn project_cleanup(m: Mod) -> bool {
  // Pure callees are a project decision, not guessed by opt.
  return opt.fix(m, ["base.copy", "tensor.broadcast"], 16)
}
```

### Folding keeps large ranges structured

`opt.fold` evaluates scalar expressions and supported constant control regions.
A range remains a pair of bounds in the IR; folding its consumers does not
require installing a literal list in the graph.

```jog
mod accumulation

fn accumulate(seed: int) -> int {
  var total = seed
  for i in 0..1000001 { total += 1 }
  return total
}
```

After `joggle run opt.fold accumulation.jog -M build/modules`, the loop remains
in the output. Its runtime result is `seed + 1000001`. The same range remains
structured when the initial value is constant: exceeding a folding resource
limit does not reject the runtime program.

| Context | Range behavior |
| --- | --- |
| Runtime graph | Keep bounds and a loop body for backend lowering |
| Optional scalar/control folding | Skip a fold requiring more than 1,000,000 range elements |
| Required metaprogram execution | Store two bounds and produce each index on demand |
| Export a range as an attribute list | Materialize at most 1,000,000 elements |
| Ascending range with end at or below start | Iterate zero times |

The optional-fold limit also applies to ranges reached inside nested constant
control flow. Small independent regions can still fold after a larger region
is skipped. This folding limit counts range elements, not total
interpreter instructions or total iterations across a nested loop nest.

Required metaprograms can iterate larger ranges without allocating a list of
indices. Copying a range, passing it to a function, and computing its memo key
all use the bounds. A return from a loop stops iteration immediately. The
iteration cost still grows with the number of executed iterations; only the
range storage is constant-space. Convergent legalization and exposure passes
return as soon as no further expansion occurs.

### Health before and after a stage

```console
$ joggle query opt.unresolved imported.jog -M build/modules
["onnx.CustomOp"]
$ joggle run project.convert imported.jog -M build/modules -M project-mods > converted.jog
$ joggle query opt.unresolved converted.jog -M build/modules -M project-mods
[]
```

This pattern turns a vague “conversion succeeded” statement into an explicit
stage contract.

## Failure guide

| Symptom | Cause | Fix |
| --- | --- | --- |
| cleanup changes nothing | graph is stable or purity list is incomplete | inspect calls; do not assume failure |
| legalize reaches limit | no convergent path under capabilities | inspect frontier and rewrite cycle |
| implementation ambiguity | equally specific candidates | narrow signatures or add policy |
| stale operation during policy | policy retained handles across edits | rediscover from live graph/path |
| partial-looking output after error | command failed before commit | use diagnostic; transaction rolled back |

> [!WARNING]
> `dce`, `cse`, and hoisting require correct purity/safety information. Do not
> classify an effecting call as pure to obtain a smaller graph.
