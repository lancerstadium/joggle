# D1–D3 preregistration: no-trial bounded whole-NN residual optimization

> Status: frozen experimental contract before implementation
> Date: 2026-09-02
> Goal: decide whether the Astra-derived systems hypothesis deserves a compiler implementation and ASPLOS/TACO claim
> Non-goal: create a public benchmark suite, compare neural architectures, or optimize SNN/popcount workloads
> Terminology: normative definitions are in `terminology.md`
> Evidence basis: audited full-paper cards for Astra, TASO, Rammer, Collage, plus the collision notes for IREE, dynamic query plans, FCC, MDD, AND–OR/e-graphs, APOLLO, PCC/translation validation, live update, and FPGA patching

## 1. Decision under test

The study tests one indivisible systems hypothesis:

> For fixed-semantics NN inference with dynamic edge facts and low request repetition, a slow plane can residualize a whole-graph optimizer into a no-trial fast plane that (i) never emits an illegal realization, (ii) has static step and arena bounds, and (iii) provides cold-start or state-change value that equally sized static one-best/top-K/conditioned-variant artifacts cannot provide.

This is not a claim that guarded choice representations, shared plan DAGs, certificates, translation validation, or atomic update are new. Those representation-level claims are already killed by prior art.

The study must end in one of three decisions:

- **FREEZE**: all hard gates pass, an effect survives the strongest baseline under equal artifact and evidence budgets, and the result remains useful on at least one real edge target.
- **REVISE**: safety/bounds pass, but the value or compactness mechanism is narrower than hypothesized; restate the problem and rerun only the affected diagnostic.
- **KILL**: any hard correctness/bound gate fails, or the candidate is equivalent to an equally compact existing mechanism without system-level benefit.

No compiler API, DSL syntax, class hierarchy, or paper prose is frozen before this decision.

## 2. Formal experimental object

For a semantic graph `G`, target contract `T`, runtime facts `x`, and resident state `s`:

- `A(G,T)` is a finite set of prevalidated implementation atoms.
- `R(G,T)` is the set of complete realizations constructed from those atoms.
- `Legal(r,x,s)` checks interface, layout, typed-resource, ordering/event, residency, and transition constraints.
- `Cost(r,x)` is diagnostic ground truth: an exact synthetic integer model in micro-instances, and measured latency/energy evidence in real-target instances.
- `P` is an immutable residual artifact.
- `M(P,x,s)` returns `(r, delta, validation_input)` or `fallback(reason)`.

For every successful result, the harness must independently verify:

1. `r ∈ R(G,T)`;
2. `Legal(r,x,s)`;
3. `delta` is sufficient to transform `s` into `r` and does not write outside its declared set;
4. abstract instructions `≤ B_steps(P)`;
5. scratch bytes `≤ B_arena(P)`;
6. repeated evaluation of identical `(P,x,s)` returns the same canonical result;
7. the fast-plane trial count is exactly zero.

Fallback is legal only when it names a prevalidated resident/baseline realization and leaves the current generation unchanged. A fallback is not counted as successful optimized coverage.

## 3. Scope: ordinary NN inference, not a synthetic neural benchmark

The diagnostic harness uses small but whole-graph fixtures derived from ordinary inference motifs. They exist to expose compiler mechanisms, not to rank models.

### F1. Layout–fusion chain

`conv/matmul → bias/normalization → activation` with an optional residual join. Atoms vary backend implementation, fusion boundary, input/output layout, conversion placement, and scratch lifetime.

This family collides with TASO and Collage. It tests whether local valid choices compose into a legal whole graph and whether additive cost assumptions remain closed.

### F2. Branch placement–schedule

Two or more branches containing convolution/matmul/elementwise operations join later. Atoms vary backend placement, task granularity, typed execution resources, DMA/event ordering, and join barrier.

This family collides with Rammer and Astra. It tests local-fastest versus resource-efficient choices and cross-dispatch dependencies.

### F3. Resident-state transition

A semantic graph has code/data atoms that may already be resident. Runtime facts change shape class, available typed resources, policy, or current generation. Materialization may preserve unaffected atoms and stage only a realization delta at a declared quiescence boundary.

This family tests context closure and cold state changes. It is not an FPGA or RISC-V special case; those can later instantiate the same typed resource/event contract.

### F4. Adversarial non-factorable controls

Generated instances deliberately add dense high-order interactions, discontinuous costs, stale evidence, and incompatible resident states. They establish where residualization should fallback or lose to top-K/full search.

This family prevents selecting only friendly factorized cases.

## 4. Runtime facts and target contracts

The core does not contain fixed fields such as GPU streams, FPGA LUT capacity, RISC-V ISA extensions, or a universal memory-capacity struct. Each fixture declares typed facts and relations.

