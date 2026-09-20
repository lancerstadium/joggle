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

You now have the language foundation. Continue with
[Compiler model](../compiler/index.md).
