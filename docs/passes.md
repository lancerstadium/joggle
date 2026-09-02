# Passes

Every Joggle transformation is declared with `pass`. Its syntax selects one of
three implementation forms.

## Three forms

A rule pass contracts a value expression to one of its proper subexpressions:

```joggle
pass canonicalize {
  identity($input) => $input;
}
```

A bodyless pass is implemented by its C++ behavior package:

```joggle
pass lower;
```

A sequence pass composes local or imported passes:

```joggle
pass optimize = simplify, math.canonicalize, target.lower;
```

The linker resolves every operation and pass through the importing Module's
versioned, digest-pinned closure and rejects pass cycles. The declaration form
fully specifies how the pass executes.

## Contraction rules

Both sides of `=>` use the same recursive term syntax:

```text
$value
module.operation(term, ...)
```

For example, the rule below removes any positive-depth stack of adjacent
`relu` operations one layer at a time:

```joggle
pass simplify {
  relu(relu($x)) => relu($x);
}
```

The right-hand term must be a proper structural subterm of the left-hand term.
Consequently a rule cannot invent an operation, infer a result type, or expand
the graph. It only selects a Value already proved by the match and erases the
root operation. This restriction gives the compact rule form its termination
guarantee.

An unqualified operation belongs to the pass's Module. A qualified operation
must belong to an imported Module. Every operation term must have exactly one
value result and no regions. Repeated variables, and repeated compound terms,
must denote the exact same SSA Value. This makes structural sharing explicit:

```joggle
pass deduplicate {
  pair($x, $x) => $x;
}
```

`$name` is a pattern variable. It is deliberately different from the `%name`
notation used by SSA values inside a `graph`; a rule is a structural pattern,
not a second graph body.

Replacement requires exact structural Type equality. Operations with regions
are excluded from this contraction core. Because the right side is a proper
subterm and each match erases the root Operation, greedy execution terminates
without an arbitrary iteration limit. Rules run in source order, each to a
fixed point.

Construction and target-specific scheduling use a bodyless C++ pass. Text rules
cover the common terminating case of selecting an existing matched value.

## C++ passes and queries

```cpp
compiler.bind(*lower,
  [](joggle::Compiler& compiler, joggle::Graph& graph,
     joggle::Diagnostics& diagnostics) {
    auto costs = compiler.query(graph, estimate_costs);
    if (!costs) {
      return false;
    }

    auto edit = graph.edit();
    // mutate through Graph::Edit
    return edit.commit(diagnostics);
  });

compiler.run(graph, "edgevec.lower");
```

`Compiler::query` infers the result type, performs complete structural, type,
and bound domain verification, and then invokes the callable. The result stays
local to the pass implementation; callers can cache it using a domain-specific
key when useful.

## Atomic execution

`Compiler::run(graph, "module.name")` resolves the pass to its stable declaration
and verifies the input Graph before executing it. One checkpoint then covers
the complete pass, including every rule and every nested pass in a sequence. A
false return, new diagnostic, exception, incompatible replacement, failed edit,
or final verification failure restores the Graph.

All three forms execute through the same `Module::PassDecl`, `Graph::Edit`,
verification, and rollback path.
