---
title: Guides
description: End-to-end workflows with source, commands, intermediate states, outputs, and diagnosis.
---

# Guides

Guides assume the [Language](../language/index.md) and show complete tasks.

```mermaid
flowchart LR
    A[transform .jog] --> B[author external mod]
    B --> C[import ONNX]
    C --> D[prepare and emit C]
```

| Guide | Input | Visible result |
| --- | --- | --- |
| [Transform a program](transform.md) | checked `.jog` | revised `.jog`, query, report |
| [Create an external mod](create-mod.md) | package + input graph | selected/annotated graph |
| [Import ONNX](import-onnx.md) | `.onnx` bytes | source then semantic `.jog` |
| [Emit C](emit-c.md) | semantic `.jog` | planned `.jog`, `.h`, `.c`, executable |

Every guide keeps stage boundaries named so a failure can be localized by
reading the last successful artifact.
