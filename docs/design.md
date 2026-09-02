# Joggle architecture

This document describes Joggle's public objects and how data moves through
them.

## Public model

Joggle exposes three owning concepts:

- `Module` is an immutable, versioned package of named members.
- `Compiler` resolves Module imports, attaches optional C++ behavior, and runs
  Module passes.
- `Graph` is a mutable SSA program, opened by its qualified Module member name
  or assembled through C++.

Their relationship is direct: a named `graph` opens as a `Graph`, and every
pass maps that Graph to another valid state of the same Graph. Operation
declarations are reusable node contracts, analogous to function signatures.
References to those declarations resolve through the owning Module's imports.

`Diagnostic` is a read-only report. `Type`, `Attribute`, `Operation`, `Value`,
and `Region` are lightweight handles rather than managers.
Typed schema declarations use the `*Decl` suffix, so `Module::TypeDecl` cannot
be confused with a runtime `Type` instance. Graph members open directly as
`Graph` and have no public declaration wrapper.

`Compiler` owns linking and behavior state. `Graph` owns its nested regions,
SSA values, verification, and edit transactions. Extension authors work with
these objects directly.

## Core invariants

Let `C(x) = format(parse(x))` for a valid Module source `x`. Joggle maintains
the following contracts across the text, C++, package, and pass paths.

1. **Canonical source.** `C(C(x)) = C(x)`. Source paths, comments, and
   non-canonical whitespace do not occur in `C(x)`, so they cannot affect
   content identity.
2. **Content identity.** A Module identity is
   `I(M) = (name, version, SHA-256(C(M)))`. A member symbol is
   `(I(M), member kind, local name)`; an interface method additionally contains
   its owning interface symbol and method name. Human-readable qualified names
   are never used as persistent binding keys.
3. **Closed declarations.** Text references resolve only through the declaring
   Module and its imports. A runtime Graph records the exact linked Module
   identities accepted by its declarations. Loading an additional target
   Module can supply an explicitly selected pass, but cannot reinterpret a
   symbol already present in the Graph.
4. **Lexical SSA.** Each Value is either a Graph/Region argument or one indexed
   result of one Operation. A use is valid only when its definition precedes it
   in the same Region or is visible through an enclosing Region. Values do not
   escape to a parent or sibling Region. Graph outputs must be visible at the
   Graph boundary.
5. **Atomic edits.** `Graph::Edit` starts from a state `S` and mutates a private
   working state `S'`. Commit publishes `S'` only if structural SSA validation
   and every text-declared operation contract succeed; failure or abandonment
   restores `S`. `Compiler::run` adds an outer checkpoint and bound domain
   verification, so a failed C++ pass or pass sequence also restores its input
   Graph.
6. **Pass progress.** A text contraction replaces its matched root only with a
   proper subterm already present in that match, strictly reducing the number
   of matched Operations. Pass composition is acyclic. An external pass may
   construct arbitrary schema-valid Operations, but it runs under the same
   whole-Graph checkpoint and final verifier.
7. **Exact behavior.** A behavior library may bind only when its ABI, host
   target, and embedded `I(M)` agree with the linked Module. Its bindings attach
   atomically. Installed and locked behavior additionally uses the binary file
   digest, so Module identity and executable identity are checked separately.
8. **One program representation.** Parsing a named graph, constructing one in
   C++, transforming it, and reopening formatted output all produce the same
   `Graph` abstraction.

The `module`, `graph`, `pass`, `behavior_loader`, `package_cli`, and
`vertical_cli` tests are executable witnesses for these boundaries. They do
not replace proofs about extension-specific passes, but they prevent the core
from silently weakening the contracts above.

## Module

A `.joggle` source is the sole extension definition. Parsing produces an
immutable `Module` containing types, attributes, operations, interfaces,
passes, and graphs as peer members; C++ cannot add or replace them. Its
identity is:

```text
module name + semantic version + SHA-256(canonical source)
```

The canonical digest is independent of source paths and process-local values.
Human-facing symbols use `module.name`; persistent symbols include the complete
module identity.

An import alias is lexical sugar inside one Module, not another identity or
registry entry. Resolution replaces the local prefix with the imported
Module's real content identity before constructing or comparing declarations.

The on-disk package transports canonical Module source and may include a C++
behavior library. Runtime-linked declaration handles give C++ code typed access
to that source.

## Compiler

A `Compiler` links the Modules participating in one compilation. Module
`import` controls names that may appear in that Module's source; explicitly
loaded Modules may also contribute passes and operations to the compilation.
This lets an application load a model Module and a target Module side by side,
then explicitly run the target pass without making the model import its target.

Every constructed Graph records the exact identities in this linked set. Graph
verification accepts declarations from that set, while text parsing still
resolves each source reference through its owning Module's imports. The host
selects a compilation by loading Modules and an optional lock before `link()`.

## Attached behavior

The Module declares its types, operations, and executable graphs without
exposing C++ implementation mechanics:

```joggle
type word(width: i64);
pass lower;
```

Optional C++ behavior binds to the exact declaration symbol. For a type,
attribute, or operation this adds semantic validation; a bodyless pass receives
its executable implementation. `Compiler::bind(declaration, function)` uses the
resolved declaration as both the typed subject and stable binding key. An
interface method adds only its method name or an explicit `MethodDecl`; behavior
code never reconstructs a `declaration.method` key.

