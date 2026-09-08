# Research contract

Status: accepted on 2026-09-08. G0 passed. G1 is active.

Working title: **Semantics-Derived Kernelization for Deterministic Edge Neural
Inference**

## Problem

Extensible neural compilers commonly describe a target twice: a graph layer
registers accepted operators, patterns, or schedules, while a backend
separately describes target instructions and memories. The graph decision is
then made before instruction selection and allocation know whether an
intermediate can remain resident, whether its consumer can read the same
representation, or whether closing a region forces a lossy or expensive
external-memory transition.

Joggle tests whether the target description itself can induce the region
boundary. It does not assume that functional denotation alone determines
performance: storage geometry, transfer routes, resource constraints,
dependency protocols, and deterministic cost observations are executable
target facts too.

## Falsifiable hypothesis

On fixed-shape, batch-one, pretrained ONNX models and two FPGA-capable
accelerator organizations, a planner using only executable target semantics can
jointly choose target instructions, resident tile representations, spills, and
host/device cuts without target-specific operator names or fusion rules.

It must satisfy all of the following:

- reproduce legal expert-known resident chains from semantics;
- discover at least one profitable complete-model region absent from the
  expert pattern set;
- reduce generated-backend expansions by at least 5x compared with exhaustive
  closed convex-region synthesis on overlapping candidates;
- improve median end-to-end latency or off-chip traffic by at least 10%, with
  at least one real-time, memory, or fit threshold crossed; and
- work on two accelerator organizations and ordinary pretrained models.

Failure of any item rejects the paper thesis rather than being hidden as an
engineering limitation.

## Concept model

The compiler input is a normalized set of typed tensor equations with explicit
shapes, index maps, reductions, constants, and observable numeric conversions.
Operator names may remain for diagnostics but cannot decide target legality.

A target is an executable transition system containing instruction
denotations, legal operand and result representations, storage geometry,
transfer and control routes, resource constraints, and deterministic cost
observations. It may not contain model-operator support tables, graph patterns,
fusion groups, or per-operator schedules.

The open search state is factored rather than represented by one catch-all IR
node:

    Cut = (live semantic tiles, unresolved dependencies)

    Realization(tile) =
      (storage, element encoding, tile/index view, symbolic footprint/alias)

    Open target state = protocol balance, when not normalized away

    Fragment =
      (instruction DAG, read/write/clobber constraints,
       resulting realizations, Pareto observations)

Explicit version counters are unnecessary because semantic identities plus
clobber effects distinguish destructive updates. Remaining-consumer counts
come from the graph cut. Concrete addresses are deferred to constraint solving;
symbolic footprints are sufficient for search.

## Algorithm under test

1. Normalize a fixed-shape model without erasing quantization or conversion
   observables.
2. Choose an elimination order and separators from the data-dependency graph.
3. Extend the live frontier by matching tensor equations to target instruction
   denotations and legal data routes.
4. Carry residency and symbolic allocation while regions remain open; model
   spill, reload, host fallback, launch, and termination as ordinary target
   transitions.
5. Canonicalize compatible frontier states, Pareto-prune them, and memoize
   repeated separator summaries.
6. Select a complete-model plan. Its chosen export/control transitions are the
   kernel boundaries.

The search can be exponential in frontier width. No model-independent
polynomial claim is made. Separator decomposition, canonicalization, bounded
beams, and a safe closed-kernel fallback are evaluated rather than assumed.

## Evidence completed: G0

Pinned ACT and VTA sources established:

- an ACT dot-softmax resident chain with 6 instructions, 2 external loads, and
  1 store, while forced closure cannot re-import the D2 value;
- a VTA wide-accumulator chain with 8 instructions, 3 loads, and 1 store, while
  exact wide closure is unavailable;
- denotation-derived ACT matches for dot and reduction-based softmax whose open
  summaries compose to the same instruction sequence as direct combined
  extraction;
- exact two-versus-three tile capacity, representation/view, and destructive
  fan-out counterexamples; and
- execution through ACT's generated functional oracle and VTA's original
  dependency simulator.

These results prove feasibility, not novelty, scalability, or performance.

## Active gate: G1

G1 uses frozen subgraphs from Model Zoo MobileNetV2 QDQ, ResNet50 QDQ,
EfficientNet-Lite4 QDQ, and BERT-SQuAD INT8 as a branch-width negative control.
It compares:

- exhaustive convex regions followed by closed synthesis;
- greedy maximal feasible regions;
- fixed expert patterns; and
- compositional open-frontier summaries.

Required measurements are backend expansions, memo hits, compile time, peak
memory, frontier width, chosen cuts, and optimality on small exhaustively
checkable instances. G1 is killed if the open method does not reduce backend
work by at least 5x or cannot soundly cover residual, branching, reduction, and
requantization structure.

## Publication boundary

ASPLOS is the primary target only if G2 produces deterministic whole-model
latency, traffic, energy, occupancy, and code-size evidence on physical
FPGA-capable targets. TACO is a fallback only if the strongest result becomes
the frontier algorithm or formal composition boundary. A software-engineering
framework, language design, or benchmark paper is not an acceptable outcome.
