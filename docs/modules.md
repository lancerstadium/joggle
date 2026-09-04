# Module design

This document defines the replacement module model. It intentionally specifies
composition rules before selecting AI vocabularies or hardware targets.

## One installation unit

One extension has one versioned `.joggle` Module identity. It may additionally
have a native library for host-only work, but that library implements the same
Module and cannot introduce declarations of its own.

```text
extension/
  module.joggle
  native/          optional C++ implementation
  tests/
```

The directory layout is conventional, not semantic. Installation, dependency
resolution, locking, and native-library selection use the Module name, version,
and digest.

There is no separate dialect package, pass plugin, target descriptor package,
runtime adapter, or Artifact package.

## Public contents

A Module has only the language's existing member forms:

- `import` selects another Module;
- `interface` states a structural contract;
- `type` and `attr` define data carried by types and calls;
- `fn` defines every callable operation.

The same `fn` form covers four execution situations:

| Situation | Declaration | Outcome |
| --- | --- | --- |
| source computation | body present, Residual inputs | materialized Function IR |
| compiler computation | body present, Known inputs | evaluated value |
| native compiler work | body absent, native binding, Known inputs | host-produced value |
| primitive boundary | body absent, Residual inputs | retained typed Op |

These are states of one declaration, not four extension APIs.

## Interfaces are capabilities

An interface is a named structural capability. It is useful for generic
constraints and for asking whether a declaration belongs to an accepted target
boundary.

```joggle
interface scalar: type {
  storage_bits: int;
}

interface instruction: fn;
```

An interface is not a C++ trait class, verifier callback, rewrite hook, or pass
registration. Ordinary authors implement it by listing the interface after a
declaration and, for type fields, supplying source expressions.

## Conversion and optimization

A conversion is an ordinary typed function. No direction is built into the
core:

```joggle
fn convert(input: module, format: type) -> module;
```

Its implementation may use the C++ transactional rewrite utilities or a future
source reflection library. The Module owns its policy and legality boundary.
Joggle does not require the names `lower`, `legalize`, `schedule`, or `pass`.

An end-user pipeline is also an ordinary source function, so its stages remain
individually callable:

```joggle
fn compile(input: bytes, machine: type) -> bytes {
  imported = @format.read(input);
  selected = @target.select(imported, machine);
  return @target.emit(selected, machine);
}
```

## Kernel definition and target closure

A reusable kernel is a source-defined `fn`. It may call another source kernel
and eventually reach bodyless primitives.

A target emitter calls `Compiler::specialize` with a predicate describing the
Functions it accepts. Unaccepted calls are recursively specialized from their
concrete types and Known properties. The operation fails if an unaccepted call
has no body.

This creates a typed agreement:

```text
kernel author supplies source body
                ×
target author supplies accepted capability boundary
                =
closed target program or a diagnostic
```

Neither side registers operator names with the other or with the core.

## Target definition

Joggle has no universal Target class. A hardware extension normally declares:

- one or more configuration types;
- data formats and references needed by that hardware;
- bodyless primitive Functions accepted by its emitter;
- optional source kernels and compiler Functions;
- one or more explicit `emit(...)->bytes` functions.

Those are recommendations, not required fields. An FPGA experiment may emit
RTL and metadata; a RISC-V experiment may emit an object and runtime data; a
simulator-only Module may return a structured trace and define no emitter.

The core never interprets these declarations or assumes a memory capacity,
tile shape, stream model, ISA, or cycle model.

## Native implementation boundary

Native code is permitted only when source cannot express the operation, such
as parsing ONNX protobuf, invoking an external code generator, or performing a
large compiler analysis.

Native code binds existing bodyless `fn` declarations by canonical identity.
It may use `Module`, `Function`, `Op`, `Value`, `Type`, `Attribute`, and
`Diagnostics`; it must not create a parallel schema or hidden operation
registry.

The build command `joggle_module(SOURCE ... NATIVE ...)` creates this optional
implementation and binds it to the exact canonical Module identity. Platform
ABI details are generated privately and are not part of the authoring API.

## Surface budget

A public Module function must be useful to another Module or to an end user.
Serialization helpers, debug reports, reference executors, test oracles, and
container readers belong in private implementation libraries or tests.

Before a Module is admitted to the future standard set, it must demonstrate:

1. a stable semantic boundary rather than a project-specific pipeline rung;
2. at least two independent consumers or producers;
3. no required change to compiler-core declarations;
4. deterministic composition and failure diagnostics;
5. an end-to-end test using an externally maintained model or workload.

## Rebuild order

The removed experimental Modules will not be recreated name-for-name. The new
vertical slice will be built in this order:

1. define one minimal shaped-value and callable semantic contract from actual
   ONNX import requirements;
2. implement ONNX as a format Module that produces that contract without
   target knowledge;
3. implement one small but executable edge target Module whose accepted
   boundary is explicit;
4. add one independently installable data-format or kernel extension;
5. evaluate extension coupling, compilation, correctness, memory, and measured
   execution against an appropriate established compiler.

The names and exact boundaries of those Modules remain undecided until the
first two use cases agree. This is deliberate: vocabulary should be extracted
from demonstrated composition, not declared as a new fixed ladder in advance.
