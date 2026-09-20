---
title: API
description: Public CLI, C++, native ABI, and bundled mod surfaces.
---

# API

API pages are lookup material after the language and compiler model.

| Surface | Use it when |
| --- | --- |
| [CLI](cli.md) | driving checked workflows from a shell or build system |
| [C++ API](cpp.md) | embedding parsing, graph edits, execution, or reactive updates |
| [Native mod ABI](native.md) | binding a codec or host function across a narrow C ABI |
| [Built-in mods](mods/index.md) | calling installed language, analysis, transform, frontend, or target functions |

The exact overloads installed in a build are always available through:

```sh
joggle mod info NAME -M build/modules
```

Documentation explains contracts and composition. `mod info` reports the
concrete declarations in the binary/source package on disk.
