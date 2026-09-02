# F2-0 smoke result: branch placement–schedule

> Status: legality/transition smoke test; D1 strong-baseline collision pending
> Date: 2026-09-02
> Fixture: `fixtures/f2-branch-schedule.md`

## Exhaustive scope

- 64 runtime-fact states over shape, tensor/copy capability, vector-lane capacity, fine events, and objective.
- 12 raw whole-graph assignments per state; 768 assignment/fact evaluations.
- Eight semantic-distinct oracle winners.
- 4,032 directed non-identity state transitions; 384 one-fact transitions.
- A direct no-trial winner-table baseline: 508 canonical bytes, two abstract selection steps, one arena byte.

Every table result matched the independent exhaustive oracle and was legal. Validation rejection and interruption at every staged write retained the old generation; successful publication advanced exactly one generation.

## Transition result

| Metric | F2-0 | Preregistered D2 target |
|---|---:|---:|
| Median staged-addition/full ratio | 0.50 | ≤0.40 |
| P95 ratio | 0.75 | ≤0.70 |
| Maximum ratio | 0.75 | full rematerialization must be explicit |
| All transitions avoiding full staging | 100% | descriptive |
| One-fact median ratio | 0.25 | descriptive |
| One-fact P95 ratio | 0.50 | descriptive |
| One-fact transitions avoiding full staging | 100% | ≥90% |

## Interpretation

F2 confirms that branch structure improves mutation locality: unlike F1, no transition stages the complete realization, and all one-fact changes preserve at least one atom. It nevertheless misses the preregistered overall median and P95 thresholds.

This result is **not** evidence for Astra-style context closure. The direct table selects the complete new winner in two steps and then computes a set difference; it carries no dependency slice and does not avoid decision reevaluation. Delta staging and atomic update are established mechanisms in prior work. A context-closure contribution survives only if a larger factorized graph shows reduced reevaluation *and* reduced staged bytes/latency relative to this full-selection-plus-diff baseline.

## Next checks

1. Compile F2 into conditioned variants and optimal-order MTBDD; the 508-byte direct table is not yet a strong D1 baseline result.
2. Add explicit dependency metadata and compare affected-decision evaluation with full winner selection under identical artifacts.
3. Replace equal-size atom counting with canonical staged payload bytes before any D2 gate decision.
4. If the combined P95/metadata/wall-clock gates still fail, demote context closure to an implementation optimization and remove it from the paper contribution set.
