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

## Module ownership

The core exposes one top-level IR owner, its function members, and optional
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

An analysis may return an ordinary Module-declared type. An emitter returns
`bytes`. Neither result introduces a second universal owner beside `Module`.
The encoding of emitted bytes belongs to the emitter; a pipeline does not call
a generic packer or carry an Artifact wrapper between functions.

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
    [&](const joggle::Op& op,
        joggle::Function::Edit& edit,
        joggle::Diagnostics&) {
      if (op.callee() != source_call) {
        return false;
      }
      auto first = edit.insert(op, normalize,
                               op.arguments());
      auto second = edit.insert(op, target, {first.value()});
      edit.replace(op, {second.value()});
      return true;
    },
    diagnostics);
```

The rule visits the committed Ops present at the start of the sweep
and returns whether it changed each one. Every changed Function is verified;
an exception, a new diagnostic, or failed verification publishes nothing.
Replacing an Op with a positional result list supports erasure and
multi-Op expansion without inventing a replacement object.

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
    [&](const joggle::Op& op)
        -> std::optional<joggle::Module::FunctionDecl> {
      return compiler.conforms(op.callee(), elementwise)
                 ? choose_replacement(op)
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
    [&](const joggle::Op& op) {
      return target_accepts(op.callee());
    },
    diagnostics);
```

The predicate defines the caller's accepted result rather than naming a global
target or lowering direction. The candidate Module is published only when all
materialized Functions verify and every remaining Op is legal. The
first illegal residual call is diagnosed with its Function context.

## Source closure

Conversion rewrites calls the target explicitly chooses to replace. Source
closure handles the complementary case: a call already has a reusable source
body and the target only needs it expanded to an accepted primitive boundary.

```cpp
auto closed = compiler.specialize(
    module,
    [&](const joggle::Module::FunctionDecl& function) {
      return compiler.conforms(function, arithmetic_primitive) ||
             compiler.conforms(function, memory_primitive) ||
             compiler.conforms(function, target_instruction);
    },
    diagnostics);
```

Accepted calls remain untouched. Every other call is specialized from its
concrete operands, properties, and result types, inserted as a local Function,
and followed recursively. An opaque unaccepted call and recursive expansion
fail closed. The algorithm is deterministic and does not mutate its input.

This is deliberately not a lowering registry. A target chooses its accepted
interfaces in its own emitter, while a kernel author supplies an ordinary
generic `fn` body. The two meet through typed conformance rather than a table of
operator names.

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

A native function that reports a diagnostic fails that invocation, even if
its C++ callback also returns a value. The enclosing source function stops at
that call, so later transformations, emitters, or other effectful functions do
not execute after a reported error.

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

The following stay in Modules and optional native libraries:

- ONNX or other format import;
- quantization, layout, tiling, scheduling, and bufferization policy;
- FPGA, custom-op, or ISA vocabularies;
- device/resource descriptions and simulators;
- cost, latency, energy, WCET, or accuracy models;
- target code generation and SystemVerilog or assembly emission.

They can share the utility layer while retaining their own types and
contracts. This keeps Joggle useful for co-design without turning one research
prototype's assumptions into universal compiler semantics.
