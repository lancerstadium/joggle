# Compiler functions

Loading, conversion, optimization, analysis, simulation, and emission all use
ordinary typed `fn` declarations. The role is described by the signature and
the owning Module, not by a second declaration hierarchy.

```joggle
module deployment@1.0.0 {
  import target@1.0.0;

  type estimate(cycles: int, bytes: int);

  fn read(path: string) -> module;
  fn optimize(input: module, target: target.config) -> module;
  fn analyze(input: module, target: target.config) -> estimate;
  fn emit(input: module, target: target.config) -> bytes;

  fn compile(path: string, target: target.config) -> bytes {
    optimized = optimize(read(path), target);
    return emit(optimized, target);
  }
}
```

This shape supports bidirectional conversion and alternative analyses without
inventing `frontend`, `backend`, `lower`, or `pass` categories. A bridge Module
may publish several conversions, and an optimizer may consume configuration or
analysis values as normal typed arguments.

## Artifact ownership

The core exposes one top-level artifact, its function members, and optional
materialized bodies:

- `module` / `joggle::Module` is the identity, symbol, import, and ownership
  boundary;
- `fn` / `joggle::Module::Function` is one named callable member with its
  signature;
- a materialized member body is `joggle::ir::Function`, an editable CFG inside
  that Module.

`module` and `function` values use copy-on-write storage. A native transform
receives an isolated value and returns the declared result; an empty
`std::optional` reports failure. A Function transform edits its value through
`joggle::ir::Function::Edit`, whose `commit` verifies the candidate. The caller
publishes the returned value only after the complete invocation succeeds.

Analyses and emitted artifacts remain extension-owned. For example, a Module
declares `estimate`, `schedule`, or `object`, and its behavior registers the
corresponding C++ representation with `Compiler::represent<T>`. `bytes` is the
portable boundary for file emission, but an extension can retain a structured
artifact between stages.

## What the core guarantees now

The existing kernel provides the invariants every compiler function needs:

- canonical declarations, Module identity, dependency locking, and behavior
  identity checks;
- type/interface checking, overload resolution, dependent result types, and
  Known/Residual staging;
- explicit Blocks, typed edges, SSA values, def-use and dominance queries;
- structural and semantic verification on transactional edits;
- deterministic evaluation budgets and guarded host effects;
- canonical serialization of Functions and whole modules.

These guarantees are target-neutral. They are sufficient to write a correct
transform without a target or device model in the kernel.

## Transactional call mapping

The first reusable transform primitive is intentionally a pair of free
functions rather than a base class or registry:

```cpp
auto changed = joggle::ir::replace_calls(
    module, source_call, target_call, diagnostics);

auto selected = joggle::ir::map_calls(
    module,
    [&](const joggle::ir::Instruction& instruction)
        -> std::optional<joggle::Module::Function> {
      return compiler.conforms(instruction.callee(), elementwise)
                 ? choose_replacement(instruction)
                 : std::nullopt;
    },
    diagnostics);
```

Both functions return the number of changed calls, with `std::nullopt` on
failure. The Function overload commits one verified edit. The Module overload
plans every replacement first, edits a private copy, detaches only changed
Functions, and publishes nothing if any Function fails verification. Matching
uses Function handles or explicit interface queries; the utility never
interprets a textual function name.

## Command-line boundary

`joggle run module.joggle pipeline input -o output` invokes one reflected
function with the exact signature `bytes -> bytes`. Bytes are the portable
file boundary, not the pipeline's internal artifact type. A Module can compose
`bytes -> model`, `model -> schedule`, `schedule -> estimate`, and
`schedule -> bytes` functions without teaching the CLI any of those roles or
registering another pass hierarchy.

## Reusable facilities still required

The next implementation layer is a C++ utility library over the public IR, not
new source syntax. Its components should be independently usable:

1. **Structural rewriting.** Captured operands, multi-instruction replacement,
   erasure, greedy/fixed-point drivers, and explicit convergence limits. The
   initial `map_calls` utility covers verified call-to-call mapping only.
2. **Conversion contracts.** A caller-supplied legality predicate, declared
   conversion functions, partial/full conversion modes, and diagnostics that
   identify the first illegal residual construct. There is no global target
   registry and no assumed lowering direction.
3. **Analysis storage.** Results keyed by immutable Function snapshots, with
   explicit preservation after a committed edit. The cache owns no alternate
   graph representation and never changes observable compilation semantics.
The order matters. Structural rewriting, conversion legality, and analysis invalidation
must stabilize before shipping optimizer collections. Otherwise each example
would create an incompatible private framework.

## What remains outside the core

The following stay in Modules and behavior libraries:

- ONNX or other format import;
- quantization, layout, tiling, scheduling, and bufferization policy;
- FPGA, custom-instruction, or ISA vocabularies;
- device/resource descriptions and simulators;
- cost, latency, energy, WCET, or accuracy models;
- target code generation and SystemVerilog or assembly emission.

They can share the utility layer while retaining their own types and
contracts. This keeps Joggle useful for co-design without turning one research
prototype's assumptions into universal compiler semantics.
