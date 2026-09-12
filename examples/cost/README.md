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

## A transformation policy

The same module also supplies `cost.profitable(Mod, list<Op>, dict) -> bool`
to the generic `tile.fuse` traversal. It accepts a legal producer/consumer pair
only when the producer has a statically visible iteration extent and both its
extent and recursive call count fit caller-provided budgets:

```sh
./build/joggle run cost.fuse prepared.jog \
  --arg 65536 --arg 100 -M examples -M build/modules > selected.jog
```

This is intentionally a policy example, not a claim that those two features
predict every target. It contains no operation names and does not change
`tile`, the IR, or an emitter. A hardware experiment can replace the body with
its own register, memory-traffic, cycle, or resource model while retaining the
same typed pair and transactional rewrite boundary. In particular, the
MobileNetV2 pilot shows why legality alone is insufficient: fusing every legal
BatchNorm-ReLU pair reduced IR size but regressed Clang latency after changing
the vectorizer's interleave choice.
