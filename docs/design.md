# Design

## Purpose

Joggle is an embeddable compiler workbench for neural-network and AI hardware/
software co-design experiments. Its core is infrastructure, not a fixed
optimization method or deployment stack.

The engineering hypothesis is falsifiable: one structured function IR and one
typed native-function extension boundary should support model import, graph and
loop transforms, custom data formats, and target experiments without changing
the core, adding per-operator classes, or maintaining a second public IR.

## Concept model

The complete public IR vocabulary is:

```text
Mod  Fn  Blk  Op  Val  Ty  Attr
```

`Mod` owns storage. `Fn`, `Blk`, `Op`, and `Val` are stable handles. `Ty` and
`Attr` are immutable structural values. Concrete computation is always a call
to a `Fn`; only calls, constants, loops, conditions, returns, and block yields
are structural operations.

A model graph is a view of calls and values in a function body. A lowered
kernel is the same kind of function with explicit loops, indexing, storage, or
target calls. Neither requires another durable IR.

`Env` is not part of the IR. It owns module search paths, loaded declarations,
native function bindings, and environment diagnostics. Environments are local
objects, never global registries.

## Extension boundary

Modules declare types and functions in `.jog`. An optional native library may
attach an implementation to any external function declaration through one
size-checked C ABI. The declaration remains the single source of its signature.
The exported entry is always `joggle_module`; ABI evolution is represented in
the API record instead of encoded in public symbol and type names.

Function metadata is an open `Attr` dictionary. The parser preserves it but
does not reserve tag names. Selection, scheduling, testing, cost models, and
research modules may define their own tags. Native binding depends only on an
external declaration and a matching native symbol; it needs no marker.

An importer, transform, analysis, simulator, or emitter is therefore an
ordinary compile-time function. `run` interprets the same structured function
body over compile-time scalars, lists, and IR handles. The `ir` module exposes a
small, representation-complete reflection vocabulary; it is not another IR or
an operator catalog. Core has no codec, pass, target, device, analysis, emitter,
or artifact class family.

Compile-time execution is explicit, deterministic, and in-place on success.
It has no ambient file, network, clock, or process access. A failed function or
invalid result restores the input `Mod`, so experiments do not leave partially
rewritten IR behind.

## Invariants

- Core depends only on the C++20 standard library.
- Adding computation never adds an `Op` subclass or parser case.
- Adding a module never generates or recompiles a core header.
- Mutations go through `Mod` and preserve handle/use-def integrity.
- Call insertion names its existing insertion point and rejects operands that
  do not dominate it; no mutable global builder state is required.
- Unknown external symbols remain printable; operations that require their
  semantics diagnose the missing dependency.
- User-visible text never exposes generated SSA names.
- Canonical printing is deterministic and round-trips structurally.

## Non-goals

Core does not contain ONNX, tensors, NN operators, devices, memory hierarchies,
instruction sets, code generators, runtimes, JITs, e-graphs, solvers, schedules,
or cloud/edge policy. It is not a general-purpose language or package manager.

Those capabilities may be removable modules after the structural core proves
it can host them. A research mechanism becomes a paper contribution only after
its own method and experiments justify that claim.

## Gates

1. A generic, nested-loop matrix multiplication must parse, print, reparse, and
   verify without another IR.
2. A C++ native function and a `.jog` compile-time function must both rewrite the
   same function representation.
3. An ONNX module must import an official model without a core operator switch.
4. A target module must add a number type, primitive, selection transform,
   simulator, and emitter with zero core changes.

Any failed gate reopens the relevant boundary; it does not justify silently
adding another framework layer.

## M1 baseline

The first working M1 slice was measured on macOS with AppleClang 17 in Release
mode on 2026-09-09. A clean configure took 0.25 s and a parallel build took
2.10 s. Peak build resident memory was approximately 230 MB. The resulting
dynamically linked CLI was 292,456 bytes and the static library was 396,728
bytes. The installed header plus production source and CLI contained 2,466
lines.

These numbers are a local regression baseline, not cross-machine performance
claims. Tests and the sample native module are excluded from the source-line
count. The build made no network requests and used no third-party library.

## M2 slice

