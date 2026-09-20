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

## Scalar widths and conversions

Joggle keeps scalar width in the type. This matters when a frontend, target,
or overload distinguishes an `i8` tensor from an `i32` tensor.

```jog
fn widths(flag: bool) -> (i8, i32, f32, f64) {
  let byte = i8(7)
  let count = i32(byte)
  let ratio = f32(count) / f32(4)
  let precise = f64(ratio)
  if flag { return byte, count, ratio, precise }
  return i8(0), i32(0), f32(0), f64(0)
}
```

Conversions are ordinary typed calls. They are visible to resolution and
target preparation; they are not implicit host-language casts hidden from the
graph.

| Family | Typical values | Why it is explicit |
| --- | --- | --- |
| `bool` | conditions and policy results | separates predicates from integers |
| `i8`…`i64`, `u8`…`u64` | model data, indices, ABI scalars | preserves width and signedness |
| `f16`, `bf16`, `f32`, `f64` | model and compiler arithmetic | preserves numerical intent |
| `int` | compile-time extents/configuration | convenient structural arithmetic |
| `str`, `bytes` | names and opaque payloads | keeps text separate from binary data |

## Indexing and update

Lists, dictionaries, and tensor-like library values use the same expression
surface, but their types define the operation:

```jog
fn revise(values: list<int>) -> dict {
  var copy = values
  if len(copy) > 0 { copy[0] = copy[0] + 1 }

  var out: dict = {}
  out["count"] = len(copy)
  out["values"] = copy
  return out
}
```

For input `[3, 5]`, the returned structural value prints as:

```text
{"count": 2, "values": [4, 5]}
```

The original argument remains a graph value. Source-level mutation creates
new carried state; it does not mutate a C++ container behind the verifier.

## `Attr` as the serialization boundary

`Attr` is the common recursive data domain used by CLI arguments, reports,
metadata, and query results:

```text
Attr = nil | bool | int | float | str | bytes | list<Attr> | dict<str, Attr>
```

This makes a query result easy to inspect and persist without inventing a new
schema language. Typed compiler handles such as `Op`, `Val`, and `Fn` are not
serializable `Attr` leaves; project them to stable names, types, or metadata
before returning a report.

```jog
fn describe(op: Op) -> dict {
  var row: dict = {}
  row["kind"] = ir.kind(op)
  row["name"] = ir.name(op)
  row["metadata"] = ir.meta(op)
  return row
}
```

## Empty collection inference

An empty collection has no element from which to infer a type. State the type
at the boundary:

```jog
var worklist: list<Op> = []
var report: dict = {}
```

For non-empty collections, all elements must unify:

```jog
let axes = [0, 2, 3]       // list<int>
let names = ["n", "h"]    // list<str>
```

Mixing unrelated element types is rejected during checking rather than being
deferred to an evaluator error.

## Failure guide

| Symptom | Cause | Correction |
| --- | --- | --- |
| empty list has open type | no element constrains `T` | annotate `list<T>` |
| overload is ambiguous | literal width leaves two candidates | use `i32(...)`, `f32(...)`, etc. |
| missing dictionary key | strict indexing used for optional data | use `has` or `get` |
| expected string but received `Attr` | structural value not projected | use `str(...)` after checking its kind |
| report cannot serialize | it contains graph handles | project handles to names/types/data |

Continue with [Control flow](control-flow.md).
