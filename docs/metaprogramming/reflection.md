---
title: Reflection
description: Enumerate functions, blocks, operations, values, types, metadata, calls, ownership, and def-use without mutating the graph.
---

# Reflection

Reflection turns the current typed graph into ordinary compile-time data and
handle collections. It is the foundation for diagnostics, analysis, selection,
conversion, and artifact generation.

## Inventory example

```jog
fn inventory(m: Mod) -> dict {
  let functions = len(ir.fns(m))
  let operations = len(ir.ops(m))
  let calls = len(ir.ops(m, ["call"]))
  var out: dict = {}
  out["functions"] = functions
  out["operations"] = operations
  out["calls"] = calls
  return out
}
```

Input:

```jog
fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {
  let y = nn.relu(x)
  return y
}
```

Output:

```text
{"calls": 1, "functions": 1, "operations": 2}
```

There are two operations: the call and the return. Parameters and call results
are values, not operations.

## Navigate top down

```jog
fn describe(m: Mod) -> list<dict> {
  var rows: list<dict> = []
  for fn in ir.fns(m) {
    var row: dict = {}
    row["name"] = ir.name(fn)
    row["params"] = len(ir.params(fn))
    row["returns"] = len(ir.returns(fn))
    row["blocks"] = len(ir.blks(fn))
    row["ops"] = len(ir.ops(fn))
    rows += [row]
  }
  return rows
}
```

The enumeration order is deterministic structural order. Nested blocks are
included by `ir.ops(fn)`; use `ir.ops(block)` when only direct children matter.

## Inspect one call

```jog
fn call_row(m: Mod, op: Op) -> dict {
  assert(ir.kind(op) == "call", "expected a call")
  var row: dict = {}
  row["callee"] = ir.callee(op)
  row["target"] = ir.target(m, op)
  row["inputs"] = len(ir.args(op))
  row["outputs"] = len(ir.outs(op))
  row["path"] = ir.path(m, op)
  return row
}
```

`callee` is the written call spelling. `target` reflects typed resolution. Use
the latter for capability/implementation decisions when qualification and
overloads matter.

## Def-use reflection

```jog
fn uses(m: Mod) -> list<dict> {
  var rows: list<dict> = []
  for value in ir.vals(m) {
    var row: dict = {}
    row["name"] = ir.name(value)
    row["type"] = text(ir.type(value))
    row["users"] = len(ir.users(value))
    rows += [row]
  }
  return rows
}
```

For operation results, `ir.def(value)` returns the defining operation. Function
parameters and block arguments do not have an ordinary defining call/constant,
so algorithms should understand the value category they accept.

## Types are reflectable trees

```jog
fn explain_type(type: Ty) -> dict {
  var out: dict = {}
  out["text"] = text(type)
  out["name"] = base.name(type)
  out["arguments"] = base.args(type)
  return out
}
```

For `tensor<f32, [2, 4]>`, the outer name is `tensor`; arguments are structural
terms representing `f32` and `[2,4]`. The `tensor` mod supplies higher-level
helpers such as `valid`, `elem`, and `dims`.

## Metadata reflection

```jog
fn selected(op: Op) -> bool {
  return ir.has(op, "meta_demo.selected")
}
```

Use `ir.meta(item)` for the complete dictionary or `ir.meta(item, key)` for one
path. Open keys let project policy add evidence without extending a core node
class.

## Resolve functions as data

```jog
fn relu_declarations() -> list<Fn> {
  var out: list<Fn> = []
  for fn in ir.fns("nn") {
    if ir.name(fn) == "relu" { out += [fn] }
  }
  return out
}
```

`Fn` values can be filtered by metadata with `ir.where`, matched against a call
with `ir.match`, or passed to another compiler function as a callback.

## Query contract

Run reflection through `query` when the public result is `Attr`-compatible:

```console
$ joggle query meta_demo.inventory meta_model.jog \
    -M build/modules -M tutorial-mods
```

Query evaluation records observed objects and refuses to publish graph edits.
The C++ API can request an `Attr` query profile to see whether a cached result was used
and which dependency category caused a miss.

## Reflection anti-patterns

| Anti-pattern | Problem | Better approach |
| --- | --- | --- |
| compare printed operation text | formatting is not a semantic API | use kind/callee/target/types/metadata |
| retain `Op` after erasing it | handle becomes stale | rediscover or use validated paths |
| infer effects from no users | unused result may still effect | use an explicit purity contract |
| treat `_` as a concrete dimension | unknown becomes unsound fact | preserve open term or infer it |
| mutate inside analysis callback | invalidates traversal/contracts | separate decision and transform |

## Practical uses

- produce stage health reports (`unresolved`, `untyped`, counts);
- collect candidates for a transform;
- inspect source-format metadata during conversion;
- derive project-defined reports or artifacts;
- compute costs with caller-provided policies;
- record fine-grained dependencies for reactive execution.

Continue with [Graph generation](generation.md) when observation alone is not
enough.
