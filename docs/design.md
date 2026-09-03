# Joggle architecture

This document fixes the intended public architecture. When migration notes in
other documents disagree with it, this document and
[the execution model](execution-model.md) take precedence.

## Purpose

Joggle is a lightweight C++ compiler framework for people co-designing AI
software and hardware. It supplies a small typed language, an editable IR, a
residualizing evaluator, deterministic verification, and package identity. It
does not prescribe a tensor stack, device model, lowering direction, scheduler,
or code generator. Those are installable Modules.

The design has four public concepts:

- `Module`: a versioned schema package for types and function declarations;
- `Function`: a signature plus zero or more Blocks;
- `Compiler`: Module resolution, host binding, evaluation, and invocation;
- values: either Known to the compiler or Residual in a Function.

An executable artifact containing several named Functions is the ordinary
Module-declared value `ir.module`, represented by `joggle::ir::Module`. It is
not a fifth semantic layer: it supplies ownership and transport, while each
Function still owns all executable graph structure.

There is one callable declaration, `fn`. Loading, conversion, transformation,
analysis, scheduling, simulation, and emission are ordinary typed functions.
There is no `op`/`pass` split and no fixed frontend/backend pipeline.

## One execution engine

Joggle does not have a type-expression interpreter, a pass interpreter, and a
program builder with independently evolving semantics. One staged evaluator
consumes the same function body and produces one of two outcomes for each
value:

```text
Known(type, host value)       execute now
Residual(type, IR value)      retain in the current Function
```

A source call first resolves one `fn` declaration. If its required inputs are
Known and an implementation is available, the evaluator executes it. Otherwise
it emits an ordinary call when residual execution is valid. `@expression` only
rejects the second outcome; it is not a different invocation mechanism.

The implementation separates policy from storage without duplicating language
semantics:

- the evaluator owns calls, literals, bindings, `if`, `while`, `return`, and
  deterministic execution limits;
- the specializer supplies residual Block and Instruction construction when an
  evaluated value is not Known;
- the type solver consumes Known values needed by type expressions, but does
  not evaluate a second subset of the language.

This boundary is important for compiler code. An extension declares a normal
type, associates it with a copyable C++ representation, and binds normal
external functions:

```joggle
type module();
fn profitable(input: module, target: target) -> bool;
fn fuse(input: module) -> module;

fn optimize(input: module, target: target) -> module {
  if profitable(input, target) {
    input = fuse(input);
  }
  return input;
}
```

`module` and `target` are Known host objects during compiler invocation, so the
branch executes in the compiler. Extension authors implement only
`profitable` and `fuse`; they do not register an evaluator, control-flow node,
or pass class. The same body may call more text-defined functions. Calls are
type checked at the Module boundary, host exceptions become diagnostics, input
artifacts are checkpointed, and step/depth budgets make failure deterministic.

Known computation under Residual control is deliberately constrained. A
guarded native function is not speculatively executed. A hermetic binding may
execute on both paths, but a path-dependent opaque host object cannot cross a
Residual join unless its type provides an ordinary materialization function or
both paths preserve the same Known value. The evaluator never invents a target
representation for a compiler object.

## Ownership

The two ownership trees are deliberately separate:

```text
joggle::Module                   joggle::ir::Module
  TypeDecl / FunctionDecl          named joggle::Function
                                     Block
                                       Instruction
                                       Terminator
```

The left tree is an installable schema package. The right tree is a
transformable executable artifact. Instructions in the right tree reference
resolved Function declarations from the left tree; neither tree contains an
owning `Graph` or `Region` object.

Within an executable Function the hierarchy is:

```text
Function
    Block
      Instruction
      Terminator
```

A Function has exactly one entry Block. A Block owns an ordered instruction
sequence and exactly one terminator. An Instruction may produce zero or more
Values. A Value is a Function parameter, Block argument, or Instruction result.

Instructions never own Blocks. Consequently the core has no nested `Region`
container, no region/yield protocol, and no operation-specific dominance rule.
Nested source code is a closure and normalizes to a Function whose captures are
explicit parameters.

Straight-line functions naturally form a def-use graph. Multiple Blocks add a
control-flow graph. These are relationships over Function-owned objects, not an
additional `Graph` owner or value domain. Algorithms consume the same Function;
cached predecessors, uses, dominance, liveness, or traversal order are analysis
results rather than alternative graph APIs.

