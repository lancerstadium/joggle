# Design

## Purpose

Joggle is an embeddable compiler workbench for neural-network and AI hardware/
software co-design experiments. Its core is infrastructure, not a fixed
optimization method or deployment stack.

The engineering hypothesis is falsifiable: one structured function IR and one
typed host-function extension boundary should support model import, graph and
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
attach an implementation to a declared host function through one versioned C
ABI. The declaration remains the single source of its signature.

An importer, transform, analysis, simulator, or emitter is therefore an
ordinary compile-time function. Core has no codec, pass, target, device,
analysis, emitter, or artifact class family.

## Invariants

- Core depends only on the C++20 standard library.
- Adding computation never adds an `Op` subclass or parser case.
- Adding a module never generates or recompiles a core header.
- Mutations go through `Mod` and preserve handle/use-def integrity.
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
2. A C++ host function and a `.jog` compile-time function must both rewrite the
   same function representation.
3. An ONNX module must import an official model without a core operator switch.
4. A target module must add a number type, primitive, selection transform,
   simulator, and emitter with zero core changes.

Any failed gate reopens the relevant boundary; it does not justify silently
adding another framework layer.
