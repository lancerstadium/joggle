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

The example policy does not recognize convolution or require seven axes. It
uses `tile.state_axes` and `tile.reduction_axes` to keep the last two affine
state axes inside the reduction band. Correctness belongs to `tile.reorder`.
Before editing, the transform requires static integer ranges, one
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

The same exposed Conv body can also be improved without an implementation
override. Here each requested pass changes that body in place: range facts
remove proved guards, scalar promotion changes the reduction's traffic, and
ordinary cleanup removes values made dead by those edits.

```sh
joggle run bounds.fold opt.fold opt.basic tile.scalarize opt.basic \
  canonical.jog -M build/modules > improved.jog
```

For an output-stationary reduction this moves the output access outside the
reduction band. The resulting source has an outer `n, m, oh, ow` loop, one
`acc` load, an inner `q, r, s` loop, and one final store. The bundled test also
applies it to an unrelated five-axis reduction, checks idempotence, emits strict
C99, and executes both paths against the same numerical harness. Proven affine
input, weight, and output indices are rebuilt directly from loop axes, so the
expanded `xi`/`wi` stride-update chains do not survive the rewrite. This is the
optimization path for an inspectable Conv definition. The `edge` example is a
separate ABI escape hatch for hardware or library kernels that genuinely are
external implementations; it is not used to optimize this Conv body.
