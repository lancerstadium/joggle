# Joggle language

One `.joggle` file defines one versioned Module. A Module is packaging and
namespace, not a second program IR. Its `graph` members are the programs;
types, attributes, operations, and passes are reusable declarations those
programs may reference.

Most model authors need only `import` and `graph`. Authors creating a new
numeric format, hardware vocabulary, or lowering add `type`, `attr`, `op`, or
`pass`. `interface` is optional and only expresses a cross-Module contract.
Names referenced by a graph or pass resolve through the current Module and its
imports, so the program text itself determines its declaration dependencies.

## Module header

```joggle
joggle 1;

module arith@1.0.0 {
  import core@^1.2.0 as c;
}
```

`joggle 1;` is the file-language version, independent of the Module's semantic
version after `@`. A v1 parser rejects every other language version instead of
guessing compatibility. The canonical formatter currently emits exactly
`joggle 1;`; a future incompatible surface therefore needs a new parser and
formatter version rather than silent reinterpretation.

The Module version is exact. Import ranges accept `1`, `1.2`, `1.2.3`, and
`^1.2.3`. They mean a major range, minor range, exact version, and SemVer caret
range respectively. `import` supplies the dependency name, accepted version,
and optional local prefix. A lock file records the exact selected versions and
canonical digests; source imports contain no filesystem paths.

`as` introduces an optional local prefix. It only shortens references such as
`c.word`; linking and persistent symbols still use the imported Module's real
name, version, and digest. Two imports cannot share a prefix, and an import
cannot shadow the current Module name.

## Canonical surface

The complete extension surface is deliberately small. Ordinary model files use
only a subset of it; the grammar is shown in full as a reference, not as a
checklist every author must fill in. Brackets mean optional syntax and braces
mean zero or more repetitions:

```text
file          := "joggle" unsigned-integer ";" module
module        := "module" identifier "@" exact-version
                 "{" { member } "}"
member        := import | interface | type | attr | op | pass | graph

import        := "import" identifier "@" version-range
                 [ "as" identifier ] ";"
interface     := "interface" identifier ":" subject
                 ( ";" | "{" { method } "}" )
subject       := "type" | "attr" | "op"
method        := identifier method-parameters "->" data-kind ";"
method-parameters := "(" [ data-parameter
                     { "," data-parameter } ] ")"
data-parameter := identifier ":" data-kind

type          := "type" identifier schema-parameters [ conforms ] ";"
attr          := "attr" identifier schema-parameters [ conforms ] ";"
schema-parameters := "(" [ schema-parameter
                     { "," schema-parameter } ] ")"
schema-parameter := identifier ":" data-kind [ "=" scalar-literal ]
data-kind     := scalar-kind | "list" "<" scalar-kind ">"
scalar-kind   := "i64" | "f64" | "bool" | "string" | "type" | "attr"

op            := "op" identifier [ generics ]
                 "(" [ op-input { "," op-input } ] ")"
                 [ "->" result-types ] [ conforms ] ";"
generics      := "<" [ generic { "," generic } ] ">"
generic       := identifier ":" data-kind
op-input      := identifier ":" op-input-spec [ "..." ]
                 [ "=" scalar-literal ]
op-input-spec := type-expression | data-kind | "region"
conforms      := ":" reference { "," reference }

pass          := "pass" identifier ";"
               | "pass" identifier "=" reference
                 { "," reference } ";"
               | "pass" identifier "{" contraction
                 { contraction } "}"
contraction   := term "=>" term ";"
term          := "$" identifier
               | reference "(" [ term { "," term } ] ")"

graph         := "graph" identifier graph-arguments
                 [ "->" result-types ]
                 "{" { operation } "return" [ returns ] ";" "}"
graph-arguments := "(" [ graph-argument
                    { "," graph-argument } ] ")"
graph-argument := ssa-name ":" value
result-types  := value | "(" [ value { "," value } ] ")"
returns       := ssa-name { "," ssa-name }

operation     := [ results "=" ] reference
                 "(" [ argument { "," argument } ] ")"
                 [ regions ] ";"
results       := result { "," result }
result        := ssa-name [ ":" value ]
argument      := ssa-name | identifier "=" value
regions       := "{" { region } "}"
region        := identifier [ graph-arguments ]
                 "{" { operation } "}"

type-expression := scalar-literal | list | reference-value
value         := scalar-literal | list | reference-value
list          := "[" [ value { "," value } ] "]"
reference-value := reference [ "<" [ value { "," value } ] ">" ]
reference     := identifier [ "." identifier ]
scalar-literal := number | string-literal | "true" | "false"

exact-version := unsigned-integer "." unsigned-integer
                 "." unsigned-integer
version-range := unsigned-integer
               | unsigned-integer "." unsigned-integer
               | exact-version | "^" exact-version
ssa-name      := "%" ( identifier | unsigned-integer )
identifier    := ( letter | "_" ) { letter | digit | "_" }
unsigned-integer := digit { digit }
number        := [ "-" ] digit { digit }
                 [ "." digit { digit } ]
                 [ ( "e" | "E" ) [ "+" | "-" ] digit { digit } ]
```