The C++ surface exposes the irreducible reverse questions directly on a
Function: predecessors of a Block, Instruction users of a Value, whether a
Value is used by either an Instruction or terminator, and dominance. Query
answers are ordinary Block/Instruction handles into that Function. More
expensive or domain-specific facts remain library analyses and may be cached
against a Function snapshot.

## Function IR

The core instruction contract is deliberately small:

```text
Instruction
  callee       resolved Function symbol
  arguments    one declaration-ordered sequence of Known/Residual Values
  results      typed Values

Block
  arguments    typed Values received on incoming edges
  instructions ordered Instructions
  terminator   return | jump | branch
```

Core terminators are:

```text
return values...
jump target(arguments...)
branch condition, true_target(arguments...), false_target(arguments...)
```

Every successor edge carries exactly the number and types required by its
target Block arguments. Merge values therefore need neither phi instructions
nor yield operations. The verifier checks at least:

1. one entry Block, appearing first and having no Block arguments;
2. unique Block and Value names in textual form;
3. exactly one valid terminator per Block;
4. existing successor targets and exact edge arity/type;
5. reachability from entry;
6. dominance of every argument use;
7. exact return arity/type against the Function signature;
8. exact call contracts against the resolved callee.

Edits are transactional. A commit publishes a new state only after structural,
type, dominance, and extension verification succeeds. Failed or abandoned
edits preserve the previous Module state.

## Source control flow

Authors normally write structured control flow:

```joggle
fn choose<T: type>(condition: i1, lhs: T, rhs: T) -> T {
  value = if condition {
    lhs
  } else {
    rhs
  };
  return value;
}
```

The parser elaborates a residual condition to Blocks and typed edges. A
canonical low-level escape form exists so arbitrary pass output can still
round-trip without a second file format:

```joggle
fn choose<T: type>(condition: i1, lhs: T, rhs: T) -> T {
  entry():
    branch condition, yes(), no();

  yes():
    jump merge(lhs);

  no():
    jump merge(rhs);

  merge(value: T):
    return value;
}
```

The formatter prefers structured syntax when a CFG can be represented without
changing meaning and uses explicit Blocks otherwise. Both forms denote the
same Function IR; neither introduces a Graph or Region declaration.

The statement form may contain several calls and rebind outer lexical names.
The two arms are instantiated from the same incoming environment and compatible
live-out rebindings become Block arguments automatically. A missing `else`
uses the incoming binding on its edge. The IR remains SSA even though source
authors use ordinary assignment syntax.

An early `return` is a structured control transfer, not a nested terminator
object. Elaboration terminates the selected sibling Block. A surviving arm is
the continuation directly; two returned arms need no merge. This rule is
identical under Known control except that only the selected arm exists in IR.
The syntax tree stores structured returns as statements; explicit low-level
Blocks alone carry explicit terminators. Both forms normalize to terminators in
the owning Function.

## Residualizing evaluation

Type and availability are independent:

```text
Known(host value)
Residual(IR Value)
```

Evaluation of an ordinary `fn` proceeds as follows:

1. A language body is interpreted and specialized with the supplied mixture of
   Known and Residual arguments.
2. A host binding runs when its required arguments are Known.
3. Otherwise a residualizable call becomes an Instruction.
4. prefix `@` requires the result to be Known; it does not select another
   function kind or evaluator.

For a Known `if` condition, both branches are checked but only the selected
branch executes. For a Residual condition, both branches elaborate into the
CFG. Equal path-independent Known results remain Known. Different Known values
are materialized on their edges and merge as Residual. Distinct host-only
values that cannot be materialized are a staging error.

Native bindings are guarded by default and cannot execute below Residual
control flow. A binding explicitly marked `HostEvaluation::Hermetic` promises
deterministic execution without observable host effects and may execute while
both residual arms are elaborated. This is a property of the C++
implementation, not of the residual target call, so the source language needs
no second effect or function syntax. Hermetic calls may be memoized once their
host representations provide stable equality and hashing. Evaluation has
deterministic limits for steps, recursion, and allocation; exhausting a limit
is an error, never an implicit change of program meaning.

Known values cross a Residual boundary through an ordinary visible function
implementing `prelude.literal`. Selection uses the same overload constraints
and expected-result type solver as every other call. The resulting Instruction
therefore remains visible to rewrites and emitters; the core has no privileged
constant operation and no target-specific materializer callback.

