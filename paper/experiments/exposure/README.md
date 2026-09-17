# Exposure: phase boundary probe

A twelve-line probe, not a study. `module.jog` implements `phases(m)`, which
executes the first `opt.expose`, then `ir.type`, then the second `opt.expose`
from the private `prepare_with` body in `c`, and returns the second call's
`bool`. It answers whether the second exposure step changes these inputs, which
a revision-equality observation on three architectures could not settle.

The module's SHA-256 is
`8b94d812472a37e83b75bd13defb95e199264e9c490be378e04782255673b58a`.

## Records and boundaries

This directory deliberately holds only the probe. Its purpose, the exact
reproduction commands, and the per-model readings are in
[`derive-prepare.md`](../derive-prepare.md) under "Phase boundary probe", which
is the authoritative record; duplicating them here would create a second copy to
keep in step.

The probe reports structural revision growth and which `opt.expose` steps
changed. That is a boundary observation about how many independent optimization
actions a revision increment represents, not a speed result.
