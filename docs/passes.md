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

The current implementation uses the reflected Prelude `function` type as a
bootstrap host representation for whole-Function callbacks:

```joggle
fn canonicalize(input: function) -> function;
```

This does not introduce a `Graph`: the C++ value is the ordinary owning
`Function`. The bootstrap restriction will disappear when Module-declared
types can register host representations directly.

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
in the generated program, the call residualizes. `@(expression)` requires the
result to be Known but does not select another function namespace.

## Rewrites and analyses

Pattern rewriting, dominance, cost estimation, scheduling, simulation, and
emission are libraries over `Function` or Module-defined handle types. A
rewrite DSL may be provided by a Module, but it does not change the core
ownership hierarchy.

Mutating compiler functions use one transactional `Function::Edit`. Failure,
diagnostics, exceptions, or failed verification restore the input checkpoint.
Analyses can consume immutable `Function`, `CFGView`, or `DefUseView` handles
without becoming owners of the program.