Minimum fact categories used across the study:

- semantic input class: tensor shape/rank and batch class;
- capability availability: which typed execution/copy/storage resources are currently admissible;
- policy: latency/energy/memory priority and hard deadline class;
- resource envelope: per-kind limits supplied by the fixture module;
- evidence version: target/library/driver/calibration identity;
- resident identity: installed realization generation and resident atoms;
- transition permission: declared quiescence/commit event.

The legality oracle treats runtime facts as data. Candidate code may not switch on hard-coded device names.

## 5. Independent exhaustive oracles

The oracle implementation must be structurally separate from the candidate residualizer. It may share parsed fixture data types, but not candidate pruning, factoring, memoization, or evaluator code.

### 5.1 Legality oracle

For each bounded instance, enumerate every atom assignment, form each complete realization, and check:

- graph interface/type/shape agreement;
- layout compatibility and required conversions;
- capability predicates;
- typed resource limits and mutually exclusive use;
- data/event happens-before constraints;
- resident compatibility and transition preconditions;
- fallback identity and no-write behavior.

The oracle emits the complete legal set and a rejection-reason vector for every illegal assignment.

### 5.2 Performance oracle

For each `(x,s)`, select the minimum-cost legal realization under the frozen cost evidence. Ties are broken by a canonical lexicographic realization encoding. The oracle reports the full Pareto set when latency, energy, and artifact mutation bytes are simultaneous objectives.

The oracle is not available to the fast plane. It exists only to calculate regret and verify selections.

### 5.3 Context-closure oracle

Given `(x_old,s_old) → x_new`, enumerate the minimum changed decision/action set that can reach the canonical new realization without violating intermediate event/resource constraints. This establishes a lower bound on necessary write set and event count.

### 5.4 Bound oracle

Interpret the residual artifact with a deliberately simple reference semantics that counts every predicate read, branch, atom emission, validation record, and delta write. Separately track peak scratch objects and bytes. Wall-clock time is never substituted for this abstract bound.

## 6. Baselines and budget fairness

Every encoding receives the same legal atoms, runtime facts, offline evidence, canonical serialization accounting, and semantic graph. Baselines may not access runtime measurements unavailable to the candidate.

### B0. Static one-best

One realization minimizing expected cloud cost over the training fact distribution, with explicit fallback when illegal.

### B1. Equal-byte flat top-K

Store as many complete realizations and fact selectors as fit the candidate artifact byte budget. Select K by optimal offline weighted set cover over the training domain, not a greedy weak baseline.

### B2. IREE-style conditioned variants

A mechanism emulator for ordered conditioned executable variants, runtime constants, and fallback. It is not reported as an IREE performance reimplementation; it isolates the published conditioned-variant mechanism under the same atoms and bytes.

### B3. Astra-style adaptive choice oracle

An upper-cost baseline allowed to execute/measure candidates online with context-indexed reuse and hierarchical/parallel/prefix structure. It reports trial count and time-to-best. It is not eligible for the no-trial gate but bounds the performance lost by exploration elimination.

### B4. Dynamic-plan choose DAG

Local choose nodes with shared subplans and state-dependent branch costs, following dynamic query plan semantics. It receives the same legality predicates and cannot run performance trials.

### B5. Formula Choice Calculus

Shared artifact with Boolean guards and whole projection. Report normalized serialized nodes/bytes and evaluation steps.

### B6. Reduced ordered weighted MDD

A reduced ordered multi-valued decision diagram representing constraints and accumulated costs. Variable order receives the best result from a fixed offline order-search budget declared before each experiment.

### B7. AND–OR memo space

Shared alternative/conjunction nodes with cost extraction. It must include the same context and whole-realization compatibility constraints.

### B8. Full rematerialization

For D2 only: evaluate the same candidate artifact from an empty resident state, stage the full realization, and commit. This isolates the value of context closure from the representation itself.

### Budget rules

- **Artifact budget**: count canonical serialized bytes, excluding generic interpreter/runtime code shared by all instances. Report runtime code size separately.
- **Offline evidence budget**: same measured points/traces and same allowable solver time for all no-trial methods.
- **Fast-plane information**: only the frozen fact snapshot and resident identity.
- **Search budget**: order optimization, top-K selection, and candidate factoring receive equal wall-time and memory ceilings; timeouts are results, not silently enlarged budgets.
- **No hidden training**: no learned selector unless introduced as an explicit additional baseline with identical training data.

## 7. D1 — exploration-elimination gap

### Question

Can the residual artifact preserve useful whole-NN state-dependent decisions without executing candidates at the edge?

### Inputs

