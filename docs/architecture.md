# Architecture

Joggle is a lightweight compiler substrate for people co-designing AI software
and hardware. It is meant for experiments in data formats, operators,
transformations, analysis, simulation, and emission where a fixed dialect
ladder or target model would become the constraint.

## Public model

The source and C++ names correspond directly:

| Source concept | C++ type | Meaning |
| --- | --- | --- |
| `module name@version { ... }` / `module` | `joggle::Module` | The single identity, symbol, import, and multi-Function IR owner |
| `fn` / `function` | `joggle::Module::FunctionDecl` | One named callable member with a canonical signature |
| materialized function body | `joggle::Function` | Optional executable CFG and def-use graph owned by that member |

All executable IR ownership types share one namespace and one hierarchy:

```text
joggle::Module
  └─ joggle::Module::FunctionDecl
       └─ optional joggle::Function body
            └─ joggle::Block
                 ├─ joggle::Op
                 ├─ joggle::Value
                 └─ joggle::Terminator
```

There is no second whole-IR or graph container. Parsing returns a `Module`;
whole-module compiler functions consume and return that same type. A repository
stores immutable Module releases and optional native behavior, but it introduces
no language object or IR layer.

Declarations and materialized bodies are two states inside the same Module,
not separate owners. `Module::function(name)` reflects the unique function
member and its signature; `Module::body(declaration)` accesses its concrete
editable CFG without guessing among overloads. `Compiler::materialize(...)`
specializes a source definition into a `Function`, while
`Compiler::create_function()` constructs an empty one. Generic specialization
therefore creates a body, not another Module representation.

`Module::insert` turns a standalone Function into a real member: it creates
one declaration from the current argument and result types, attaches that
declaration to the body, and freezes the signature. Later transactions may
rewrite ops or control flow but cannot make the declaration and CFG
silently drift apart. `Compiler::verify(module)` checks the attachment before
validating each body against the linked environment. The stored declaration
uses fully qualified type identities internally, so another materialized
member can call it before a format-and-parse round trip; human-facing Function
formatting still uses Prelude's concise type spellings.

Module identity has two deliberately different hashes. `digest()` covers the
complete canonical artifact and therefore changes when a body changes; release
locks and native behavior use it. `interface_digest()` erases Function bodies
and covers imports plus declarations; Compiler API boundaries use it when exact
interface compatibility is required. Member Symbols themselves use a
versioned qualified declaration name. An optimizer can change executable
content, and a Module can add another member, without renaming an unchanged
tensor type or call signature. Incompatible reuse of the same Module version is
still detected by its interface digest when a declaration crosses a Compiler
boundary.

The other public concepts are small:

- `joggle::Compiler` owns a linked declaration environment, behavior bindings,
  diagnostics, and deterministic evaluation limits.
- `joggle::Type` and `joggle::Attribute` are immutable instances of
  Module-declared schemas.
- `fn` is the only callable declaration in source.

Every `Op` is a typed call to one `fn` declaration. Its inputs have one schema,
but two IR roles are derived from their declared domains:

- value-domain inputs are SSA operands, even when a caller supplies a Known
  literal while constructing the IR;
- compiler-domain inputs are immutable named properties.

`Op::operands()`, `Op::properties()`, and the named lookup methods expose this
split directly. Consequently a fusion rule can traverse def-use edges while a
layout or quantization rule reads stable properties, without inventing a
second operator schema beside the language declaration.

A Module also owns immutable binary data addressed as `sha256:<digest>`.
`Module::store` deduplicates it and `Module::data` provides read-only views.
Artifact identity covers the data names, while interface identity does not.
This keeps model constants, rewritten IR, and their transactional publication
inside one value without serializing megabytes into the textual body or
threading a separate resource object through every compiler function.

## One extensibility mechanism

Loading, transforming, analysing, simulating, and emitting are roles of typed
functions, not compiler subsystems or declaration kinds. For example:

```joggle
fn read_onnx(path: string) -> module;
fn legalize(input: module, target: target) -> module;
fn estimate(input: module, target: target) -> estimate;
fn emit(input: module, target: target) -> bytes;
```

The signatures state what composes. A Module may define a body in Joggle, bind
an external implementation in C++, or leave a function call Residual. The
core has no `frontend`, `lower`, `analysis`, `pass`, or `backend` registry.
Teams may use those words as project roles without making them language
keywords.

A typed Residual `Op` is also a complete specialization site. Materializing
that call recovers only the callee's declared generic bindings from concrete
operand, property, and result types, then instantiates its ordinary source
body. This keeps reusable generic operator implementations executable without
a Kernel object, dialect registry, or parallel template API.

