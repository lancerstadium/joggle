---
title: Design context
description: Understand Joggle's design choices in relation to adjacent compiler systems.
---

# Design context

This section explains why Joggle chooses one typed language, graph-scoped mods,
and dependency-aware execution. It describes implemented architecture and
separates it from quantitative comparison.

## Read this section when

- deciding whether Joggle fits a compiler project;
- mapping concepts from MLIR, TVM, IREE, ONNX Runtime, Halide, or e-graphs;
- reviewing which differences are architectural and which still require
  experimental evidence;
- deciding where a new feature belongs without copying a neighboring system's
  abstraction mechanically.

Start with [Related systems and design boundaries](related-systems.md).

> [!IMPORTANT]
> The comparison uses public papers and official documentation. It records the
> documented abstraction a developer meets first, not every mechanism a system
> could support.