`string-literal` uses double quotes and the escapes `\\`, `\"`, `\n`, `\r`,
and `\t`. Whitespace, `#` line comments, and `//` line comments are trivia.
Defaults are restricted to scalar `i64`, `f64`, `bool`, and `string`
parameters. A variadic input must be last; lists and regions cannot be
variadic or have defaults. A generic used as an operation property has its
declared data kind, while a `type` generic in operand position is an SSA value
type. These context rules resolve the deliberate overlap between
`type-expression` and `data-kind`.

Graph arguments, operation results, operands, and returns use `%`; pass terms
use `$`. A qualified `reference` has exactly one Module prefix and one local
member name. The formatter removes trivia, emits the fixed language header,
and groups members as imports, interfaces, types, attributes, operations,
passes, then graphs while preserving source order inside each member kind.
`letter` and `digit` are the ASCII identifier classes; semantic-version
components are unsigned 32-bit values. Formatting valid source twice therefore
produces the same bytes.

## Interfaces and conformance

The kind follows `interface`, and each signature in its body declares a method.

```joggle
interface numeric_format: type {
  storage_bits() -> i64;
  is_signed() -> bool;
}

interface elementwise: op;
```

A colon on a concrete declaration lists the interfaces it conforms to:

```joggle
type integer(width: i64, signed: bool = false) : numeric_format;

op relu<T: type>(input: T) -> T : arith.elementwise;
```

The linker checks local and imported interface names, subject kinds, versions,
and canonical digests. Method parameters and results use `i64`, `f64`, `bool`,
`string`, `type`, `attr`, and homogeneous `list<T>`; `region` is restricted to
operation signatures.

## Types and attributes

```joggle
type tensor(element: type, shape: list<i64>);

attr dense(values: list<i64>);
```

Primitive parameters may have defaults. Parameter counts, kinds, defaults, and
Module ownership are checked automatically. A C++ package may additionally
attach semantic validation, but the schema does not advertise implementation
mechanics.

## Operations

```joggle
interface commutative: op;
op add<T: type>(lhs: T, rhs: T) -> T
  : elementwise, commutative;
```

SSA operands and results carry type expressions. Repeated type variables impose
equality, while ordinary primitive kinds describe schema properties. A final
operand may be variadic, for example `op concat<T: type>(inputs: T...) -> T;`.
Input names drive verification and diagnostics but do not become process-local
numeric keys.

Type variables may range over declared types, primitive parameters, or lists:

```joggle
op dense<E: type, N: i64, K: i64, M: i64>(
  input: tensor<E, [N, K]>,
  weight: tensor<E, [M, K]>,
  bias: tensor<E, [M]>
) -> tensor<E, [N, M]>;
```

A non-`type` generic can also be bound by a named operation property. This
keeps shape, width, layout, and similar compile-time values in the operation
contract instead of repeating the result type at every call:

```joggle
op input<N: i64>(width: N) -> word<N>;
op default_input<N: i64>(width: N = 8) -> word<N>;

graph example() -> word<8> {
  %value = input(width = 8);
  return %value;
}
```

`N` in `width: N` remains a named property, not an SSA operand. A `type`
generic in input position remains an SSA operand type. Property values,
operand types, explicit result annotations, and graph return types all feed the
same unifier. Defaults participate in inference when the property is omitted.

