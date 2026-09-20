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

## Read the graph change, not only the command

The transform operates on graph identity rather than source text. Conceptually
the input contains these edges:

```text
x ──► operator + ──► y ──► return
        ▲
        └── constant 0
```

After replacement, the return consumes `x` directly and the unused addition and
constant can be erased:

```text
x ─────────────────► return
```

This distinction matters for nested blocks and repeated text: a graph rewrite
changes the selected definition/use relation, not every matching character
sequence in the file.

## Compose functions

```sh
./build/joggle run opt.fold_add_zero opt.basic checked.jog \
  -M build/modules > optimized.jog
```

Both functions execute left-to-right inside one transaction. If either fails or
final verification fails, no partial graph is printed.

For independent checkpoints, use two invocations:

```sh
./build/joggle run opt.fold_add_zero checked.jog \
  -M build/modules > stage-1.jog
./build/joggle run opt.basic stage-1.jog \
  -M build/modules > stage-2.jog
```

Now each saved file is a verified boundary. This is useful when comparing stage
effects; it intentionally gives up all-or-nothing composition across commands.

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

## Write a project transform

The same workflow accepts an out-of-tree function. This transform marks calls
whose callee matches an explicit argument:

```jog
mod annotate
use ir

fn calls(m: Mod, callee: str, label: str) -> bool {
  var changed = false
  for op in ir.ops(m, ["call"]) {
    if ir.callee(op) == callee {
      changed = ir.set(m, op, "project.label", label) || changed
    }
  }
  return changed
}
```

```sh
./build/joggle run annotate.calls checked.jog \
  --arg '"operator +"' --arg '"candidate"' \
  -M build/modules -M project-mods > annotated.jog
```

Relevant output:

```jog
[project.label: "candidate"]
let y = x + 0
```

The metadata key is open: the core preserves it without assigning target or
domain meaning. A later policy can query it through `ir.get`.

## Fixed points and changed flags

A transform returning `false` means it committed no graph change. That is useful
for fixed-point drivers, but it is not proof that the graph is globally optimal.
The function's documented preconditions define what “no change” means.

## Capture evidence

```sh
./build/joggle run opt.basic checked.jog \
  --report run.attr --timing timing.attr \
  -M build/modules > optimized.jog
```

Compare `run.attr` structurally. Treat `timing.attr` as measurement requiring
repetitions, warmup control, and distribution reporting.

The graph goes to standard output, deterministic stage facts go to `run.attr`,
and timing goes to `timing.attr`. Keeping these channels separate prevents a
benchmark timestamp from making a structural fixture nondeterministic.

## Verify semantic behavior

Structural verification proves graph invariants, not equivalence to the input.
For an arithmetic rewrite, add representative and boundary inputs to an
executable oracle. For floating-point rewrites, state whether NaNs, signed zero,
overflow, and reassociation are preserved. Do not infer semantic correctness
from an empty unresolved-call report.

## Diagnose

| Failure | Next inspection |
| --- | --- |
| unresolved function | `opt.unresolved` on input and `mod info` for provider |
| open type | `opt.untyped`, then inference stage |
| transaction failure | structured diagnostics; input remains unchanged |
| unexpected no-op | inspect exact callee/type and transform preconditions |
| valid but wrong result | add a semantic oracle; verification is structural |
| stale `Op` after edit | consume the replacement handle returned by the edit |

## Developer checklist

1. Publish the accepted input pattern in typed terms.
2. Separate legality from profitability.
3. Collect candidates before mutation when traversal would be invalidated.
4. Use returned live handles after replacement.
5. Return `true` only for a committed structural change.
6. Test no-op, rejection, nested structure, repeated application, and rollback.
7. Show real before/after Joggle in the mod's documentation.
