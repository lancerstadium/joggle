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

The same callback can receive structural policy instead of requiring one
wrapper per device configuration:

```sh
./build/joggle query cost.total examples/cost/model.jog \
  --arg 7 --arg 2 -M examples -M build/modules
```

This returns `9`: the call weight is seven and the return weight is two. The
configuration is forwarded by `stat.sum`; it is not stored in the model or
interpreted by the compiler core.
