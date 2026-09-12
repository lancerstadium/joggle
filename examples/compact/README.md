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

The module changes neither a frontend nor an artifact emitter. Its purpose is
to expose a real resource tradeoff through an ordinary distributable module:
on the recorded GoogLeNet pilot it reduces planned workspace substantially but
increases latency. `compact` is therefore an explicit implementation choice,
not a default optimization or a universal performance claim.
