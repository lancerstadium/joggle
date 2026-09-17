# Modification-scope study: what shape does a change have?

The manuscript claims that a compiler decision is easier to revise when it is an
ordinary definition than when it lives inside a separately maintained
implementation. Ease is not directly measurable, and the tempting proxy is
wrong: source lines reward terse code and drift with every refactoring, so this
study does not use them as evidence of ease. What it measures instead is the
**shape** of a change — where it lands, what else it obliges, and what must be
re-validated afterwards.

## Dimensions, and why each one is structural

| Dimension | Recorded as | What it stands for |
| --- | --- | --- |
| Integration spread | files created or modified | how many places a single decision reaches |
| Framework reach | framework-core files touched | whether a change can stay out of the system's own sources |
| Rebuild obligation | build-manifest edits | whether the change forces a toolchain step |
| Registration points | native registrations | whether the system must be told about the change separately |
| Dependency obligation | new dependency declarations | what the change drags in |
| Artifact obligation | generated artifacts | what must be regenerated to observe the change |
| Validation obligation | oracle result per task | whether a completed change is checked at all |
| Descriptive only | source lines | reported for completeness, never used as an ease claim |

## Protocol

Four frozen tasks, each performed in each system through that system's
documented extension route. A task counts as complete only when it produces an
oracle-checked artifact. A task a system cannot complete keeps its row and is
recorded as `unsupported`; nothing is dropped and no cell is estimated.

The route for each task, and the observed boundary, are recorded in
`paper/data/extension-{footprint,tvm,onnx-mlir}-pilot.csv`. The matrix in the
manuscript is generated from those files by `render_scope.py`, not transcribed
by hand, so a table cell cannot drift from its record.

## What this study does not measure

Developer effort. There is no user study, and none is claimed. The scope
dimensions bound how much a change touches; they do not price the thought
required to find the right place to touch. The companion agent study measures a
process cost on the same systems — time to a validated change, rebuild cycles,
and agent steps — which is a machine cost, not a human one.

## Threats to validity

1. **One implementation per cell.** Each task was performed once per system by
   the authors. A repeat by another implementer could differ.
2. **The authors are not neutral.** The same authors wrote Joggle and performed
   every task. Frozen tasks, recorded routes, and generated tables bound the
   discretion, but they do not remove it.
3. **Task selection.** The four tasks were chosen to span implementation,
   policy, external code, and numeric format. They are not a random sample of
   maintenance work, and the refactoring suite that complements them is
   constructed rather than observed.
4. **Language and toolchain differ.** A change written in one system's
   extension language is not the same artifact as one written in another's, so
   the counts compare obligations, not equivalent text.
