---
title: Get started
description: Build Joggle, check a program, run a transformation, and inspect the result.
---

# Get started

This page takes one path through the system: build the default configuration,
check a real fixture, apply one transformation, and query the result. It needs
CMake 3.20 or newer and a C++20 compiler.

## 1. Build the default configuration

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The default build has no downloaded dependencies. Optional ONNX, TFLite, and
SAT support can be enabled later; they are not required for this walkthrough.

## 2. Check a program

Use the matrix-multiplication fixture already exercised by the test suite:

```sh
./build/joggle check test/data/matmul.jog -M build/modules
```

`-M` adds a module search root. `check` loads the dependency closure declared
by the input, verifies it, and prints its canonical form. A nonzero exit status
means parsing, resolution, or verification failed.

## 3. Run a transformation

```sh
./build/joggle run opt.fold_add_zero test/data/matmul.jog \
  -M build/modules > transformed.jog
```

`run` names an ordinary typed function and applies it transactionally. The
output is committed only if the whole invocation succeeds and verifies.

## 4. Query without editing

```sh
./build/joggle query opt.untyped transformed.jog -M build/modules
```

`query` returns a canonical attribute value instead of rewritten IR. Here the
result lists calls whose outputs still have the open `_` type.

## 5. Inspect the package boundary

```sh
./build/joggle mod list -M build/modules
./build/joggle mod info opt -M build/modules
./build/joggle mod check opt -M build/modules
```

These commands use the same module discovery and verification rules as
`check`, `run`, `query`, and `emit`.

## Continue in order

1. [Transform a model](../tutorials/transform.md) explains transactions, composition, and
   structured reports.
2. [Write a mod](../tutorials/write-mod.md) creates an out-of-tree extension with one
   responsibility.
3. [Import ONNX](../tutorials/import-onnx.md) makes frontend decoding and semantic
   conversion explicit.
4. [Emit C](../tutorials/emit-c.md) prepares an artifact, plans storage, and compiles it.

For lookup rather than a walkthrough, use the
[language reference](../reference/language.md) or
[bundled mod catalogue](../reference/module-catalogue.md).
