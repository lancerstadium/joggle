---
title: Packages
description: Define a mod, declare dependencies, split source files, and control visibility.
---

# Packages

## Smallest layout

```text
math_policy/
  module.jog
```

```jog
mod math_policy
use base
```

The directory name and declaration identify one mod. The parent directory is a
search root:

```sh
joggle mod check math_policy -M local-mods
```

## Dependencies

`use` is explicit and transitive loading follows the declared graph:

```jog
mod app.policy
use ir
use opt
```

A sibling directory is not an implicit dependency. Add its parent with another
`-M` and declare it with `use`.

## Split a large mod

```text
app.policy/
  module.jog
  lib/
    analysis.jog
    rewrite.jog
```

`module.jog` is read first; `lib/*.jog` follows in sorted order. Every fragment
belongs to the same lexical mod and must not repeat `mod` or `use`.

```jog
// lib/analysis.jog
local fn score(x: i32) -> i32 {
  return x * 2
}
```

## Public and local functions

```jog
fn apply(m: Mod) -> bool { return helper(m) }
local fn helper(m: Mod) -> bool { return false }
```

`apply` appears in `joggle mod info`; `helper` is visible only inside the mod.

## Lifecycle

```sh
joggle mod list -M local-mods
joggle mod info math_policy -M local-mods
joggle mod install local-mods/math_policy installed-mods -M local-mods
joggle mod upgrade local-mods/math_policy installed-mods -M local-mods
joggle mod uninstall math_policy installed-mods
```

Install validates a staged copy. Upgrade also checks installed reverse
dependencies. Continue with [Functions](functions.md).
