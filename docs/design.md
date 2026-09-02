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

- `Module`: a versioned namespace and ownership unit for types and functions;
- `Function`: a signature plus zero or more Blocks;
- `Compiler`: Module resolution, host binding, evaluation, and invocation;
- values: either Known to the compiler or Residual in a Function.

There is one callable declaration, `fn`. Loading, conversion, transformation,
analysis, scheduling, simulation, and emission are ordinary typed functions.
There is no `op`/`pass` split and no fixed frontend/backend pipeline.

## Ownership

The complete program hierarchy is:

```text
Module
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
4. `@(expression)` requires the result to be Known; it does not select another
   function kind or evaluator.

For a Known `if` condition, both branches are checked but only the selected
branch executes. For a Residual condition, both branches elaborate into the
CFG. Equal path-independent Known results remain Known. Different Known values
are materialized on their edges and merge as Residual. Distinct host-only
values that cannot be materialized are a staging error.

Pure compile-time calls may be memoized. Observable host effects cannot be
speculatively executed below Residual control flow: they must residualize or
produce a staging diagnostic. Evaluation has deterministic limits for steps,
recursion, and allocation; exhausting a limit is an error, never an implicit
change of program meaning.

Loops use the same mechanism. Known control may execute within the configured
budget. Residual control creates header, body, and exit Blocks; loop-carried
values are Block arguments.

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
edge unless their Module supplies a materialization representation.

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
checks reachability, dominance, successor edges, and all function exits.

The `function` kernel domain remains the bootstrap representation for direct
`joggle::Function` callbacks. Parameterless Module-declared types can now
register ordinary copyable C++ host representations and flow through composed
compiler functions. Parameterized host values and their concrete-Type
projection remain future work.

Likewise, the current separate compile-time-expression and dataflow-body parser
paths are temporary. The final parser produces one function syntax tree, and
the residualizing evaluator decides which parts execute and which parts become
IR.
