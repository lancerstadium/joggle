# Language

`.jog` is Joggle's only source and readable IR format. It describes reusable
modules, functions, types, control flow, and explicit compile-time workflows.
It is intentionally not a second pipeline or kernel language.

The implemented surface is conventional:

```jog
module demo
use tensor

fn matmul<T: Ty, M: int, N: int, K: int>(
  a: tensor<T, [M, K]>,
  b: tensor<T, [K, N]>
) -> tensor<T, [M, N]> {
  var c = tensor<T, [M, N]>(0)
  for i in 0..M, j in 0..N {
    var sum = T(0)
    for k in 0..K {
      sum += a[i, k] * b[k, j]
    }
    c[i, j] = sum
  }
  return c
}
```

The language uses ordinary `module`, `use`, `fn`, `let`, `var`, `for`, `if`,
and `return`. It has generics, structural types, attributes, and overloadable
operators. It has no `graph`, `kernel`, `compute`, `map`, `fold`, `rewrite`,
`region`, or `pass` syntax.

Multiple loop variables denote a lexically nested Cartesian product. A source
may be a range or any compile-time list, so the same form traverses tensor
indices and IR collections. `return` is always an ordinary statement in the
function block. Mutable values crossing a `for` or `if` boundary become block
arguments, results, and an internal `yield`; these mechanics remain visible to
C++ transforms but are recovered as normal source syntax by the printer.

Calls, literals, indexing, unary operators, and common binary operators are
implemented. Operators normalize to ordinary function calls such as
`operator +` and `operator []`; adding a concrete overload does not add a new
IR operation kind. Operator functions use the symbol directly:

```jog
fn +<T: Ty>(a: T, b: T) -> T;
fn +<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
```

Ordinary and symbolic functions both form overload sets. Verification filters
by arity and recursive generic unification, then prefers the structurally more
specific signature; equally specific survivors are an ambiguity error. A local
family shadows imported families. Otherwise declarations from the transitive
`use` closure participate together, which lets the `sat<W>` overload outrank
the generic base overload without a saturating-type case in the resolver.

Types are immutable structural values rather than uninterpreted spellings. A
type has a constructor name and zero or more type/value arguments; bracketed
shapes use the same recursive representation:

```text
tensor<f32, [2, N]>
  tensor
    f32
    []
      2
      N
```

Whitespace is canonicalized when a `Ty` is constructed. `Ty::valid`,
`Ty::name`, and `Ty::args` expose validation and structure to embedding code
without introducing a class per type constructor. Malformed nesting and empty
arguments are rejected while parsing declarations. Constructor meaning is
supplied by modules; the core only needs the tree for matching and substitution.

A named parametric type is declared with the same `fn` mechanism as every
other extension. A zero-argument function returning `Ty` is a type constructor;
its generic list is the constructor's argument list:

```jog
module sat

fn sat<W: int>() -> Ty;
fn add<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
```

Generic parameters are compile-time `Val`s and use ordinary types for their
constraints. `Ty` means a type argument, `int` can represent a bit width or
dimension, and `list<int>` describes a shape. An omitted annotation is `_`, the
open constraint. The same structural checker validates explicit generic
arguments, inferred bindings, and type-constructor arguments; no separate kind
or trait registry exists. `Fn::generics()` exposes the parameter values, so
embedding code reads both `name()` and `type()` through the normal `Val` API.

Type construction and value construction may share one name. For example,
`fn tensor<E: Ty, S: list<int>>() -> Ty` declares the type spelling while
`fn tensor<E: Ty, S: list<int>>(fill: E) -> tensor<E, S>` constructs a value.
They are normal overloads; the verifier identifies the zero-value-argument
`Ty` overload when checking a type and the value overload when checking a call.

After `use sat`, `sat<8>` resolves to `sat.sat<8>`. No `type` keyword, generated
class, registry callback, or metadata tag is involved. Constructor arity and
visibility are verified like function arity and visibility. Unloaded
constructors remain open structural types so a source-only parse does not need
to install every extension; once a matching module is loaded, a missing `use`
edge or incompatible declaration is an error.

During verification, a call to a known local or qualified module function is
checked against its declaration. Generic arguments may be written explicitly
or inferred recursively from argument types, and the resulting substitution is
applied to every call result. Conflicting bindings, wrong argument counts, and
wrong concrete types receive source-located diagnostics. For example,
`sat.add(a, b)` over two `sat<8>` values has result type `sat<8>`, while mixing
`sat<8>` and `sat<16>` is rejected. Unknown calls remain valid open IR so a
frontend can transport source operations before a semantic bridge is loaded.
`Mod::find_fns` and `Env::find_fns` return the complete declaration family;
their singular `find_fn` forms intentionally return an invalid handle when the
name is overloaded. `Env::resolve(mod, op)` performs the same overload choice
as verification, allowing tools to distinguish a resolved call from open IR.
Resolution from a `Fn` uses that function's module and transitive imports, so
compile-time execution and verification have identical visibility rules.