The compiler unifies operand types, dependent properties, and any explicit
result annotations against this contract, then instantiates omitted result
types. Thus `%y = relu(%x);`
needs no annotation, while an operation whose variables occur only in its
result is constrained by `%y: tensor<word<8>, [1, 4]> = input(...);` or by the
result signature when `%y` is returned directly. Text graphs, C++-constructed
graphs, and lowering results use the same solver.

A marker interface such as `commutative` uses the same versioned conformance
mechanism as an interface with methods.

## Passes

```joggle
pass canonicalize {
  cast($input) => $input;
}

pass lower;
pass optimize = canonicalize, lower;
```

Every transformation is a `pass`. `$name` denotes a pattern variable; `%name`
is reserved for an SSA value in a `graph`. A pass with a body contains ordered
contraction rules, a pass ending in `;` receives a C++ implementation, and a
pass with `=` composes other local or imported passes. See [Passes](passes.md)
for recursive term syntax, termination, composition, and atomic rollback.

## Graphs

A `graph` member is an executable SSA program. Its signature declares the
program boundary, and its body creates operations from the current Module and
its imports. Parentheses are always present, including for a graph with no
inputs:

```joggle
graph main(%x: tensor<word<8>, [1, 4]>) -> tensor<word<8>, [1, 4]> {
  %y = relu(%x);
  return %y;
}
```

The body is ordinary SSA. `%name: Type` declares a value type, `=` defines a
value, and named operation properties use `name = value` inside the call. Most
result types are inferred from the operation contract:

```joggle
graph forward(%input: tensor<integer<8>, [1, 4]>) -> tensor<integer<8>, [1, 3]> {
  %weight: tensor<integer<8>, [3, 4]> = constant(
    value = dense<[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]>
  );
  %bias: tensor<integer<8>, [3]> = constant(
    value = dense<[0, 0, 0]>
  );
  %output = linear(%input, %weight, %bias);
  return %output;
}
```

An annotation is needed only when an operation's result type cannot be inferred
from operands or the graph result. A multi-result operation writes
`%first: T, %second: U = split(%input);`; a multi-result graph uses `-> (T, U)`
and `return %first, %second;`.

Structured operation regions use braces on the same call rather than a second
graph syntax:

```joggle
%y: tensor<word<8>, [2, 4]> = scope(tag = "nested") {
  body {
    %sum = add(%x, %x);
  }
};
```

A region is one ordered operation body. Its optional arguments are declared on
the region name, so every graph construct has a lossless text form:

```joggle
%y: tensor<word<8>, [2, 4]> = scope(tag = "nested") {
  body(%item: tensor<word<8>, [2, 4]>) {
    %sum = add(%item, %item);
  }
};
```

SSA names are lexical to their region. A nested region may capture a value from
an enclosing region and may shadow an enclosing name. Sibling regions may reuse
the same local names; a region argument or result is not visible after leaving
that region. Dominance is checked across the same nesting structure.

`body: region` in an operation signature declares one named nested-body slot.
It does not introduce a second function or graph signature. Region arguments
are explicit SSA entry values on each operation instance, and the region has no
separate return or yield list; the owning operation's results remain those in
its `op` declaration. The core verifies binding names, cardinality, lexical SSA,
ownership, and dominance. An optional operation verifier may add a
domain-specific relationship between the owner, its region arguments, and its
nested operations.

The surrounding Module supplies imports, versioning, identity, installation,
and locking. A graph is an ordinary Module member. The graph signature is its
complete boundary: typed SSA arguments enter on the left and returned SSA
values leave on the right. Opening a named graph and constructing an empty one
through C++ produce the same `Graph`; passes edit that object atomically and the
formatter writes it back as the same `graph` syntax. The C++ operations are
documented in [Typed C++ API](cpp-api.md).

Imports provide versioned lexical namespaces for external names. Operation
signatures determine well-formedness, and pass patterns determine
applicability. The linker and verifier derive every used-member edge from
actual references; an extension author does not repeat dependency or
capability lists.

Module identity is `name + version + SHA-256(format(module))`. Comments, source
paths, and whitespace do not affect it. Persistent symbols include that complete
identity; `arith.add` is only the human-facing form. Imports are Module-scoped,
and the lock file pins the selected closure.
