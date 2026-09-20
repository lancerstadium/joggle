---
title: Compiler functions
description: Write queries, transformations, conversions, and emitters as ordinary Joggle functions.
---

# Compiler functions

## Query

```jog
mod inspect
use ir

fn call_count(m: Mod) -> int {
  return len(ir.ops(m, ["call"]))
}
```

```sh
joggle query inspect.call_count model.jog -M modules -M local-mods
```

A query returns `Attr`-compatible data and may not publish graph edits.

## Transformation

```jog
fn mark_calls(m: Mod) -> bool {
  var changed = false
  for op in ir.ops(m, ["call"]) {
    changed = ir.set(m, op, "visited", true) || changed
  }
  return changed
}
```

```sh
joggle run inspect.mark_calls model.jog \
  -M modules -M local-mods > marked.jog
```

Returning `bool` reports whether the function changed the graph. The enclosing
`run`, not the individual edit, is the commit boundary.

## Conversion

A converter is a transform whose contract changes representation ownership:

```jog
fn convert(m: Mod) -> bool {
  // Find source calls, prove a mapping, replace them with shared semantics.
  return false
}
```

There is no separate converter syntax. Its mod documents source and destination
capabilities.

## Artifact function

```jog
fn manifest(m: Mod) -> str {
  return "functions=" + text(len(ir.fns(m))) + "\n"
}
```

```sh
joggle emit inspect.manifest model.jog \
  -M modules -M local-mods > manifest.txt
```

Artifact functions return `str` or `bytes`. They should expose an `accepts` or
`frontier` query when they support only part of the graph.

## Composition

```sh
joggle run first.apply second.apply model.jog -M modules > result.jog
```

Names execute left-to-right in one verified transaction. Installing a mod does
not run any function automatically.

## Function roles are contracts, not syntax

| Role | Typical signature | Reads graph | Mutates graph | Returns |
| --- | --- | ---: | ---: | --- |
| query | `fn report(m: Mod) -> dict` | yes | no | `Attr`-compatible value |
| transform | `fn apply(m: Mod) -> bool` | yes | yes | changed flag |
| converter | `fn convert(m: Mod) -> bool` | yes | yes | changed flag |
| capability | `fn accepts(m: Mod, op: Op) -> bool` | yes | no | decision |
| policy callback | `fn choose(op: Op, options: list<Fn>) -> Fn` | yes | no | selected candidate |
| emitter | `fn source(m: Mod) -> str` | yes | no | artifact |
| binary reader | `fn read(data: bytes) -> str` | no graph input | no | `.jog` source |

The evaluator enforces types and transactions. The mod/page defines the
semantic contract, including whether a callback must be read-only.

## Arguments

CLI `--arg` values are parsed as `Attr` and matched after the graph parameter:

```jog
fn apply(m: Mod, threshold: int, options: dict) -> bool {
  return false
}
```

```console
$ joggle run policy.apply model.jog \
    --arg 16 --arg '{mode: "safe"}' \
    -M build/modules -M project-mods
```

Arguments are part of query/reactive cache identity. A different dictionary or
threshold invalidates reuse even when the graph is unchanged.

## Callback functions

Compiler functions can receive a `Fn` handle and invoke it through `ir.invoke`.
This is how `stat`, `tile`, and `opt` accept project policy without a global
registry.

```jog
fn score(op: Op, config: Attr) -> int {
  if ir.callee(op) == "tensor.matmul" { return int(config["matmul"]) }
  return 1
}

fn total(m: Mod, config: dict) -> int {
  return stat.sum(m, ir.find("policy.score"), config)
}
```

Typed matching checks the callback signature before invocation. The receiving
algorithm should document whether the callback may mutate; analysis and policy
callbacks are normally read-only.

## Reports versus artifacts

A transform may return a deterministic report through the run API while the
updated graph remains the primary output. An emitter returns the artifact
itself. Keep timing separate from deterministic reports.

| Output | Intended use |
| --- | --- |
| changed flag | fixed-point control and stage reporting |
| `dict`/list query | scripts, diagnostics, inspection |
| run report | deterministic facts about executed edits |
| run timing | profiling only |
| string/bytes emission | target artifact |

## Failure and rollback

`assert`, unresolved calls, callback contract violations, invalid edits, or
final verification can fail a compiler function. In a multi-stage `run`, any
failure rolls back every earlier stage in that call.

If independent commits are wanted, invoke separate `run` commands and persist
their outputs explicitly.

## Designing a new compiler function

1. Name the responsibility and owner mod.
2. Choose query, mutating, callback, or artifact semantics.
3. Give parameters narrow types instead of one unstructured options bag where
   the shape is stable.
4. Return a changed flag only when a graph mutation committed.
5. Make unsupported cases visible through a reason/frontier/report.
6. Separate legality from profitability.
7. Document one complete input, command/call, output, and failure.

> [!IMPORTANT]
> A function named `convert`, `emit`, or `pass` receives no special privilege.
> Its signature, body/native binding, and documented contract define behavior.