- F1–F4 instances at exhaustive sizes: 4–14 choice sites, 2–5 alternatives/site, 2–8 runtime-fact dimensions.
- Three constraint densities: sparse/local, structured cross-layer, and dense/adversarial.
- A frozen train domain of fact points for slow-plane evidence and a disjoint test domain of admissible fact combinations. Test points may be new combinations of known fact values; no unsupported capability value is smuggled in.
- At least 20 deterministic seeds per generated cell; seeds are committed before aggregate results are inspected.

### Outputs and metrics

- illegal-success count and reason;
- optimized coverage and fallback rate;
- cost regret `(selected - oracle) / oracle`, with median/P95/max;
- artifact bytes and shared runtime bytes;
- abstract steps and peak arena bytes, both predicted and observed;
- fast-plane candidate executions/trials;
- cold-start selection/materialization wall time, reported separately from abstract steps;
- break-even request count versus B3 online exploration.

### Hard gates

1. Zero successful illegal realizations across every exhaustive and generated test point.
2. Zero fast-plane performance trials.
3. Observed steps and arena never exceed artifact-declared bounds.
4. Deterministic canonical result for repeated identical inputs.
5. Every fallback leaves the resident generation unchanged.

### Value gate

D1 supports the systems hypothesis only if, under equal artifact bytes:

- optimized coverage is at least 95% on F1–F3 test domains;
- P95 regret is at most 10% and max regret at most 25% on successful non-fallback cases;
- compared with the best of B1/B2/B4–B7, either P95 regret is at least 2 percentage points lower at matched coverage, or optimized coverage is at least 10 percentage points higher at matched P95 regret;
- fast-plane cold-start is lower than the time for one B3 candidate trial, and the break-even request count versus B3 is at most 32 for at least two of F1–F3.

These thresholds are intentionally system-level, not a claim of universal optimality. Failure on F4 is expected; silent illegality is never acceptable.

### Decision

- Hard-gate failure: **KILL**.
- Hard gates pass but no best-baseline effect: **REVISE** to a safety/boundedness tool; do not claim exploration elimination value.
- Value passes only on one friendly family: **REVISE** to that named family and drop whole-NN generality.

## 8. D2 — context-closure value

### Question

Does Astra-derived context closure materially reduce state-change work while preserving exactly the same legal realization as full rematerialization?

### Inputs

- F1–F3 transition traces with one-fact, correlated multi-fact, and adversarial changes.
- Transition classes: shape change, capability loss/recovery, policy change, evidence-version invalidation, resident generation mismatch.
- At least 1,000 transitions per fixture/scale cell or exhaustive transitions when the domain is smaller.

### Outputs and metrics

- realization equivalence to B8 full rematerialization;
- changed decision count, staged bytes, delta writes, and event count;
- closure size divided by full realization size;
- abstract steps, arena, P50/P95/max wall time;
- stale-decision false reuse and unnecessary invalidation counts;
- commit/fallback outcome under injected validation failure and interrupted staging.

### Hard gates

1. Incremental and full materialization produce the same canonical realization, or both return the same declared fallback reason.
2. No stale invalid decision survives the closure.
3. Interrupted/failed staging never changes the published resident generation.
4. Transition-specific step/arena/write bounds are never exceeded.

### Value gate

D2 supports a context-closure contribution only if, over non-adversarial F1–F3 transitions:

- median closure touches at most 40% of full-realization atoms and staged bytes;
- P95 closure touches at most 70%;
- P95 transition wall time improves by at least 1.5× over B8, while maximum latency does not regress;
- at least 90% of one-fact changes avoid rewriting every unaffected branch;
- added dependency metadata is at most 25% of the residual artifact bytes.

### Decision

- Any hard-gate failure: **KILL incremental materialization** and retain only full bounded materialization if D1 survives.
- Correct but below value threshold: **REVISE**; context closure is an implementation optimization, not a paper contribution.
- Dense F4 closure approaching 100% is acceptable only if the artifact detects it and chooses full materialization without exceeding bounds.

## 9. D3 — bound-versus-compactness tension

### Question

Do static step/arena bounds force the residual artifact to expand until it becomes no better than concrete variants or established shared-plan encodings?

### Inputs

Sweep independently:

- choice sites: 4, 6, 8, 10, 12, 14, then larger non-exhaustive sizes;
- alternatives/site: 2–5;
- runtime-fact dimensions: 2, 4, 6, 8;
- compatibility-constraint density: 0.05, 0.15, 0.30, 0.60;
- interaction order: unary, pairwise, and selected 3/4-way constraints;
- semantic-distinct winners: controlled from 1 to the maximum legal count;
- resident overlap: 0%, 25%, 50%, 75%, 100%.

### Outputs and metrics

