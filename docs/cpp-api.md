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
auto i8 = compiler.make("i8");
auto f32 = compiler.make("f32");
```

Prelude type instances are compiler-owned and parameterless, so C++ code
constructs them directly by their language spelling. Their declarations still
live in the canonical `prelude` Module; there is no parallel C++ schema table.
No explicit Prelude lookup is required.
`compiler.module("prelude")` exposes that Module for reflection.
`compiler.modules()` enumerates the lockable package closure and therefore
omits the ambient Prelude, which is versioned with the language installation
rather than resolved from a project package root.
Parameterized user types still use their declaration handle:

```cpp
auto packed = module->type("packed");
auto i4 = compiler.make(*packed, std::int64_t{4});
auto width = i4->get<std::int64_t>("width");
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

An operation declaration reflects its three semantic parts directly:

```cpp
auto operands = add->operands();
auto properties = add->properties();
auto results = add->results();
```

Operands and results are `PortDecl` values with a name, variadic flag, and the
same public immutable `Module::Expression` used by parsing, formatting, linking,
and type inference. Properties use `ParameterDecl`; its `domain` member is the
compile-time value domain, while `PortDecl::type` is an SSA type expression.
`Expression::Kind::List` denotes a list value expression;
`Expression::list_domain(element)` constructs the distinct `list<element>`
domain expression.
There is no synthetic
closed parameter-kind enum and no private duplicate result-type contract.
`OperationDecl::notation()` returns an optional binary spelling used by the
text graph expression resolver; after resolution the runtime Graph contains
the ordinary declaration handle.

Pure compile-time functions are reflected without an execution callback:

```cpp
auto align = module.function("align");
auto inputs = align->inputs();
auto result_domain = align->results().front().domain;
auto body = align->body();
```

`FunctionDecl::body()` is the same immutable `Module::Expression`. Function
definitions are Module content, participate in the canonical digest, and are
evaluated by the checked type-expression engine rather than a C++ registry.

A `TypeDecl` reflects its derived parameters directly:

```cpp
for (const auto& parameter : packed->derived_parameters()) {
  std::cout << parameter.name << '\n';
  inspect(parameter.value);
}
```

The parameter domain is owned by an interface field. Constructor and derived
parameters are both read with `Type::get`; only constructor parameters enter
instance identity. Derived values may feed operation result parameters during
normal type inference and are canonical Module content.

Interface method bindings infer their C++ result and argument types from an
ordinary non-generic callable, then check those types against the text Module
signature:

```cpp
compiler.bind(operation, latency,
  [](const joggle::Operation& value) -> std::int64_t {
    return estimate_latency(value);
  });

auto cycles = compiler.call<std::int64_t>(value, latency);
```

The explicit `bind<Result, Arguments...>` form remains available for generic or
overloaded callables whose signature C++ cannot inspect. In both forms, a
mismatch is rejected against the DSL declaration before the binding is stored.
Type interfaces use declarative fields and `Type::get`, not method callbacks.
Attribute and operation calls may use a method name directly; resolving an
interface and method
declaration explicitly is reserved for genuine name collisions and dynamic
schema tooling.

Joggle checks schema names and signatures at link, bind, construction, and call
boundaries. A behavior library's build adds one hidden identity translation
unit; handwritten code continues to include only the stable generic API.

## Typed passes

The Module declares the pass function type:

```joggle
pass read: bytes -> graph;
pass count: graph -> int;
pass emit: graph -> bytes;
pass compile: bytes -> bytes = read, optimize, emit;
```

C++ binds ordinary callables with matching argument and return types:

```cpp
compiler.bind(*read,
  [](joggle::Compiler& compiler,
     const joggle::Bytes& source) -> std::optional<joggle::Graph> {
    return source.empty() ? std::nullopt : compiler.graph();
  });

compiler.bind(*count,
  [](const joggle::Graph& graph) -> std::int64_t {
    return static_cast<std::int64_t>(graph.all_operations().size());
  });

auto graph = compiler.run<joggle::Graph>(*read, bytes);
auto nodes = compiler.run<std::int64_t>(*count, *graph);
```

`Bytes` is `std::vector<std::byte>`. A pass returning `std::optional<T>` fails
when it returns empty; a no-result pass binds a `void` callable and is invoked
with the Boolean `run(pass, arguments...)` form. A mutable `Graph&` is used for
`graph -> graph`; analyses and encoders normally accept `const Graph&`.

The whole composed pass is one atomic execution. Existing Graph inputs are
restored if any nested pass returns failure, reports a diagnostic, throws, or
produces an invalid Graph. A Graph produced by a reader is simply discarded on
failure. The erased value representation remains private and uses a fixed
discriminated layout so behavior libraries do not depend on cross-DSO RTTI.

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

`Graph::Edit` is the transitional mutation entry point for construction and
passes. It can append or insert operations, set named properties, replace
values, erase operations, and declare outputs. `append` and
`insert` use the operation's text contract to infer omitted result types,
including generics bound by named properties:

```cpp
auto reshaped = edit.append(
    reshape, {input},
    joggle::property("shape", std::vector<std::int64_t>{1, 4})).value();
```

This is the C++ form of `reshape(input, shape = [1, 4])`; both calls feed the
same property value to the same result-type solver. An explicit result type can
still constrain a result-only generic.
`Operation::value()` is the ordinary single-result path and rejects zero- or
multi-result operations. Code implementing a genuinely multi-result contract
uses `results()` or `result(index)` explicitly; the instruction handle retains
its named properties.
If such a variable remains unconstrained, the edit reports it immediately by
throwing `std::invalid_argument` before changing the Graph.
A commit publishes the whole edit only after SSA structure and every Module
operation type contract verify. A failed commit restores the previous Graph
immediately; abandoning an active edit does the same on destruction. Replacing a
value updates ordinary uses and graph outputs together.
`Compiler::verify(graph)` additionally runs the optional bound domain verifiers.

Erasing an operation removes its results in the same transaction. The edit
rejects the erase before changing the function if a result is an output or has
a live use.

Nested Region construction no longer exists. Control flow belongs to
Function-owned sibling Blocks and typed successor edges. Until that public API
lands, the remaining `Graph` object supports only a single flat instruction
sequence; `operations()` and `all_operations()` intentionally return the same
ordered snapshot.

`format(graph, "compiled")` currently produces canonical straight-line
function syntax with deterministic local names, explicit result types, and
named arguments. The formatter uses the declarations' real Module names.
`Edit::replace(operation, schema)` covers the common one-operation
conversion case while preserving the source result types and provenance. A
source may be replaced by another instruction only after the source results
have been redirected. General many-to-many
conversion remains explicit through `insert`, value `replace`, and `erase` in the
same transaction.

`Graph::inputs()`, `Graph::operations()`, and `Graph::outputs()` expose the
transitional function boundary and its flat sequence directly. There is no
public or internal Region between that boundary and its instructions.
