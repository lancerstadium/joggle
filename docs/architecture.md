# Architecture

Joggle is a lightweight compiler substrate for people co-designing AI software
and hardware. It is meant for experiments in data formats, operators,
transformations, analysis, simulation, and emission where a fixed dialect
ladder or target model would become the constraint.

## Public model

Two objects named Module serve different, explicit roles:

| C++ type | Meaning |
| --- | --- |
| `joggle::Module` | Immutable, versioned schema package: declarations and contracts |
| `joggle::ir::Module` | Mutable executable artifact: a named set of IR Functions |

All executable IR ownership types share one namespace and one hierarchy:

```text
joggle::ir::Module
  └─ joggle::ir::Function
       └─ joggle::ir::Block
            ├─ joggle::ir::Instruction
            ├─ joggle::ir::Value
            └─ joggle::ir::Terminator
```

There are no root aliases for these IR types. `joggle::Module` is not an IR
container, and `joggle::ir::Module` is not a declaration package.

The other public concepts are small:

- `joggle::Compiler` owns a linked declaration environment, behavior bindings,
  diagnostics, and deterministic evaluation limits.
- `joggle::Type` and `joggle::Attribute` are immutable instances of
  Module-declared schemas.
- `fn` is the only callable declaration in source.

## One extensibility mechanism

Loading, transforming, analysing, simulating, and emitting are roles of typed
functions, not compiler subsystems or declaration kinds. For example:

```joggle
fn read_onnx(path: string) -> ir.module;
fn legalize(input: ir.module, target: target) -> ir.module;
fn estimate(input: ir.module, target: target) -> estimate;
fn emit(input: ir.module, target: target) -> bytes;
```

The signatures state what composes. A Module may define a body in Joggle, bind
an external implementation in C++, or leave a program operation Residual. The
core has no `frontend`, `lower`, `analysis`, `pass`, or `backend` registry.
Teams may use those words as project roles without making them language
keywords.

This is also why Joggle does not prescribe tiles, streams, FPGA resources,
RISC-V instructions, devices, schedules, or cost models. An extension defines
the types and functions it needs. A bridge Module imports two vocabularies and
owns conversions between them; neither vocabulary has to depend on the other.

## Staged execution

Every function invocation carries one typed value environment. Availability is
orthogonal to type:

- a Known value has a compiler payload;
- a Residual value is represented by a `joggle::ir::Value`.

A body executes as far as its Known inputs permit. Remaining calls and control
flow become IR. Prefix `@` asserts that an expression must finish as Known; it
does not invoke another evaluator or another function kind.

The same rule controls source flow:

- Known `bool` selects `if` or advances `while` in the compiler;
- Residual `i1` creates Blocks and typed edges;
- `for item in values` requires a Known `list<D>` and deterministically expands
  one iteration per element.

Generic values bound by compiler inputs are ordinary Known locals. They can
drive control flow and dependent type expressions without being wrapped in
temporary IR Values.

Execution preserves the declared result sequence: zero results are empty, one
result is one value, and multiple results remain positional values. The C++
boundary maps those cases to `void`, `T`, and `std::tuple<Ts...>`; the IR and
source evaluator do not invent a unit result or a separate result container.

## One graph, no Graph object

A Function already contains the nodes and relations needed by graph-shaped AI
programs. Its Blocks and terminators define the CFG; Instructions and Values
define def-use. `predecessors`, `users`, and `dominates` query those relations
directly. Analyses may cache richer products, but they do not own a second
program representation.

Structured source is an authoring form over this IR. It is not a Region tree.
Instructions never own Blocks. Explicit Blocks remain available for arbitrary
transformation output and exact serialization.

## Trusted kernel and extension plane

The kernel implements parsing, canonical formatting, module identity, package
resolution, typed values, overload and dependent-type solving, staged
execution, IR ownership, transactions, and verification. The ambient Prelude
declares compiler domains, native scalar types, callable types, and interfaces.

Everything specific to an AI framework, model format, hardware target, numeric
format, schedule, simulator, or emitter belongs in installable Modules and
optional behavior libraries. This boundary keeps the core small while leaving
experiments inspectable and serializable.

## Deliberate non-goals

Joggle currently does not try to be a complete general-purpose language, ship
a universal optimizer, model every device, or replace mature code generators.
It supplies a coherent point at which such components can be registered and
composed. Closure literals and automatic capture lifting are not implemented;
named Functions already work as typed callable values.
