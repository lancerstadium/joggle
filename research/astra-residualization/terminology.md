# Locked terminology for the Astra-derived study

> Status: locked for D1–D3; rename only by changing every research artifact and implementation identifier together.

| Canonical term | Definition | Rejected/ambiguous variants |
|---|---|---|
| semantic graph | The fixed-semantics NN graph supplied to the optimizer. | model graph, graph IR, workload graph when used for the same object |
| implementation atom | A prevalidated local implementation/rewrite/schedule fragment that may participate in a realization. | recipe, kernel option, choice leaf |
| realization | One complete, executable whole-NN implementation state. | plan, variant, configuration when the whole state is meant |
| runtime fact | A versioned, read-only input to materialization, such as shape, available typed resources, policy, or resident generation. | context value, device property |
| resident state | The currently installed code/data/layout/schedule generation relevant to a transition. | cache state, old plan |
| admissible domain | Runtime facts and resident states for which the artifact contract is defined. | supported cases, valid inputs |
| context closure | The least set of decisions/actions invalidated by a fact or resident-state change under declared dependencies. | incremental slice, affected nodes |
| residual artifact | The immutable slow-plane output consumed by the fast plane. | capsule, bundle, package, guarded DAG |
| materializer | The bounded fast-plane evaluator that returns a realization delta or explicit fallback. | JIT, runtime optimizer, selector |
| realization delta | The staged write/event set that transforms one resident generation into another. | patch, update list |
| performance trial | Executing a candidate realization to obtain a selection signal. Forbidden in the fast plane. | profiling, autotuning |
| legality oracle | The exhaustive independent checker used only by diagnostics to decide compositional legality. | verifier when referring to the test oracle |
| performance oracle | The exhaustive minimum-cost legal realization used only by diagnostics. | optimal plan, Astra oracle |
| fallback | A declared, already legal resident or baseline realization returned when bounded materialization cannot succeed. | failure, default |
| step bound | A static upper bound on abstract materializer instructions, independent of host timing. | time bound, WCET |
| arena bound | A static upper bound on fast-plane scratch memory. | heap bound, memory limit |
| slow plane | Offline/cloud compilation, proof, profiling, and search. | cloud loop when no fleet behavior is required |
| fast plane | Edge-side fact snapshot, materialization, validation, and commit; no performance trial. | runtime JIT, online tuning |

Names deliberately not frozen yet: project/algorithm name, DSL name, and paper title. D1–D3 test a mechanism before branding it.
