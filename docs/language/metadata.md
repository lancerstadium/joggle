---
title: Metadata
description: Attach open data to functions, operations, and values without adding keywords.
---

# Metadata

Square brackets contain an open attribute dictionary:

```jog
[entry, stage: "serve"]
fn predict(
  [layout: "row-major"] input: tensor<f32, [1, 4]>
) -> tensor<f32, [1, 4]> {
  [implementation: "lut"]
  let output = nn.relu(input)
  return output
}
```

| Location | Metadata belongs to |
| --- | --- |
| before `fn` | function |
| inline before a parameter or binding | value |
| before a statement | root operation |

A bare key means `true`. Nested lists and dictionaries use normal `Attr`
syntax. Repeated brackets merge in key order; duplicate keys are errors.

## Metadata is data, not syntax

`entry`, `layout`, `implementation`, `target`, and `cost` have no core meaning.
A selected function must read them:

```jog
fn selected(op: Op) -> bool {
  return ir.has(op, "implementation") &&
         str(ir.meta(op, "implementation")) == "lut"
}
```

An edit uses the owning `Mod`:

```jog
fn mark(m: Mod, op: Op) -> bool {
  return ir.set(m, op, "implementation", "lut")
}
```

Metadata edits participate in revisions and transaction rollback. Installing a
mod never makes a key execute automatically.

## Nested data and dotted paths

Metadata can group ownership by subsystem:

```jog
[project: {
  decision: "vector",
  evidence: {width: 8, score: 17},
  alternatives: ["scalar", "vector"]
}]
fn kernel(x: i32) -> i32 { return x }
```

Readers should use their owned path and preserve unrelated keys. This allows
analysis, policy, and emission evidence to coexist without a central enum of
annotations.

```jog
fn decision(fn: Fn) -> str {
  if !ir.has(fn, "project.decision") { return "default" }
  return str(ir.meta(fn, "project.decision"))
}
```

## Input and output of an annotation transform

Input:

```jog
fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {
  let y = nn.relu(x)
  return y
}
```

Transform:

```jog
fn mark_relu(m: Mod) -> bool {
  var changed = false
  for op in ir.ops(m, ["call"]) {
    if ir.callee(op) == "nn.relu" {
      changed = ir.set(m, op, "project.selected", true) || changed
    }
  }
  return changed
}
```

Output:

```jog
fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {
  [project.selected]
  let y = nn.relu(x)
  return y
}
```

Running the transform again returns `false`: the requested value is already
present. This idempotence is useful for reactive and fixed-point workflows.

## Ownership convention

| Key prefix | Suggested owner | Example consumer |
| --- | --- | --- |
| `onnx.*`, `tflite.*` | frontend | semantic converter |
| `opt.*` | implementation selection | optimizer/reporting |
| `mem.*` | storage planner | target emitter |
| `c.*` | C target | C symbol/layout logic |
| `project.*` | external project mod | project policy |

Prefixes are conventions, not core-reserved syntax. A producer must document
the schema, valid locations, and whether the key is evidence, a request, or a
committed decision.

> [!IMPORTANT]
> Metadata does not prove legality. A key such as `project.vectorize: 8` is a
> request or record; the transform must still validate dependence, shape, and
> target constraints.

## Metadata versus types and operations

Use metadata only for open auxiliary data. If a fact changes overload
compatibility or result shape, encode it in the type/signature. If it changes
program semantics, use an explicit operation or function. This keeps type
checking and correctness independent from optional annotations.

## Failure guide

| Symptom | Cause | Correction |
| --- | --- | --- |
| key appears ignored | no selected function reads it | invoke/document its consumer |
| duplicate key error | same bracket set defines a path twice | merge at one source location |
| `str(...)` fails | leaf is not a string | check schema/kind before projection |
| edit disappears | enclosing transaction rolled back | inspect the first diagnostic |
| two mods overwrite a key | ownership prefix/schema is unclear | namespace keys and document precedence |

You now have the language foundation. Continue with
[Compiler model](../compiler/index.md).
