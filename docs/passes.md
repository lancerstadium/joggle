# Compiler functions and pipelines

Joggle has no source-level `pass` declaration. Compiler work is expressed by
ordinary typed functions:

```joggle
fn read(input: bytes) -> graph;
fn optimize(input: graph) -> graph;
fn cost(input: graph) -> int;
fn emit(input: graph) -> bytes;
```

`graph` and `bytes` are compiler domains. They do not assign frontend,
backend, or lowering status to a function. A Module may define conversions in
either direction, an analysis returning a scalar, or a target-specific emitter
without asking the core for a new pipeline category.

## External implementation

A bodyless function can receive a C++ binding:

```joggle
fn convert(input: graph) -> graph;
```

```cpp
compiler.bind(*convert,
  [](joggle::Graph& graph,
     joggle::Diagnostics& diagnostics) {
    auto edit = graph.edit();
    // Transform through the ordinary Graph API.
    return edit.commit(diagnostics);
  });

compiler.run(graph, "bridge.convert");
```

The current C++ entry remains named `run` while the public invocation API is
being unified; it resolves a `FunctionDecl`, not a separate pass object.

## Composition

Composition uses the same block body as every implemented function:

```joggle
fn compile(input: bytes) -> bytes {
  return @(emit(optimize(read(input))));
}
```

The signature of every adjacent call must match. Calls resolve through the
declaring Module and its imports, and recursive composition is rejected.

## Rewrite body

The compact terminating rewrite form is one possible function body:

```joggle
fn canonicalize(input: graph) -> graph {
  return rewrite(input) {
    cast($value) => $value;
    relu(relu($value)) => relu($value);
  };
}
```

`$name` is a pattern metavariable. It is intentionally distinct from an
ordinary local variable in a dataflow function.

Both sides of `=>` are recursive terms. The replacement must be a proper
structural subterm of the match. A rule therefore selects a value already
proved by the match and removes at least one operation; it cannot invent nodes
or expand the graph. This gives the compact rewrite form a termination
guarantee.

Rules execute in source order, each to a fixed point. Operation terms must have
one program-value result and no nested regions. Repeated variables and repeated
compound terms must denote the same runtime value.

Construction, scheduling, and expanding rewrites belong in an external C++
function, where the full transactional `Graph::Edit` API is available.

## Atomic invocation

Invocation verifies its inputs, opens one checkpoint, executes the selected
function and nested calls, and verifies the result. A false return, diagnostic,
exception, invalid edit, incompatible rewrite, or failed final verification
restores the input graph.

External, composed, and rewrite-bodied functions therefore share:

- one declaration identity;
- one C++ binding table;
- one typed call path;
- one graph transaction boundary;
- one diagnostic and rollback policy.

This is the reason Joggle uses a body form on `fn` instead of a second pass
language.
