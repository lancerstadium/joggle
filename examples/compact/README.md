# Compact convolution

This source-only module provides one alternative body for the biased
`nn.conv2d` signature. It accumulates convolution and applies bias and the
requested activation in one function and one result buffer. Calls that do not
match this signature remain unchanged, so the module composes with the shared
`nn` semantics or another implementation family.

```sh
joggle run compact.apply semantic.jog \
  -M examples -M build/modules > selected.jog
```

Passing a configuration dictionary combines `compact` with `spatial` and
makes the decision independently for each call. The following selects
`compact` only when the staged body's two
full-size intermediate results would exceed 262,144 scalar elements:

```sh
joggle run compact.apply semantic.jog \
  --arg '{max_extra_elems: 262144}' \
  -M examples -M build/modules > selected.jog
```

`select` is an ordinary read-only
`fn(Mod, Op, list<Fn>, dict) -> list<Fn>`. It inspects the inferred result
shape and tagged candidates; `opt.instantiate` verifies that it returns at
most one member and that it does not mutate the program. A study can replace
this policy without changing either implementation or the compiler core. The
compiler does not know the budget key, its unit, either implementation name,
or convolution as a special case. Unbiased calls have only the spatial
candidate and remain spatial.

The module changes neither a frontend nor an artifact emitter. Its purpose is
to expose a real resource tradeoff through an ordinary distributable module:
on the recorded GoogLeNet pilot it reduces planned workspace substantially but
increases latency. `compact` is therefore an explicit implementation choice,
not a default optimization or a universal performance claim.
