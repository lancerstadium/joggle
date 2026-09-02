# F1-0 smoke result: layout–fusion chain

> Status: mechanism smoke test, not a D1/D2 gate decision
> Date: 2026-09-02
> Fixture: `fixtures/f1-layout-fusion.md`
> Scope: 32 exhaustive runtime-fact points, 36 raw assignments per point, 992 non-identity state transitions

## Reproduction

```sh
cmake -S research/astra-residualization/harness -B /tmp/jrd-build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/jrd-build --parallel
ctest --test-dir /tmp/jrd-build --output-on-failure
/tmp/jrd-build/residual_oracle --baselines
/tmp/jrd-build/residual_oracle --conditioned
/tmp/jrd-build/residual_oracle --exploration
/tmp/jrd-build/residual_oracle --materializer
/tmp/jrd-build/residual_oracle --mtbdd
/tmp/jrd-build/residual_oracle --transitions
```

The canonical serializer includes the fact schema, referenced atom definitions, path descriptors, fallback, and selector payload. It carries cost tables and interaction/transition evidence only for mechanisms that evaluate costs in the fast plane.

## D1 representation collision

| Mechanism | Artifact bytes | Optimized coverage | Regret | Declared max steps | Fast-plane trials |
|---|---:|---:|---:|---:|---:|
| B0 static one-best | 127 | 8/32 | mean 4.60%, P95 37.5%, max 44% | direct return | 0 |
| B1 exact top-K, K=2 | 273 | 32/32 | mean 3.70%, P95 37.5%, max 44% | not yet instrumented | 0 |
| B1 exact top-K, K=3 | 348 | 32/32 | 0 | equivalent dynamic evaluator | 0 |
| B2 conditioned variants | 173 | 32/32 | 0 | 21 | 0 |
| B3 Astra-style exhaustive exploration | 158 | 32/32 | 0 after convergence | not eligible | 1–3, mean 1.75 |
| B4 dynamic-plan reference materializer | 348 | 32/32 | 0 | 26 | 0 |
| B5 FCC | not separately serialized yet | — | — | — | 0 |
| B6 optimal-order reduced MTBDD | 212 | 32/32 | 0 | 9 | 0 |
| B7 AND–OR memo | pending | — | — | — | — |

### Immediate interpretation

- The current B4/reference materializer is strictly dominated by B2 conditioned variants on both bytes (`348 → 173`) and the declared step bound (`26 → 21`) while preserving zero regret and zero trials.
- B6 MTBDD spends 39 more bytes than B2 but reduces the declared path bound from 21 to 9 steps.
- B3 is smallest because it does not carry performance evidence; it pays 1–3 actual candidate executions per unseen context. This is exactly the edge behavior the proposed system forbids.
- B1 K=3 and B4 have the same byte count because, after honest accounting, exact runtime selection among three complete versions requires the same cost/applicability evidence as the current dynamic-plan materializer.

**Decision for this representation only: KILL.** A factorized guarded/dynamic-plan artifact is not a representation contribution on F1-0. This does not kill the no-trial bounded *system* hypothesis; it removes one invalid way of packaging it.

## D2 transition smoke

| Metric | Result | Preregistered F1–F3 value target |
|---|---:|---:|
| Transitions | 992 | — |
| Median staged-addition/full ratio | 0 | ≤0.40 |
| P95 ratio | 1.0 | ≤0.70 |
| Maximum ratio | 1.0 | explicit full-rematerialization path allowed |
| All transitions avoiding full staging | 55.65% | descriptive |
| One-fact transitions | 160 | — |
| One-fact median ratio | 0 | descriptive |
| One-fact P95 ratio | 1.0 | descriptive |
| One-fact transitions avoiding full staging | 75% | ≥90% |
| Declared transition step/arena bounds | 40 steps / 21 bytes | never exceeded |

All validation-rejection and interruption points retained the old generation; successful transitions published exactly one new generation. The staged addition set matched the independent set-difference oracle for every transition.

### Immediate interpretation

F1-0 fails the D2 value thresholds: layout-family changes replace all four atoms, so P95 closure is 100%, and only 75% of one-fact changes avoid full staging. This is a useful boundary, not a correctness failure. Context closure cannot be a broad contribution on a mostly serial graph; F2 must show branch-local preservation under placement/schedule changes, or D2 is demoted to an implementation optimization.

## What this result does not establish

- No cold-start wall-clock or real target measurement exists yet.
- F1-0 is a hand-checkable mechanism fixture, not evidence of whole-model performance.
- FCC and AND–OR baselines are not yet independently implemented; the representation audit is incomplete.
- No candidate mechanism survives yet. The next legitimate step is F2 branch placement–schedule plus B5/B7, not compiler integration or paper drafting.