Operation type relations also belong to the Module. Generic type expressions
are structurally unified against operands, dependent named properties, and
explicit result expectations; the same solver checks text construction, C++
construction, and pass output.
`joggle::property(name, value)` supplies a named property during C++ operation
creation, so it participates in inference at the same point as `name = value`
in a text graph.
C++ behavior is reserved for value-domain semantics that the text contract
does not express, rather than duplicating operand/result type checks.

An optional behavior shared library exports one `Behavior` descriptor. Its
declared identity must equal the linked Module's complete content identity, and
its entry may only call the same direct binding API. `Compiler::load_behavior`
owns the library lifetime and attaches all bindings atomically.

## Passes and implementation queries

`pass` is the only public transformation declaration. Its implementation is
exactly one of an external C++ binding, ordered contraction rules, or a
sequence of other passes. A contraction's right side is a proper subterm of
its left side, so text-defined passes only reuse existing SSA values and
terminate by construction. Matching machinery remains private rather than
becoming a second public IR.

Implementation queries are ordinary C++ callables passed to
`Compiler::query`. Every call checks the Graph and executes the supplied
callable. Query results remain local to the implementation using them. A caller
can add an explicit cache when its domain provides a meaningful cache key.

## Graph

A `graph` is a named program stored in a Module; its body is SSA:

```joggle
graph main(%x: tensor<word<8>, [1, 4]>) -> tensor<word<8>, [1, 4]> {
  %y = relu(%x);
  return %y;
}
```

The ordinary user path is `compiler.graph("model.main")`. It resolves the
member through the Module's imports and opens a mutable `Graph`. Calling
`compiler.graph()` creates an empty graph for programmatic construction.
`Graph::inputs()`, `Graph::operations()`, and `Graph::outputs()` expose the
program boundary and directly owned operation sequence. The implementation's
root region is not public. Only a structured Operation exposes real nested
`Region` handles; a top-level Operation has no region parent. Passes that
deliberately cross those structured boundaries request the explicit preorder
snapshot from `Graph::all_operations()`.

Tools that inspect a Module use `Module::members()`. It returns the same
content-identified `Symbol` for every member kind; graph inspection does not
introduce another graph object. `compiler.graph(symbol)` opens a graph Symbol
directly, while the string overload is only the human-facing lookup path.

Opening a named member resolves its references and returns `Graph`. Text-loaded
and C++-constructed programs then use the same verifier, edits, passes, and
formatter.

Loaded, programmatically constructed, and transformed programs share one
`Graph`. A pass edits it; formatting the result yields an ordinary `graph`
member that can be loaded, verified, and passed again.

Graph construction and transformation share one nested transactional API,
`Graph::Edit`. An edit either commits a verified change or rolls back. Separate
construction and replacement machinery stays internal. The same edit can
append to the Graph or a structured region, or insert before an existing
operation;
target passes therefore preserve SSA order without a separate lowering API.
Region-local SSA bindings use lexical scope: nested bodies can capture enclosing
values, but values never escape to a parent or sibling body.
An operation's `region` parameter declares a named opaque body slot, not another
callable signature. The core owns its binding and SSA invariants; extensions may
attach domain checks without creating a second region-schema subsystem.
Omitted C++ result types are inferred by the same Module type-contract solver
used for text graphs and final pass verification. Commit runs that solver over
the complete edited Graph, so a schema-invalid transaction is never published;
optional C++ domain verifiers remain the Compiler's outer verification layer.

Formatting a committed Graph reconstructs the canonical graph declaration from
that runtime value, including deterministic SSA names, explicit types,
properties, structured regions, and region arguments. The `joggle run` command
therefore executes the same linked passes as the C++ API and publishes their
actual resulting Graph rather than a separate export IR. Its output is wrapped
as a derived canonical Module importing the exact compilation closure, so it is
again ordinary Joggle input rather than a terminal dump format.
The default derived name is `<source>_<graph>_compiled`. It separates different
graph members and the source package in one repository; running the resulting
single-graph Module again preserves that name.

## Extension boundary

Hardware descriptions, number formats, instruction sets, logic networks, and
mapping policies are expressed as ordinary Modules. A target extension chooses
its declarations and interfaces; the core supplies linking, SSA storage and
verification, atomic edits, and pass execution.

The examples exercise this boundary end to end. `bitmath.numeric_format` is the
shared contract; the independent `word` and `fixed.q` types implement it;
MiniAI tensors remain format-independent. Its `reshape` contract binds the
result shape from a named list property, while optional C++ behavior checks the
nonlinear element-count invariant. One `feedforward` Module stores the model
over both formats. The `edgevec` Module then owns lane-aware operation
schemas, a typed operation interface, and one `lower` pass that rewrites either
graph through `Graph::Edit`. Its interface defines `cycles()`, and an ordinary
query sums that target-owned meaning.

Cross-module contracts use `Module::InterfaceDecl`. An interface is a versioned
declaration with typed methods. A declaration lists conformance after `:`; the
Compiler resolves and dispatches the exact declaration/method pair.

Typed C++ is implemented by generic codecs over schema handles:
`Compiler::make`, `Type::get<T>`, `Attribute::get<T>`, typed `Compiler::bind`,
and typed `Compiler::call`. Joggle checks these uses against the linked schema
when behavior is attached.
Declaration-handle equality includes the complete canonical Module identity,
so behavior code compares resolved declarations rather than string hooks or
process-local tokens.
