---
title: Contributing
description: Repository ownership, documentation contracts, and test organization for Joggle development.
---

# Contributing

## Repository ownership

| Path | Role |
| --- | --- |
| `include/joggle/` | supported C++ and native ABI |
| `src/` | core implementation |
| `modules/` | installed official mods |
| `examples/mods/` | runnable external-author examples |
| `test/data/` | regression inputs, not user templates |
| `test/tools/` | fixture generators/download helpers |
| `docs/` | user/developer documentation site |
| `paper/` | separate manuscript workspace |

## Change checklist

1. Choose the owning subsystem or mod before editing.
2. Keep public behavior in supported headers/functions, not `src/` internals.
3. Add the narrowest executable oracle.
4. Document input, output, and failure boundary without requiring test reading.
5. Run focused labels, then the complete configured suite.

See [Testing](testing.md) for suite organization.