Loops use the same mechanism. Known control may execute within the configured
budget. Residual control creates header, body, and exit Blocks; loop-carried
values are Block arguments. `continue` and `break` become ordinary edges to the
header and exit with those same values. When their path is Known, they are
handled by the evaluator without creating an edge.

Structured elaboration returns a set of typed control outcomes rather than one
`(kind, block)` pair: fallthrough paths, breaks, and continues. Every path owns
a snapshot of its lexical bindings and residual-control depth. This permits a
Residual decision inside a finite Known loop to multi-version the remaining
continuation without choosing a target representation for Known state. If the
paths later reach a typed boundary, the ordinary `prelude.literal` resolution
materializes each value there. If they form a runtime-dependent cycle, finite
specialization detects the repeated staged state and adds a backedge to the
Block representing its first occurrence. Known state is thereby encoded in
control location; it does not acquire a target width or format. Computed Known
conditions need no special replay rule.

Repeated-state detection is intentionally structural and conservative. Values
are equal when they are the same Known payload or SSA value, or when they are
the same result position of the same `prelude.literal` function with
pairwise-identical operands. It does not equate arbitrary calls or claim
general program equivalence. State that continually creates new Residual values
does not close until a future extension supplies a sound quotient analysis.

## Extension boundary

A Module may declare new types, attributes, interfaces, and functions. C++ can
attach a host representation to a declared type and a callable to a declared
Function symbol. The binding is not a second declaration and does not create a
second overload set.

One binding serves two cases:

- Known inputs: invoke the callable and obtain a Known result;
- Residual inputs: keep a call to the same Function when its values have an IR
  representation.

Language-defined bodies support mixed-stage partial evaluation. External
functions without a binding are still legal when they can remain Residual.
Host-only values need no stage annotation; they simply cannot cross a residual
edge unless a visible Module supplies a matching `prelude.literal` function.

Native scalar formats, tensors, buffers, FPGA bit layouts, custom instructions,
device descriptions, cost models, cycle simulators, and emitters are ordinary
Modules. The trusted kernel contains only parsing, identity, types, Functions,
Blocks, Values, overload resolution, evaluation, verification, diagnostics,
and binding hooks.

## Module identity

For canonical source `C(M)`, Module identity is:

```text
(name, semantic version, SHA-256(C(M)))
```

Imports resolve to that full identity. A Function symbol includes its owning
Module identity and overload discriminator. Host behavior binds to the exact
symbol; a string name alone is never a persistent binding key.

Canonical formatting is idempotent:

```text
format(parse(format(parse(source)))) = format(parse(source))
```

Comments, paths, host addresses, and registration order do not affect content
identity.

## Implementation state

The owning C++ IR is Function/Block/Instruction/Value. The nested `Region` API
and storage, the owning `Graph` type, and their source declarations have been
removed. Blocks have typed arguments and terminators; structural verification
checks reachability, dominance, successor edges, and all function exits. The
public Function now provides predecessor, use, and dominance queries without
constructing an alternative graph object.

Concrete named Functions can now enter IR as typed callable Values. Their
signature is verified against the referenced Module declaration, they dominate
all Blocks, and they serialize as qualified symbols rather than synthetic
constant Instructions. This establishes the reference model needed for later
closure lifting without reintroducing nested Region ownership.

Function-value resolution is bidirectional. The operation solver first uses
available ordinary operand types and expected results to resolve any missing
callable operand types, then uses those types to select overloads or specialize
generic Functions before performing the final complete call check. Canonical
formatting emits a typed local binding for a selected Function value, so a
round trip cannot lose an overload decision.

The `function` kernel domain remains the bootstrap representation for direct
`joggle::Function` callbacks. Parameterless Module-declared types can now
register ordinary copyable C++ host representations and flow through composed
compiler functions. Parameterized host values provide an ordered tuple
projection; their concrete Type travels with the payload and is checked by the
same dependent-type solver used for IR calls.

Function bodies and every local or Block-argument type annotation now use the
same expression AST and parser. Type and attribute constructors, lists,
operators, calls, conditionals, and prefix `@` therefore have one meaning in
signatures and bodies. The residualizing evaluator—not a restricted annotation
parser—decides which parts execute and which parts become IR.
