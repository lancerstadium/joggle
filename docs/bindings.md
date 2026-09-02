# C++ behavior bindings

The `.joggle` Module defines the schema. C++ attaches executable behavior to
its exact, content-addressed declarations.

## Verifiers

```cpp
compiler.bind(word,
  [](const joggle::Type& type, joggle::Diagnostics& diagnostics) {
    const auto width = type.get<std::int64_t>("width");
    if (!width || *width <= 0) {
      diagnostics.report("word width must be positive");
      return false;
    }
    return true;
  });
```

Type and attribute verifiers run when structural instances are constructed.
Operation verifiers run through `Compiler::verify` and after every Compiler-run
pass. They are optional refinements: basic arity, kind, ownership, SSA, and
dominance checks are always active; attaching a verifier adds domain-specific
rules for that exact declaration. No verifier marker appears in the Module.
For a graph loaded from text, diagnostics emitted without an explicit source
are automatically attached to the operation's `.joggle` source range. Target
operations inserted in place of a source operation inherit that provenance.

The declaration is resolved from the Module once. Its stable symbol is the
binding key; the binding table never stores a lookup string.

The callable's first parameter selects `type`, `attr`, `op`, or `pass`, so the
same form covers every declaration kind. Interface methods add one reference:

```cpp
compiler.bind(word, "storage_bits", method);
compiler.bind(q, "storage_bits", method);
```

The method name is resolved across the interfaces that the declaration already
conforms to, including imported interfaces. If two such interfaces declare the
same method name, Joggle diagnoses the ambiguity and accepts an explicit
`interface.method` selector. Ordinary callables have their method
arguments and result inferred and checked against the text signature. They
accept `Diagnostics&` last only when they need to report a domain error.
Generic or overloaded callables can resolve declaration handles and use the
explicit typed `bind<Result, Arguments...>` API.

All behavior callbacks follow one success rule: a returned value or `true` is
accepted only when the callback emits no diagnostic. A diagnostic suppresses a
type, attribute, interface-method, or query result; for a pass it triggers
the graph checkpoint rollback. This prevents a callback from simultaneously
reporting failure and publishing a usable-looking value.

## Optional behavior libraries

A Module may keep all behavior in the host application, or place the same
bindings in a shared library. The library exports one versioned entry and a
small descriptor:

```cpp
namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto word = module.type("word");
  if (!word) {
    diagnostics.report("behavior does not match its Module");
    return false;
  }
  compiler.bind(*word,
    [](const joggle::Type& type, joggle::Diagnostics&) {
      auto width = type.get<std::int64_t>("width");
      return width && *width > 0;
    });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
```

`JOGGLE_EXPORT_BEHAVIOR` emits the one versioned entry point and descriptor;
the author supplies only the ordinary binding function. It is a generic ABI
macro from `joggle/behavior.h`, not generated Module code.

The build attaches the exact canonical Module identity without generating or
including a header:

```cmake
joggle_add_behavior(bitmath_behavior
  MODULE bitmath.joggle
  SOURCES bitmath_behavior.cpp
)
```

This creates one hidden translation unit containing the canonical identity and
links it into the plugin. Handwritten C++ continues to use the generic API.

After linking the schema closure, the host loads the library explicitly:

```cpp
if (!compiler.load_behavior("bitmath", "libbitmath_behavior.so")) {
  compiler.diagnostics().print(std::cerr);
}
```

For a content-addressed installed package, the shorter overload locates the
single behavior for the current target and verifies its file digest:

```cpp
compiler.load_behavior("bitmath");
```

If a lock is active, both overloads require a matching `behavior` lock entry
and verify the selected binary SHA-256 before opening the library.

The loader checks the ABI version, descriptor size, target, exact Module
identity, and entry pointers before calling the plugin. Loading the same exact
behavior is idempotent. A false return, exception, or new diagnostic rolls back
every binding made by that load. Library handles outlive captured verifier,
method, and pass functions.

Behavior functions use the Joggle C++ API and therefore share its major
version, C++ standard library, and toolchain ABI. Loading remains explicit
because executing an installed shared library is a trust decision.

The shipped Bitmath, MiniAI, and EdgeVec behavior sources are complete
examples. Bitmath binds numeric-format methods and word legality; MiniAI
verifies tensor formats and dense-layer shape equations through the imported
Bitmath interface; EdgeVec implements a target-owned lowering pass and typed
lane query. None of these sources duplicates declarations from its text Module.

## Implementation queries

```cpp
auto count_nodes = [](const joggle::Graph& graph) {
  return graph.all_operations().size();
};

auto result = compiler.query(graph, count_nodes);
```

The result type is inferred from the callable. A query accepts
`Diagnostics&` last only when it needs to report a domain error. Each call
executes only after the current Graph passes structure, Module type contracts,
and bound domain verification. Applications may memoize the result explicitly
using a domain-specific key.

## Passes

```cpp
compiler.bind(simplify,
  [&count_nodes](joggle::Compiler& compiler, joggle::Graph& graph,
               joggle::Diagnostics& diagnostics) {
    auto count = compiler.query(graph, count_nodes);
    if (!count) {
      return false;
    }

    auto edit = graph.edit();
    // mutate through the same Graph::Edit API
    return edit.commit(diagnostics);
});
```

Whole-graph passes can iterate `graph.all_operations()` directly. It returns a
preorder snapshot across nested regions, so a pass may inspect it before opening
an edit. `graph.operations()` returns only the Graph's directly owned, top-level
operations. For a same-result-shape lowering, `edit.replace(operation, target)`
inserts the target with the source operands and result types, redirects every
result use including graph outputs, and erases the source as one edit action.
Target properties begin with the target declaration's defaults and can then be
set through `edit.set` before commit.

`Compiler::run` verifies the input, takes a whole-Graph checkpoint, invokes the
binding, and verifies the result. Queries are requested where the implementation
actually needs them. A false return, new diagnostic, exception, or failed
verification restores the checkpoint—even if the pass committed one or more
inner Edits before failing.

The Compiler coordinates direct declaration-bound methods and Graph edits.

A pass that does not call back into the Compiler can omit that parameter:

```cpp
compiler.bind(lower,
  [](joggle::Graph& graph, joggle::Diagnostics& diagnostics) {
    auto edit = graph.edit();
    // mutate through the same Graph::Edit API
    return edit.commit(diagnostics);
  });
```

Passes with `{ operation(...) => ...; }` bodies execute their rules directly.
Passes with `= first, second;` bodies compose other passes. Neither accepts a
C++ binding. All three forms share one whole-Graph checkpoint and final
verification boundary.
