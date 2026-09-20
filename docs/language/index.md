---
title: Language
description: Learn Joggle syntax in a deliberate order before programming the compiler graph.
---

# Language

The language section explains `.jog` as a programming language. It does not
teach compiler architecture or a target pipeline. Examples start with scalars;
tensor is introduced later as one library-defined structural type.

| Order | Topic | You will learn |
| --- | --- | --- |
| 1 | [Packages](packages.md) | `mod`, `use`, files, visibility, search roots |
| 2 | [Functions](functions.md) | declarations, definitions, parameters, results, overloads |
| 3 | [Types](types.md) | built-ins, structural types, generics, user type constructors |
| 4 | [Values and collections](values.md) | bindings, literals, lists, dictionaries, attributes |
| 5 | [Control flow](control-flow.md) | `if`, `for`, mutation, return, short circuit |
| 6 | [Metadata](metadata.md) | open annotations and who gives them meaning |

## Small complete program

```jog
mod hello
use base

local fn clamp_zero(x: i32) -> i32 {
  if x < 0 { return 0 }
  return x
}

fn twice_nonnegative(x: i32) -> i32 {
  let safe = clamp_zero(x)
  return safe + safe
}
```

The language has no `pass`, `graph`, `kernel`, `rewrite`, or `emit` syntax.
Compiler behavior is written with the same functions after learning the graph
API in [Compiler model](../compiler/index.md).

> [!IMPORTANT]
> `mod` is the only package declaration and CLI noun. `module` is rejected and
> has no compatibility alias.