- canonical artifact bytes and construction time;
- number of legal complete realizations and semantic-distinct winners represented;
- bytes per winner and bytes per covered fact point;
- declared and observed max steps/arena;
- evaluator branch count, predicate reads, and delta writes;
- compile-time timeout/memory failures;
- Pareto front across bytes, max steps, arena, coverage, and regret.

### Hard gates

The candidate must maintain exact bound accounting and zero illegal outputs throughout the sweep. A timeout may yield a declared fallback artifact; it may not emit an unchecked artifact.

### Compactness gate

D3 supports a representation/system contribution only if:

- for at least two of F1–F3 and across at least three consecutive scales, the candidate lies on the global Pareto front formed with B1/B2/B4–B7;
- at matched P95 regret and coverage, it uses at most 75% of the bytes of the best baseline **or** at matched bytes it supports at least 2× as many semantic-distinct winners;
- its declared max steps are no more than 1.5× the best baseline at that matched point and its arena is no more than 2×;
- gains remain after counting dependency metadata, certificates, fallback entries, and canonical serialization headers.

### Decision

- If B4–B7 match or dominate the candidate after honest accounting: **KILL representation novelty**; retain only a system integration claim if D1/D2 real-target results are independently strong.
- If strict bounds require near-flat expansion: **REVISE** to bounded top-K/conditioned variants rather than inventing a new structure.
- If gains occur only through a better variable-order heuristic also applicable to MDD/FCC: contribute the heuristic only and rerun it inside those baselines.

## 10. Real-target checkpoint before FREEZE

Micro-diagnostics can kill the idea but cannot establish ASPLOS/TACO value. FREEZE additionally requires at least one ordinary NN inference stack on two genuinely different constrained targets or two materially different target modules.

Minimum workloads:

- one convolutional/residual model;
- one transformer/attention model;
- one small branch/irregular model;
- at least two dynamic fact categories per workload besides input shape.

Minimum real-target evidence:

- zero illegal realizations under conformance and failure injection;
- measured P50/P95/max cold-start and transition latency;
- measured peak memory and artifact bytes;
- end-to-end inference latency/energy regret against B3 after convergence;
- break-even request count and state-churn sensitivity;
- baseline compiler/version/driver identities and all offline profiling cost.

If only a simulator is available, the result remains **REVISE**, not FREEZE.

## 11. Failure injection

Every materializer version is tested with:

- corrupted/truncated artifact;
- unknown module/type/resource/effect identifier;
- stale evidence or target identity;
- unsupported runtime-fact value;
- resident generation mismatch;
- insufficient declared resource;
- validator rejection;
- timeout at every abstract instruction boundary;
- interruption before and after staging but before publish;
- duplicate/reordered events;
- adversarial fact snapshot change during evaluation.

Required outcome: explicit bounded fallback or successful atomic publish. Partial new generation visibility is a hard failure.

## 12. Reproducibility contract

- Fixtures, generators, seeds, canonical serializer, oracle outputs, and all raw per-point results are versioned.
- Every result row records graph hash, target-contract hash, fact/resident hash, artifact hash, baseline, offline budget, declared bounds, observed counters, output realization hash, and oracle comparison.
- Aggregate scripts read raw rows; no hand-edited summary values.
- Exact synthetic costs are integers. Real measurements retain all repeats and report clock/power/thermal controls.
- A baseline timeout, candidate timeout, fallback, illegal output, and parser failure are distinct outcomes.
- Mechanism emulators are named as such and are not presented as full reimplementations of IREE, Astra, or database systems.

## 13. Implementation boundary after preregistration

The first harness is deliberately small and independent of the current Joggle compiler:

1. a declarative fixture reader;
2. an exhaustive legality/performance/context-closure oracle;
3. canonical realization and artifact accounting;
4. baseline mechanism emulators;
5. one candidate residualizer and bounded reference interpreter;
6. raw-result writer and deterministic aggregation.

It must not add general compiler classes, a device hierarchy, a new DSL, backend code generation, ONNX import, or pass infrastructure. If D1–D3 survive, the proven objects—not speculative class names—determine the Joggle module/IR/pass interfaces.

## 14. Immediate implementation order

1. Freeze one hand-checkable F1 instance and enumerate it on paper.
2. Implement canonical assignment, legality reasons, and exhaustive oracles.
3. Add B0/B1 and a deliberately literal conditioned-variant baseline.
4. Add the bounded reference interpreter and assert declared versus observed counters.
5. Run D1 on F1 before adding F2/F3.
6. Add context transitions and B8 for D2.
7. Add FCC/MDD/AND–OR/dynamic-plan mechanism emulators for D3.
8. Only after oracle agreement, add generated instances and real-target adapters.

This order prevents a large compiler implementation from hiding a failed research premise.
