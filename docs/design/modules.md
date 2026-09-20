---
title: Module organization
description: How Joggle packages source, dependencies, public functions, and optional native code.
---

# Module organization

A `mod` is the package boundary. It can contain semantics, analysis,
transformation, conversion, and artifact code for one responsibility.

## Layout

The smallest package contains one file:

```text
my_mod/
  module.jog
```

A larger package may split implementation files and add one native library:

```text
my_mod/
  module.jog
  lib/
    analysis.jog
    rewrite.jog
  native/
    joggle_my_mod.so
```

`module.jog` declares the package and its dependencies:

```jog
mod my_mod
use ir
use tensor
```

All source fragments form one module. Public `fn` definitions appear in
`joggle mod info`; `local fn` definitions stay private.

## Discovery

Module roots are always explicit:

```sh
joggle mod list -M build/modules -M local-mods
joggle mod info my_mod -M build/modules -M local-mods
joggle mod check my_mod -M build/modules -M local-mods
```

Names resolve through these roots, and `use` dependencies close transitively.
Directory adjacency is not an implicit dependency.

## Lifecycle

```sh
joggle mod install path/to/my_mod installed-mods -M build/modules
joggle mod upgrade path/to/my_mod installed-mods -M build/modules
joggle mod uninstall my_mod installed-mods
```

Install validates a staged copy before making it visible. Upgrade also checks
installed reverse dependencies and rejects changes that would silently select
a different declaration.

## Native boundary

Native code is optional. It is appropriate for binary codecs or host artifact
execution, not as a second registration path for ordinary transformations.
Only explicitly represented scalar values cross the native value ABI; IR
handles and collections remain in compile-time execution.

See [Write a mod](../tutorials/write-mod.md) for a complete small package and
the [mod catalogue](../reference/module-catalogue.md) for bundled packages.
