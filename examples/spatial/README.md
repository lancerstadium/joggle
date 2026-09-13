# Spatial loop scheduling

`spatial` is a pass, not a convolution implementation. The shared `nn` module
owns the only semantic `conv2d` body. Target preparation exposes that body as
ordinary `Fn`/`Blk`/`Op` IR; `spatial.apply` then changes its seven-axis loop
from output-stationary order

```text
n, m, oh, ow, q, r, s
```

to

```text
n, m, q, r, s, oh, ow
```

with the generic `tile.reorder` transform. There is no `spatial.nn.conv2d`
overload, implementation tag, frontend rule, or emitter case.

```sh
joggle run c.prepare mem.plan semantic.jog \
  -M build/modules > canonical.jog
joggle run spatial.apply canonical.jog \
  -M examples -M build/modules > scheduled.jog
joggle emit c.source scheduled.jog \
  -M build/modules > model.c
```

The example policy selects compatible seven-axis loops, but correctness belongs
to `tile.reorder`. Before editing, it requires static integer ranges, one
carried state, equal affine read/write addresses, an injective address map for
state axes, and unchanged relative order within both state and reduction axes.
Moving reduction axes across independent output axes is therefore accepted;
permuting the reduction itself is rejected. The same mechanism is available to
pooling, tensor programs, or a user policy without naming an NN operator.

This separation is intentional: `nn` defines computation, `tile` proves and
performs a structural rewrite, and `spatial` contains only a replaceable
profitability policy. An external or packed kernel may still use implementation
selection when it truly changes the available computation, but loop scheduling
does not need a second function body.
