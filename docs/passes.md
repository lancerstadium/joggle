# Compiler functions

Joggle has no `pass` declaration kind. Loading, conversion, analysis,
transformation, and emission are ordinary `fn` declarations over typed values.
“Pass” is a use of a function, not a second object model.

```joggle
fn read(input: bytes) -> ir.module;
fn optimize(input: ir.module, target: device.target) -> ir.module;
fn cost(input: ir.module, target: device.target) -> cost.report;
fn emit(input: ir.module, target: device.target) -> bytes;
```

Here `ir.module`, `device.target`, and `cost.report` are types owned by
installable Modules. The core does not assign frontend/backend status or a
lowering direction to them. A bridge Module may define conversions in either
direction.

The shipped `ir` Module declares the whole-program value used by ordinary
compiler functions:

```joggle
module ir@1.0.0 {
  type module();
}
```

Its standard C++ representation is `joggle::ir::Module`, a copy-on-write named
set of executable `joggle::Function` values. Prelude `function` remains a
low-level convenience for callbacks that intentionally operate on one
Function:

```joggle
fn canonicalize(input: function) -> function;
```

Neither type introduces a `Graph`: data-flow and control-flow relations belong
to each `Function`. `ir.module` only supplies program ownership across multiple
functions, including generated helper functions and future lifted closures.
It serializes as a normal versioned `.joggle` Module, so pass output can be
checked, installed, replayed, or passed to another tool without an adapter
format.

## Binding

A bodyless function can receive a typed C++ implementation. The binding is
attached to the declaration's full Module identity:

```cpp
compiler.bind(*canonicalize,
  [](joggle::Function& function,
     joggle::Diagnostics& diagnostics) {
    auto edit = function.edit();
    // Transform Blocks, Instructions, and Values.
    return edit.commit(diagnostics);
  });
```

The current invocation convenience is `Compiler::run`. It resolves a
`FunctionDecl`; it does not create a separate pass registry visible to users.
The unified staged evaluator will make an ordinary function call the only
semantic invocation path.

## Composition

Composition uses an ordinary function body:

```joggle
fn compile(input: bytes, target: device.target) -> bytes {
  program = read(input);
  optimized = optimize(program, target);
  return emit(optimized, target);
}
```

Calls use the same overload and staging rules as program computations. Known
arguments execute a body or registered implementation. If a value must remain
in the generated program, the call residualizes. Prefix `@` requires the
result to be Known but does not select another function namespace.

## Rewrites and analyses

Pattern rewriting, dominance, cost estimation, scheduling, simulation, and
emission are libraries over `Function` or Module-defined handle types. The
core language has no privileged rewrite form; a Module may expose pattern
types and ordinary functions as a library without changing ownership.

Mutating compiler functions use one transactional `Function::Edit`. Failure,
diagnostics, exceptions, or failed verification restore the input checkpoint.
Analyses consume an immutable `Function` and return ordinary Module-declared
results. CFG and def-use are relations already present in that Function, not
separate handles or owners.

The minimal structural relations are queried directly:

```cpp
function.predecessors(block);
function.users(value);
function.has_uses(value);
function.dominates(a, b);
```

This is sufficient for local rewriting and for building cached liveness,
loops, scheduling, or cost analyses. Such a cache indexes a committed Function
state; it is not a mutable mirror of the IR and becomes stale after an edit.
