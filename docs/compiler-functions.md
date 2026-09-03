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
- `fn` / `joggle::Module::FunctionDecl` is one named callable member with its
  signature;
- a materialized member body is `joggle::Function`, an editable CFG inside
  that Module.

`module` and `function` values use copy-on-write storage. A native transform
receives an isolated value and returns the declared result; an empty
`std::optional` reports failure. A Function transform edits its value through
`joggle::Function::Edit`, whose `commit` verifies the candidate. The caller
publishes the returned value only after the complete invocation succeeds.
Every typed call validates materialized Function bodies at both its input and
output boundary; repeated references to the same immutable revision are checked
once per invocation.

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

## Transactional rewriting

General structural rewrites are one lambda and one transaction. There is no
pattern base class, rewrite registry, or second IR container:

```cpp
auto changed = joggle::rewrite(
    module,
    [&](const joggle::Instruction& instruction,
        joggle::Function::Edit& edit,
        joggle::Diagnostics&) {
      if (instruction.callee() != source_call) {
        return false;
      }
      auto first = edit.insert(instruction, normalize,
                               instruction.arguments());
      auto second = edit.insert(instruction, target, {first.value()});
      edit.replace(instruction, {second.value()});
      return true;
    },
    diagnostics);
```

The rule visits the committed Instructions present at the start of the sweep
and returns whether it changed each one. Every changed Function is verified;
an exception, a new diagnostic, or failed verification publishes nothing.
Replacing an Instruction with a positional result list supports erasure and
multi-Instruction expansion without inventing a replacement object.

When inserted calls must be reconsidered, the bounded driver is explicit:

```cpp
auto changed = joggle::rewrite_to_fixpoint(
    module, rewrite_rule, 8, diagnostics);
```

Each iteration sees the previous iteration's committed calls. A zero-change
iteration proves convergence. Exhausting the supplied limit diagnoses failure
and publishes none of the intermediate Module values; there is deliberately no
hidden default iteration budget.

Exact call mapping remains as a pair of smaller convenience functions:

```cpp
auto changed = joggle::replace_calls(
    module, source_call, target_call, diagnostics);

auto selected = joggle::map_calls(
    module,
    [&](const joggle::Instruction& instruction)
        -> std::optional<joggle::Module::FunctionDecl> {
      return compiler.conforms(instruction.callee(), elementwise)
                 ? choose_replacement(instruction)
                 : std::nullopt;
    },
    diagnostics);
```

Both functions return the number of changed calls, with `std::nullopt` on
failure. They use the same rewrite transaction, so the Module overload
publishes nothing if any Function fails verification. Matching uses Function
handles or explicit interface queries; the utility never interprets a textual
function name.

## Conversion completion

`convert` combines the same rewrite lambda with a final legality predicate:

```cpp
auto changed = joggle::convert(
    module, rewrite_rule,
    [&](const joggle::Instruction& instruction) {
      return target_accepts(instruction.callee());
    },
    diagnostics);
```

The predicate defines the caller's accepted result rather than naming a global
target or lowering direction. The candidate Module is published only when all
materialized Functions verify and every remaining Instruction is legal. The
first illegal residual call is diagnosed with its Function context.

## Command-line boundary

`joggle run module.joggle function input -o output` invokes one reflected
function with one of four portable signatures: `bytes -> bytes`,
`bytes -> module`, `module -> module`, or `module -> bytes`. A Module input is
parsed into the same compilation, linked with its dependencies, and copied
with every default-specializable source Function materialized. A Module output
is canonical Joggle source. Bytes remain the opaque boundary for other file
formats.

This makes individual loaders, whole-Module transformations, and emitters
directly runnable while preserving ordinary typed composition. Extension-owned
`model`, `schedule`, or `estimate` values still compose in source or through
the C++ API; the CLI does not classify them or register a pass hierarchy.

A pipeline is consequently normal source, not an out-of-band pass list:

```joggle
fn prepare(input: module) -> module {
  normalized = normalize(input);
  return specialize(normalized);
}
```

Each native `module -> module` implementation receives a copy-on-write value.
The intermediate result is checked before the next call and only the final
result crosses the `run` boundary. The same functions remain independently
invocable and require no generated registration table.

If a nested call fails, its original diagnostic remains the primary error.
Joggle adds one source-positioned note for each enclosing `fn` call, from the
failing call outward. This is ordinary call context; pipelines do not need a
separate trace format or pass-manager diagnostics.

## Analysis values and reuse

An analysis is an ordinary typed function: its result is a Module-declared type
with a registered C++ representation. Read-only implementations accept
`const Function&` or `const Module&`; no analysis declaration or manager is
needed.

Algorithms that cache a Function-local result retain its `Function::Revision`
beside the result and reuse it only while it equals `function.revision()`.
Function copies initially share a Revision, successful edits acquire a new one,
and failed or no-op rewrites preserve it. Cache ownership and eviction remain
with the analysis implementation rather than a global compiler registry.

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