The CLI preserves the same rule. `joggle run` invokes one reflected function
with a `bytes -> bytes`, `bytes -> module`, `module -> module`, or
`module -> bytes` boundary; it does not accept an external list of specially
classified passes. Module inputs are linked and materialized before the call,
and Module outputs use canonical source. In-process users retain the full C++
  type surface, including Module-owned artifacts.

This is also why Joggle does not prescribe tiles, streams, FPGA resources,
RISC-V ops, devices, schedules, or cost models. A Module defines
the types and functions it needs. A bridge Module imports two vocabularies and
owns conversions between them; neither vocabulary has to depend on the other.

## Staged execution

Every function invocation carries one typed value environment. Availability is
orthogonal to type:

- a Known value has a compiler payload;
- a Residual value is represented by a `joggle::Value`.

A function declaration stores one ordered input list and one ordered result
list. Whether a port belongs to a compiler domain (`int`, `type`, `attr`, and
so on) or denotes a module value is derived from its declared domain; no
parallel static/value signature is stored. Known and Residual are execution
states of those same typed ports, not declaration flags.

Known locals retain both their declared domain and payload. In particular, an
empty `list<string>` remains distinct from an empty `list<type>`, so ordinary
overload resolution does not need payload tags, special list operations, or a
second compile-time language.

A body executes as far as its Known inputs permit. Remaining calls and control
flow become IR. Prefix `@` asserts that an expression must finish as Known; it
does not invoke another evaluator or another function kind.

The same rule controls source flow:

- Known `bool` selects `if` or advances `while` in the compiler;
- Residual `i1` creates Blocks and typed edges;
- `for item in values` requires a Known `list<D>` and deterministically expands
  one iteration per element;
- `for item: T in values` materializes a finite integer progression as a
  fixed-size CFG whose iterator has module type `T`.

Generic values bound by compiler inputs are ordinary Known locals. They can
drive control flow and dependent type expressions without being wrapped in
temporary IR Values.

Integer-count loops use the same rule rather than a second loop form. The
ordinary Hermetic Prelude overloads of `range` construct a Known `list<int>`.
An untyped iterator expands when generic `N` is Known; an iterator annotated
with a module type residualizes the same progression into header, body, latch,
and exit Blocks. Literal, comparison, and increment semantics come from
ordinary visible functions for that type. Range construction and compile-time
expansion are bounded by the Compiler evaluation budget.

Execution preserves the declared result sequence: zero results are empty, one
result is one value, and multiple results remain positional values. The C++
boundary maps those cases to `void`, `T`, and `std::tuple<Ts...>`; the IR and
source evaluator do not invent a unit result or a separate result container.

## One graph, no Graph object

A Function already contains the nodes and relations needed by graph-shaped AI
workloads. Its Blocks and terminators define the CFG; Ops and Values
define def-use. `predecessors`, `users`, and `dominates` query those relations
directly. Analyses may cache richer products, but they do not own a second
module representation.

Structured source is an authoring form over this IR. It is not a Region tree.
Ops never own Blocks. Explicit Blocks remain available for arbitrary
transformation output and exact serialization.

## Trusted kernel and Module plane

The kernel implements parsing, canonical formatting, module identity, release
resolution, typed values, overload and dependent-type solving, staged
execution, IR ownership, transactions, and verification. The ambient Prelude
declares compiler domains, native scalar types, callable types, interfaces,
the whole-module `module` type, and compiler-domain primitive functions. Its
source is embedded from `language/prelude.joggle`, so source tooling and the
runtime share one authority. Those primitives use the same `fn`
resolution and execution path as Module functions; the kernel contributes
only their deterministic Hermetic implementations. It never evaluates an
undeclared operator token or a magic function name.

Everything specific to an AI framework, model format, hardware target, numeric
format, schedule, simulator, or emitter belongs in installable Modules and
optional behavior libraries. This boundary keeps the core small while leaving
experiments inspectable and serializable.

Prelude's `module` is represented by `joggle::Module`; its `function` value is
the materialized `joggle::Function` body of a `Module::FunctionDecl`. These
are the only core-owned host artifact mappings. Extension-owned schedules,
estimates, traces, object files, or target descriptions use ordinary declared
types and `Compiler::represent`.

## Deliberate non-goals

Joggle currently does not try to be a complete general-purpose language, ship
a universal optimizer, model every device, or replace mature code generators.
It supplies a coherent point at which such components can be registered and
composed. Closure literals and automatic capture lifting are not implemented;
named Functions already work as typed callable values.
