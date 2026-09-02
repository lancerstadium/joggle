# Typed C++ API

Joggle's C++ path loads a canonical `.joggle` Module, resolves immutable
declaration handles, and uses generic typed conversion.

```cpp
joggle::Compiler compiler;
compiler.load("arith.joggle");
if (!compiler.link()) {
  compiler.diagnostics().print(std::cerr);
  return 1;
}

auto module = compiler.module("arith");
auto integer = module->type("integer");
auto i8 = compiler.make(*integer, 8, false);
auto width = i8->get<std::int64_t>("width");
```

`Compiler::make` encodes ordinary C++ scalars, strings, `Type`, `Attribute`, and
iterable homogeneous lists.

```cpp
auto tensor_module = compiler.module("tensor");
auto tensor_decl = tensor_module->type("tensor");
auto activation = compiler.make(*tensor_decl, *i8,
                                std::vector<std::int64_t>{1, 4});
```

`Type::get<T>` and `Attribute::get<T>` resolve parameters by schema name. They
return `std::optional<T>` for an unknown parameter, wrong requested kind, or
invalid list element.

Declaration handles compare by kind, local name, Module version, and canonical
Module digest. A behavior pass resolves its dependencies once and can compare
`operation.schema() == linear` directly; it does not dispatch through operation
name strings or process-local numeric tokens.

Interface method bindings infer their C++ result and argument types from an
ordinary non-generic callable, then check those types against the text Module
signature:

```cpp
compiler.bind(scalar, score,
  [](const joggle::Type& value,
     std::int64_t scale) -> std::optional<std::int64_t> {
    auto bits = value.get<std::int64_t>("bits");
    return bits ? std::optional<std::int64_t>(*bits * scale) : std::nullopt;
  });

auto score_value =
    compiler.call<std::int64_t>(value, "score", std::int64_t{3});
```

The explicit `bind<Result, Arguments...>` form remains available for generic or
overloaded callables whose signature C++ cannot inspect. In both forms, a
mismatch is rejected against the DSL declaration before the binding is stored.
Ordinary calls use a method name directly; resolving an interface and method
declaration explicitly is reserved for genuine name collisions and dynamic
schema tooling.

Joggle checks schema names and signatures at link, bind, construction, and call
boundaries. A behavior library's build adds one hidden identity translation
unit; handwritten code continues to include only the stable generic API.

## Graphs

A packaged graph opens directly by qualified Module member name:

```cpp
auto graph = compiler.graph("model.forward");
```

The compiler resolves the member through that Module's imports and opens a
mutable `Graph`, using the same type and bound-domain verification as
programmatic construction. Generic tools can enumerate `Module::members()`;
every entry is the same content-identified `Symbol`, including graph members.
`compiler.graph(symbol)` opens a reflected graph member without converting its
identity back to text. Every graph member opens directly as `Graph`, which can
consume passes from any Module selected into the same Compiler.

Code that imports or builds a graph directly uses the same runtime object:

```cpp
auto add = module->operation("add");
auto graph = compiler.graph();
if (!i8 || !add || !graph) {
  return 1;
}
auto edit = graph->edit();

auto lhs = edit.argument(*i8);
auto rhs = edit.argument(*i8);
auto sum = edit.append(*add, {lhs, rhs}).value();
edit.output(sum);

joggle::Diagnostics diagnostics;
if (!edit.commit(diagnostics)) {
  diagnostics.print(std::cerr);
}
```

`Graph::Edit` is the only mutation entry point for construction and passes. It
can append or insert operations, set named properties, add structured regions,
replace values, erase operations, and declare graph outputs. `append` and
`insert` use the operation's text contract to infer omitted result types,
including generics bound by named properties:

```cpp
auto reshaped = edit.append(
    reshape, {input},
    joggle::property("shape", std::vector<std::int64_t>{1, 4})).value();
```

This is the C++ form of `reshape(%input, shape = [1, 4])`; both calls feed the
same property value to the same result-type solver. An explicit result type can
still constrain a result-only generic.
`Operation::value()` is the ordinary single-result path and rejects zero- or
multi-result operations. Code implementing a genuinely multi-result contract
uses `results()` or `result(index)` explicitly; structural operations retain
their `Operation` handle for regions and properties.
If such a variable remains unconstrained, the edit reports it immediately by
throwing `std::invalid_argument` before changing the Graph.
A commit publishes the whole edit only after SSA structure and every Module
operation type contract verify. A failed commit restores the previous Graph
immediately; abandoning an active edit does the same on destruction. Replacing a
value updates ordinary uses and graph outputs together.
`Compiler::verify(graph)` additionally runs the optional bound domain verifiers.

Erasing a structured operation removes its nested regions, arguments,
operations, and results in the same transaction. The edit rejects the erase
before changing the Graph if any value in that subtree is a graph output or is
used by an operation outside the subtree.

A structured region is created in one call and directly owns its arguments and
ordered operations, matching the text form exactly:

```cpp
auto body = edit.region(scope, "body", {*i8});
auto item = body.arguments().front();
edit.append(body, *identity, {item});
```

Region arguments and operation results obey lexical dominance. Nested regions
may consume enclosing values, while a value produced in one sibling region
cannot be consumed in another; commit rejects and immediately rolls back such a
transaction. Text parsing uses the same region nesting rule for SSA names.
The string passed to `edit.region` must name a `region` slot in the owning
operation declaration. Region bodies do not form another Graph boundary;
operation-specific restrictions beyond binding and SSA structure belong in the
owner's optional verifier.

`format(graph, "compiled")` produces a canonical `graph compiled(...)`
declaration with deterministic SSA names, explicit result types, properties,
regions, and region arguments. Declaration references use their real Module
names, so the fragment can be placed in a Module that imports those Modules and
parsed back without generated metadata.

`Graph::operations()` is the direct top-level view. A pass that intentionally
crosses structured boundaries uses `Graph::all_operations()`, which returns a
preorder snapshot across nested regions without introducing another traversal
object.
`Edit::replace(operation, schema)` covers the common one-operation
lowering case while preserving the source result types and provenance. A
structured source may be replaced by a flat target operation; its body is
removed only after the source results have been redirected. General many-to-many
lowering remains explicit through `insert`, value `replace`, and `erase` in the
same transaction.

`Graph::inputs()`, `Graph::operations()`, and `Graph::outputs()` expose the
program boundary and its top-level sequence directly. Nested `Region` objects
appear only beneath structured operations; no public root Region sits between
the Graph and its operations.
