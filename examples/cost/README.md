# Module-defined measurement

This example assigns an arbitrary weight of four to calls and one to every
other operation. The policy is an ordinary Joggle function; neither the unit
nor the operator weights are built into the compiler.

From the repository root:

```sh
cmake -S . -B build
cmake --build build
./build/joggle query cost.total examples/cost/model.jog \
  -M examples -M build/modules
```

The result is `5`: one call plus one return. Replace `cost.weight` with a
device-specific cycle, energy, code-size, or resource model while retaining the
same traversal and typed invocation boundary.
