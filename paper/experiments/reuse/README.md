# Reuse: deriving a storage planner

The primary derivation case. A copy of the installed `mem.plan` storage planner
has one internal ranking rule replaced, and the copy runs on the same prepared
UltraFace program as the original.

## What is here

| File | Role |
| --- | --- |
| `module.jog` | the `reuse` module: the derivation recipe |
| `compiler.jog` | the derived compiler definition the recipe produces |
| `control.json` | the source-patch control: the same rule as a one-hunk patch, plus the refactoring cases and their outcomes |
| `result.json` | the correctness check on UltraFace RFB-320: two calls per variant, absolute tolerance `1e-5`, identical outputs and weights |
| `check.py`, `check.cmake` | the drivers; `check.cmake` is the `derive-memory` CTest test |
| `compare.py` | the variant comparison |
| `external/` | the TVM natural-route control, including its export and bench records |

## Records and boundaries

The measurements are not here. The derivation's artifact consequence, the
source-patch control, the refactoring outcomes, and the external route are
written up in [`derive-prepare.md`](../derive-prepare.md), and the refactor
survival matrix is Table 2 of the manuscript. `control.json` is the raw record
behind that table; the numbers quoted in the paper come from it.

The result is correctness-only. The storage reduction follows from the ranking
policy, not from structural derivation, because the one-hunk patch reaches the
same plan; what derivation adds is that the recipe survives refactorings that
stop the patch from applying. No latency or memory claim is made here.