List literal element types and loop-element types participate in the same
fixed-point propagation. This is what lets `for op in ir.ops(block)` type `op`
as `Op` without a special loop form. `Attr` is the one intentionally dynamic
compile-time value type: its runtime tag is checked by the consuming function.

List literals may contain any compile-time value, including IR handles, and
`+` concatenates lists. This makes structural selections concise without a
second pattern language:

```jog
let group = [producer, consumer]
group += [last]
```

`let` bindings are immutable. `var` bindings may be reassigned with `=` or
`+=`, and tensor-like values may use `value[i, j] = next`. The latter normalizes
to a call of `operator []=` returning the updated value, so mutation remains an
explicit value flow.

Multiple results use ordinary comma-separated bindings rather than tuple or
result operations:

```jog
let quotient, remainder = divmod(7, 3)
let data: tensor<f32, [4]>, token: i32 = source()
```

Known function declarations infer the result types. Explicit annotations keep
types for open calls whose semantics have not yet been imported. A call is
still one `Op`; its ordered values are returned by `outs()`. The same form is
used by model functions and executable compile-time helpers.

Canonical printing deliberately discards comments and incidental whitespace.
Printing and reparsing must produce a structurally equal module.

Attributes use the same literal syntax wherever constant metadata is needed:

```jog
{axis: 1, pads: [0, -1], raw: hex"007fff", label: "weight"}
```

The structural `Attr` values are `nil`, `bool`, signed integer, `f64`, `str`,
`bytes`, list, and string-keyed dictionary. Byte strings print as lowercase hex
and cross the native ABI without being treated as UTF-8.

### Compile-time functions

There is no separate pass syntax. Any ordinary function accepting a `Mod` may
be selected explicitly by the embedding API or CLI:

```sh
joggle run opt.fold_add_zero model.jog -M modules
```

Calls inside that function remain ordinary calls. Compile-time execution
supports structured `for` and `if`, scalar operators, lists, and the universal
`Mod`, `Fn`, `Blk`, `Op`, and `Val` handles exposed by `ir`. Failed execution is
transactional. It does not evaluate arbitrary model functions or silently run
transforms while parsing.

`ir.ops(m)` walks every function body in deterministic structural preorder,
including nested loops and conditions. Use `ir.ops(f)` for one function or
`ir.ops(b)` for one block. The optional fourth argument to `ir.replace` names a
single user operation; omitting it redirects every use after checking type and
dominance. Successful edits advance `Mod::revision()`, while a failed run
restores both the IR and its prior revision.

Construction also uses ordinary overloaded functions. `ir.constant` and
`ir.call` insert leaves before a named operation. `ir.clone` deep-copies an
operation and its nested blocks, while `ir.move` changes block-local order only
when all operands and users remain dominated. `ir.kind(op)` returns `call`,
`constant`, `loop`, `branch`, `return`, or `yield`; `ir.blocks(op)` exposes
nested bodies. A terminator supplies an insertion point even for an otherwise
empty block, so there is no stateful builder object.

Generic compile-time helpers use the same syntax and bindings. In
`fn below<N: int>(x: int) -> bool { return x < N }`, a call to `below<4>(3)`
binds the generic `Val` `N` to the integer `4` in the function frame. `Ty` and
`list<...>` generic values are materialized through the same mechanism, so
modules can write reusable shape and format helpers without a second evaluator
API.

### Open attributes

Square brackets hold an open attribute dictionary rather than a fixed set of
compiler keywords. They may precede a function or an operation statement:

```jog
[entry, stage: "select", policy: {modes: ["fast", "small"]}]
fn choose(m: Mod) -> bool { return true }

fn placed(x: tensor<f32, [4]>) -> tensor<f32, [4]> {
  [place: "edge", layout: "packed"]
  let y = transform(x)
  [trace]
  return y
}
```

A bare name means `true`; values use normal `Attr` literals. Repeated brackets
are accepted and canonical printing merges them in key order. Duplicate keys
are errors. On a statement, the dictionary belongs to its root `Op`, whether
that operation is a call, loop, condition, or return. `Fn::meta` and `Op::meta`
expose the same data to C++, while overloaded `ir.has` and `ir.meta` expose it
to textual functions. `ir.set` and `ir.unset` edit function or operation
attributes.

No attribute name changes parsing, binding, or the IR shape. In particular,
`host`, `target`, `place`, and `layout` have no built-in meaning. A native
library may bind any matching body-less declaration; no marker is required and
a function with a body cannot be rebound. Useful module-defined keys include
`role`, `stage`, `target`, and `cost`, but none is owned by the core. Structural
type constructors, parametric matching, result inference, and overload-set
resolution use the same function declarations. Return types do not distinguish
overloads, and parameter signatures that differ only in generic names are
rejected as duplicates.

An attribute becomes behavior only when an explicitly selected function
queries it. A transform may use `[rewrite: "lab.fused"]` to choose a
replacement call; an emitter may use `[target: "board-name"]`; a search
procedure may attach a structural `cost` dictionary. Installing such a module
does not register a new language keyword or silently execute any of these
policies.