M2 adds collection iteration and transactional compile-time function execution.
`modules/opt/module.jog` implements add-zero folding using only normal language
forms and `ir` reflection calls. The workflow test applies the same rewrite once
from direct C++ traversal and once from the textual function, then checks a
forced post-edit failure rolls back byte-for-byte to the canonical input.

## M3 contract

The first external frontend target is the official ONNX Model Zoo
`mobilenetv2-7` model mirrored by the ONNX organization on Hugging Face. The
test input is pinned to repository revision
`b055c14ebe95ca2df473547484e4d447867951ed`; its 14,246,826-byte model has
SHA-256 `c1c513582d56afceff8516c73804e484c81c6a830712ab6d682253f4a3cd042f`.
`test/model.cmake` downloads and verifies it only when explicitly invoked, so a
normal build remains offline.

Inspection with the official ONNX 1.19 schema reports IR version 3, opset 7,
155 nodes, 267 initializers, 268 declared inputs, and one graph output. All 155
nodes have one output. The graph contains Conv, BatchNormalization, Relu, Add,
GlobalAveragePool, and Reshape; all initializers use typed fields rather than
`raw_data`. The importer must therefore exclude initializer-backed legacy graph
inputs and normalize typed tensor payloads without losing their bits.

The ONNX module uses Protobuf as an optional module dependency, not a core
dependency. It maps graph inputs to function parameters, initializers to
typed tensor constants with preserved bytes, nodes to calls named by ONNX
domain and operator, node attributes to structural `Attr` dictionaries, and
graph outputs to returns. Operator meaning is not decoded by a core switch.
The same codec boundary must be usable by a later TFLite module.

The pinned model now passes the complete codec gate: binary decode, generation
of 267 tensor constants and 155 calls, parse, verify, canonical print, reparse,
and structural equality. Its 14,156,560 bytes of initializer payload are
preserved in typed tensor constants. A Release run on the M1 reference machine
imports and prints the self-contained 28.4 MB module in approximately 0.49 s;
the second parse-print takes approximately 0.31 s. These are local regression
measurements, not general performance claims.

## M4 slice

The optional `sat` module is the extension-boundary gate. Its `sat<W>` type and
`sat.add` primitive are ordinary signatures. Its textual `sat.select` function
uses `ir.type` to select only matching additions; integer additions remain
untouched. Three native scalar functions recognize supported formats, execute
the saturating reference semantics, and emit a concrete SystemVerilog adder.

SystemVerilog is an output of that removable module, not a core backend or IR.
After the generic `ir.type` query completed the reflection boundary, the whole
format, policy, simulator, and emitter were added without changing the core
library, parser, evaluator, public header, or operation vocabulary.

## M5 slice

M5 turns open metadata and reflection into construction rather than mere
inspection. A module can use arbitrary function metadata to select work, query
users, insert any call, replace uses, and erase the old operation. Dynamic list
literals allow the same source language to collect IR handles.

The operator-neutral region primitive computes live-ins and a single live-out
for an ordered call region, enforces dominance and motion safety, preserves the
visible result name, and commits the fusion atomically. The generic textual
`opt.fuse` helper follows a user-supplied callee sequence; it contains no ONNX
or NN operator names. A test-only bridge applies the helper to the pinned
MobileNetV2 import and replaces 36 Conv-BatchNormalization-ReLU chains with 36
user-named calls, reducing those 108 calls to 36. The optimized 28.4 MB module
then verifies, prints, reparses, and remains structurally equal.

## M6 first slice

`Ty` now preserves canonical text while exposing a recursive constructor tree.
Known local and qualified calls are resolved against their module declaration;
generic bindings are inferred structurally and substituted into result types.
The verifier also propagates list element and region argument types to a fixed
point, so compile-time traversal code is checked with the same mechanism as
model code. Qualified resolution follows only explicit and transitive `use`
edges, preventing unrelated modules already present in an `Env` from changing
the meaning of a source file.

The generic `sat.add<W>` declaration is the first module-defined parametric
gate: inferred and explicit widths succeed, conflicting widths and wrong arity
fail with located diagnostics, and no saturating-arithmetic case exists in the
core. Language normalizations (`base.copy`, list construction, and indexing)
retain only the minimal intrinsic rules needed to recover ordinary source
bindings and iteration. Overload sets, named constructor declarations, and
explicit unresolved-call state remain before the M6 exit gate.
