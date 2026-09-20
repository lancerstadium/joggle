---
title: Write a mod
description: Build and load a small out-of-tree Joggle transformation.
---

# Write a mod

A `mod` is the package and extension boundary. A source-only package needs one
directory containing `module.jog`; it does not need registration code or a
generated interface.

## Create the package

Create `local-mods/choose_lut/module.jog`:

```jog
mod choose_lut
use ir

fn apply(m: Mod) -> bool {
  var changed = false
  for op in ir.ops(m) {
    if ir.callee(op) == "nn.relu" {
      changed = ir.set(m, op, "implementation", "lut") || changed
    }
  }
  return changed
}
```

The public `apply` function accepts the same `Mod` object used by computation,
analysis, conversion, and emission code. Helpers that should not enter the
package API use `local fn`.

## Check and invoke it

```sh
./build/joggle mod check choose_lut \
  -M build/modules -M local-mods

./build/joggle run choose_lut.apply model.jog \
  -M build/modules -M local-mods > selected.jog
```

Search roots are explicit and repeatable. Dependencies come from `use`
declarations; physical directory adjacency is not an implicit dependency.

## Keep one responsibility

A useful package may contain semantic definitions, analysis, selection policy,
conversion, and artifact logic when they implement one graph-level concern.
Split source across `lib/*.jog` only when the package grows; sorted fragments
and `module.jog` still form one `mod`.

Before sharing a package, verify that:

- `joggle mod info` exposes only intentional public functions;
- failure leaves the input unchanged and emits a useful diagnostic;
- metadata outside the package's responsibility is preserved;
- at least one test loads the package through an external `-M` root.

The repository's `extensions/ikj`, `extensions/edge`, `extensions/locality`,
and `extensions/compact` directories are executable examples. Run their gates
with `ctest --test-dir build -L extension --output-on-failure`.

For package installation and compatibility checks, continue with the
[mod system](../internals/modules.md). Next tutorial: [Import ONNX](import-onnx.md).
