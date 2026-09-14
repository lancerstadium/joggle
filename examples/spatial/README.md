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
n, m, oh, q, r, s, ow
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

Before changing the program, the same policy is inspectable as ordinary
compile-time data:

```sh
joggle query spatial.plan canonical.jog \
  -M examples -M build/modules > plan.attr
```

The report lists each candidate's function, current and selected axis names,
per-axis locality scores, and the read/write affine forms that produced the
decision. It is read-only and serializable; there is no schedule object to keep
in sync with the function IR.

Affine recognition uses the loop's static half-open ranges as proof facts. It
can therefore reduce a grouped coordinate such as `m / 160` to a constant when
`m` ranges over `0..160`, then continue through the surrounding address
expression. This matters for pointwise contractions but is not an NN rule: the
same proof applies to layout groups or tiles written by an external module.

The example policy does not recognize convolution or require seven axes. It
uses `tile.state_axes`, `tile.reduction_axes`, and the whole-loop
`tile.read_forms`/`tile.write_forms` queries. It scores unit-stride access
higher than reuse and moves the best suffix of state axes inside the unchanged
reduction band. The score is deliberately visible source policy, not a hidden
target heuristic. Correctness belongs to `tile.reorder`.
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

The budgeted scalar form covers a small output tile rather than one output at
a time. A user policy can split a state axis, use `tile.reorder` to place the
inner state axis below the reduction band, and call
`tile.scalarize(m, loop, factor)`. The pass carries at most `factor` scalar
accumulators and proves every hoisted state address against the static tensor
capacity. The regression suite executes both a two-lane Conv pipeline and a
locality-selected two-lane spatial pipeline; a padded three-lane split is
rejected by affine legality instead of being turned into an out-of-bounds load
or store. None of these paths defines a second `nn.conv2d` function.

`spatial.block(m, factors)` is the complete generic policy used by that test.
For each loop it prefers the first factor that exactly divides the innermost
proved state extent. If none does, it uses the first smaller factor and
`tile.peel` separates an aligned prefix from an ordinary scalar tail. `[4, 7]`,
for example, covers both power-of-two widths, width seven, and odd extents such
as 111 without a masked access or target-emitter case. Dynamic and too-small
extents are left unchanged. The policy splits the selected prefix, moves only
the new inner axis below the reduction band, and scalarizes within the selected
hard budget. It neither inspects a callee name nor assumes a loop rank. Each
explicit edit returns its replacement `Op`, so the three edits compose
directly without an anchor, result wrapper, or module rescan:

```sh
joggle run spatial.block canonical.jog --arg 2 \
  -M examples -M build/modules > blocked.jog
joggle run spatial.block canonical.jog --arg '[4, 7]' \
  -M examples -M build/modules > blocked-mixed.jog
joggle run bounds.fold opt.fold opt.basic blocked.jog \
  -M build/modules > blocked-clean.jog
```

An optional final argument limits structural duplication across the complete
invocation. Before editing a loop, the policy multiplies the selected lane
count by `tile.scalar_cost(m, loop)`. If that amount would exceed the remaining
limit, it leaves the loop untouched; it does not partially split or reorder
the rejected candidate.

```sh
joggle run spatial.block canonical.jog --arg '[4, 7]' --arg 4000 \
  -M examples -M build/modules > bounded-block.jog
```

The cost is a backend-independent proxy: recursively nested, non-terminator
source operations duplicated per scalar lane. It is useful for a small source
policy, but it is not a prediction of C bytes, instructions, or latency.
