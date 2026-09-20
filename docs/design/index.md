---
title: System design
description: The implemented structure of Joggle from source model to artifact.
---

# System design

Joggle is a C++20 compiler library and command-line tool. Its source format,
compiler functions, and readable IR use the same `.jog` language.

## System at a glance

```text
model or frontend bytes
          │
          ▼
    typed .jog Mod
          │
          ├── query: inspect and return Attr data
          ├── run:   transactionally edit the Mod
          └── emit:  produce text or bytes
                         │
                         ▼
                    C or VM artifact
```

The arrows are explicit commands or API calls. Import, semantic conversion,
optimization, storage planning, and emission are not hidden behind one fixed
pipeline.

## Program representation

| Handle | Responsibility |
| --- | --- |
| `Mod` | program graph, functions, dependencies, diagnostics, and revisions |
| `Fn` | signature, generic parameters, metadata, and optional body |
| `Blk` | ordered operations and block arguments |
| `Op` | constant, call, loop, branch, return, or yield |
| `Val` | typed parameter, block argument, or operation result |
| `Ty` | structural type |
| `Attr` | compile-time scalar or collection data |

The same representation holds an abstract operator call and its exposed loop
body. Transformations choose how much detail to expose; the core does not force
a sequence of intermediate dialects.

## Compiler functions

Typed functions define the public compiler surface:

- a query accepts a `Mod` or handles and returns attributes;
- a transformation edits through `ir` and returns whether it changed the graph;
- a reader converts bytes into a `Mod`;
- an emitter converts a checked `Mod` into text or bytes.

Function sequences passed to `run` are one transaction. The final state is
verified before it is published.

## Component map

```text
include/joggle/   public C++ embedding API
src/              parser, IR, verifier, resolver, evaluator
tool/             joggle CLI
modules/          installed source and optional native mods
extensions/       out-of-tree examples
test/             unit and end-to-end behavior gates
```

Continue with [Module organization](modules.md) and
[Execution and updates](execution.md). For hands-on work, go to the
[tutorials](../tutorials/index.md).
