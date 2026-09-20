---
title: Values and collections
description: Bind immutable and mutable values and work with literals, lists, dictionaries, and attributes.
---

# Values and collections

## Bindings

```jog
let fixed = 4
var changing = 0
changing += fixed
```

`let` is immutable. `var` permits source-level assignment. Both are typed graph
values; mutation across control flow becomes explicit carried state internally.

## Literals

```jog
let enabled = true
let count: i64 = i64(8)
let ratio: f32 = f32(0.5)
let label = "fast"
let payload: bytes = hex"00ff7f"
```

Explicit scalar constructors are useful when overload choice depends on width.

## Lists

```jog
var order = [0, 1, 2]
order[1] = 7
for axis in order { inspect(axis) }
```

Empty lists retain a declared element type:

```jog
let types: list<Ty> = []
```

## Dictionaries and `Attr`

```jog
var report = {stage: "convert", changed: true}
report["ops"] = 12

let stage = str(report["stage"])
let missing = get(report, "device", "portable")
```

Dictionary values are open `Attr` data. Indexing is strict; `get` returns
`nil` or a supplied fallback. `has`, `keys`, `kind`, and `len` inspect
structure.

## Value versus printed text

`str(attr)` projects a string leaf. `text(attr)` returns canonical source text,
including quotes and escaping. Use structured values across APIs and render
text only at a presentation or artifact boundary.

Continue with [Control flow](control-flow.md).
