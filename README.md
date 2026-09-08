# Joggle

Joggle is a research compiler for deterministic neural-network inference on
small, software-managed accelerators.

Its current question is deliberately narrow: can one executable description of
target instructions, storage, transfers, constraints, and costs induce both
legal accelerator implementations and profitable whole-model execution
regions—without a second target-specific operator, fusion, or schedule
registry?

Joggle treats a region boundary as a real target transition: live values are
exported to an ABI-visible representation, control returns or transfers, and
resident state is lost unless the target explicitly preserves it. Instruction
selection, residency, spilling, and host/device cuts are therefore one search
problem rather than independent compiler stages.

## Status

The previous implementation is preserved at Git tag
`archive/pre-relaunch-a2a281e`. It is not part of the current design.

The research contract and its first feasibility gate are complete.
Source-grounded experiments on ACT and VTA established that open resident
composition can be legal when forced external-memory closure is not, and
identified the minimum state needed to handle representation, capacity,
destructive fan-out, and target dependency protocols.

Only the next experimental slice is in scope: compare reusable open-frontier
search against closed convex-region synthesis on frozen subgraphs from standard
pretrained models. A general DSL, pass framework, package manager, broad
operator library, runtime, and speculative backends remain out of scope until
that mechanism survives its kill criteria.

See [docs/research.md](docs/research.md) for the accepted hypothesis, concept
model, evidence boundary, and gates.

## Non-goals

Joggle is not an SNN, LUT, Popcount, custom-number-format, graph-IR, or
kernel-language project. It does not claim automatic fusion, compiler
extensibility, semantic instruction matching, or hardware-aware partitioning
individually; each already has substantial prior art. The proposed contribution
is their specific conjunction: semantics-derived kernelization, compositional
open frontiers, and measurable deterministic edge-inference benefit.

## License

Joggle is licensed under the MIT License. See [LICENSE](LICENSE).
