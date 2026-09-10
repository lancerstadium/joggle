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
function body. Mutable values crossing a `for` or `if` boundary become `Blk`
arguments, results, and an internal `yield`; these mechanics remain visible to
C++ transforms but are recovered as normal source syntax by the printer.

Calls, literals, indexing, unary operators, and common binary operators are
implemented. Operators normalize to ordinary function calls such as
`operator +` and `operator []`; adding a concrete overload does not add a new
IR operation kind. `&&` and `||` are the deliberate exception: they normalize
to ordinary structured branches so their right operand is evaluated only in
the selected `Blk`. The printer recovers the source expression, while passes
see the actual control flow. Operator functions use the symbol directly:

```jog
fn +<T: Ty>(a: T, b: T) -> T;
fn +<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
```

Ordinary and symbolic functions both form overload sets. Verification filters
by arity and recursive generic unification, then prefers the structurally more
specific signature; equally specific survivors are an ambiguity error. Local
and transitively imported declarations participate in the same visible family.
This lets a tensor or number-format overload call less-specific base algebra in
its own body, while a more-specific `sat<W>` overload still wins without a
saturating-type case in the resolver.

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

The same tree is available to compile-time functions. `ir.type(value)` returns
a `Ty`; `name(type)` and `args(type)` inspect it, `int(type)` projects a numeric
term, and `str(type)` requests canonical text for a native boundary.
`ty(text)`, `ty(integer)`, and `ty(name, arguments)` construct validated trees.
`ir.type(mod, value, type)` writes an inferred type back to a value and its
structured carried-value family. This is ordinary type algebra, not a separate
shape-expression or data-format registry.

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

The `_` term remains open when nested in a structural argument. Consequently
`tensor<f32, [_, 3]>` satisfies a `list<int>` shape constraint without claiming
that the unknown extent is a type. A named relation uses an ordinary generic,
for example `fn f<N: int>(x: tensor<f32, [N, 3]>)`; repeated `N` occurrences
retain equality through the existing unifier.

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
or inferred recursively from argument types, explicit result annotations, and
the enclosing function result when a call is returned directly. The resulting
substitution is applied to every call result. This permits a shape-producing
function to infer dimensions that occur only in its result without encoding
them in the function name or forcing an otherwise redundant `let` binding.
Conflicting bindings, wrong argument counts, and wrong concrete types receive
source-located diagnostics.
For example,
`sat.add(a, b)` over two `sat<8>` values has result type `sat<8>`, while mixing
`sat<8>` and `sat<16>` is rejected. Unknown calls remain valid open IR so a
frontend can transport source operations before a semantic bridge is loaded.
`Mod::find_fns` and `Env::find_fns` return the complete declaration family;
their singular `find_fn` forms intentionally return an invalid handle when the
name is overloaded. `Env::resolve(mod, op)` performs the same overload choice
as verification, allowing tools to distinguish a resolved call from open IR.
Resolution from a `Fn` uses that function's module and transitive imports, so
compile-time execution and verification have identical visibility rules.

Ordinary function signatures also describe a consumer boundary. A bodyless
declaration may use a dotted local name such as `tensor.matmul`; `ir.name(fn)`
returns that local semantic name, `ir.fns("edge")` enumerates declarations in a
loaded module, and `ir.accepts(op, fn)` applies the normal generic unifier to an
existing call. This supports precise element, shape, width, and custom-format
constraints without adding a target hierarchy or a second pattern language.
Inspecting a module this way does not add a `use` edge to the transformed model.

List literal element types and loop-element types participate in the same
fixed-point propagation. This is what lets `for op in ir.ops(blk)` type `op`
as `Op` without a special loop form. `Attr` is the one intentionally dynamic
compile-time value type: its runtime tag is checked by the consuming function.
Integer values are accepted where an `index` is required, so literal zero and
computed scalar coordinates use the same tensor indexing functions as loop
variables.

List literals may contain any compile-time value, including IR handles, and
`+` concatenates lists. This makes structural selections concise without a
second pattern language:

```jog
let group = [producer, consumer]
group += [last]
```

`let` bindings are immutable. `var` bindings may be reassigned with `=` or the
compound forms `+=`, `-=`, `*=`, `/=`, `%=`, `|=`, `^=`, `&=`, `<<=`, and
`>>=`. Each compound form is the corresponding overloaded binary call followed
by the same value update; it does not add an operation kind. Tensor-like values
may use `value[i, j] = next`, which normalizes to `operator []=` returning the
updated value, so mutation remains explicit value flow.

Multiple results use ordinary comma-separated bindings rather than tuple or
result operations:

```jog
let quotient, remainder = divmod(7, 3)
let data: tensor<f32, [4]>, token: i32 = source()
```

Each binding may carry the same open attributes used elsewhere:

```jog
fn run<[role: "extent"] N: int>(
  [layout: "nhwc"] x: tensor<i8, [N, 8]>
) -> tensor<i8, [N, 8]> {
  [place: "edge"]
  let [quant: {scale: [0.25], zero_point: [0]}] y = execute(x)
  return y
}
```

Position is the only distinction. Attributes before `fn` describe the `Fn`,
attributes before a statement describe its root `Op`, and inline attributes
before a generic, parameter, or local name describe that `Val`. No key is
reserved. Mutable bindings carry their value attributes through the internal
`Blk` arguments and results created by `for` and `if`.

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

Dictionary values use ordinary library functions and indexing:

```jog
if has(attrs, "axis") {
  let axis = attrs["axis"]
}
let fallback = get(attrs, "axis", -1)
for key in keys(attrs) { inspect(key) }
```

`attrs[key]` is strict; `get` returns `nil` or an explicit fallback when absent.
`kind` reports the structural value kind, and `len` applies to lists or
dictionaries. `int` and `str` project a checked `Attr` leaf into a typed scalar;
the same names also project structural `Ty` terms. These operations are in
`base`, leaving `ir` exclusively about `Mod/Fn/Blk/Op/Val` reflection.

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

`assert(condition, message)` is the ordinary failure boundary for module code.
A true condition returns `true`; a false condition stops compile-time execution
with the supplied message. When invoked through `run`, it participates in the
same whole-module rollback as every other runtime failure.

`ir.ops(m)` walks every function body in deterministic structural preorder,
including nested loops and conditions. Use `ir.ops(f)` for one function or
`ir.ops(b)` for one `Blk`. The optional fourth argument to `ir.replace` names a
single user operation; omitting it redirects every use after checking type and
dominance. Successful edits advance `Mod::revision()`, while a failed run
restores both the IR and its prior revision.

`ir.def(v)` returns the operation defining a value; parameters have an invalid
definition detectable with `ir.live`. Together with `ir.users`, this completes
both directions of ordinary dataflow traversal. Byte attributes remain opaque
storage by default, but `base.size(value)` and `base.byte(value, index)` provide
checked compile-time access when a codec relation must interpret a small
payload. They do not add file or ambient-memory access.

`ir.name(v)` and `ir.rename(m, v, name)` are the symmetric readable-name
operations. They matter when one source call is decomposed into several normal
calls: a module can preserve the externally meaningful result name without
accessing internal storage or generated identifiers.

Construction also uses ordinary overloaded functions. `ir.constant` and
`ir.call` insert leaves before a named operation. `ir.clone` deep-copies an
operation and its nested `Blk`s, while `ir.move` changes `Blk`-local order only
when all operands and users remain dominated. `ir.kind(op)` returns `call`,
`constant`, `loop`, `branch`, `return`, or `yield`; `ir.blks(op)` exposes
nested bodies. A terminator supplies an insertion point even for an otherwise
empty `Blk`, so there is no stateful builder object.

Structured construction follows the same rule. `ir.loop` receives iterator
names, source values, and carried values, then returns an `Op` with one body and
an initial `yield`. `ir.branch` returns an `Op` with two initially forwarding
arms. `ir.args(blk)` obtains `Blk` arguments and `ir.args(m, op, values)`
reconnects any operation, including `return` and `yield`, while enforcing its
structural arity and dominance. A named local selected as carried state prints
as the corresponding ordinary `var`; users never construct `Blk` objects or
source-presentation records themselves.

`ir.rename` treats the versions of a carried mutable binding as one lexical
name. Calling it on the incoming value, a loop or branch `Blk` argument, a
yielded update, or the structure result renames the whole chain atomically.
Calling it on a loop iterator also updates the loop header. This makes generic
`Blk` traversal safe to edit without exposing the printer's bookkeeping.

Call conversion uses `ir.retarget(m, op, callee, args)`. It applies the same
visibility, overload, generic, argument, result, and dominance checks as an
ordinary source call before changing either the callee or its operands. A
failed match returns `false` with the original call intact. Bridge modules can
therefore try a semantic function without constructing a parallel legality
system or relying on whole-pipeline rollback.

Function bodies are exposed by an explicit edit, never by loading a module.
`ir.resolve(m, op)` applies normal import, qualification, overload, and generic
resolution to a call and returns an invalid `Fn` when the call remains open.
`ir.expand(m, op, fn)` substitutes the selected ordinary
function body at that call, remaps its parameters and nested control flow, and
preserves the caller's visible result bindings. The edit is atomic; a missing
body, signature mismatch, unrepresentable compile-time argument, or metadata
whose policy has not been chosen leaves the module unchanged.

`ir.match(op, fns)` applies the same specificity ordering to an explicit list
of function handles. No match returns an invalid `Fn`; equally specific matches
are an execution error rather than a declaration-order choice. When the chosen
function comes from another module and its local name denotes the resolved
source symbol, `ir.expand` treats it as an alternative implementation, adds its
owning module only if not already visible, and rolls back both changes on
failure. Thus an unqualified source call and a module-supplied implementation
still use ordinary symbol resolution rather than a string alias table.

`ir.uses(m)` returns the module's declared dependencies and
`ir.use(m, name)` adds one idempotently. Dependency edits advance the same
module revision and participate in compile-time rollback. This lets an
explicit frontend bridge introduce the semantic library whose qualified
functions it selects; parsing a frontend never does so implicitly.

Generic compile-time helpers use the same syntax and bindings. In
`fn below<N: int>(x: int) -> bool { return x < N }`, a call to `below<4>(3)`
binds the generic `Val` `N` to the integer `4` in the function frame. `Ty` and
`list<...>` generic values are materialized through the same mechanism, so
modules can write reusable shape and format helpers without a second evaluator
API.

Composition is ordinary function composition. A module may call transforms
directly and use a bounded `for` to reach a fixed point; no function runs merely
because it is tagged or installed. The bundled `opt.fix(m, pure, limit)` is one
such function. Its `pure` list is explicit policy: an unknown call is never
merged or deleted unless the caller names it. `ir.live` lets deletion-based
transforms safely consume a traversal snapshot, while `ir.blk` and
`ir.meta(op)` support structural comparison.

Canonical printing preserves expression trees with precedence-aware
parentheses. In particular, `a && (b || c)`, `(a + b) * c`, and
`a - (b - c)` retain their meaning after print and reparse.
Logical expressions preserve true short-circuit behavior, including during
compile-time execution; a missing or mutating call in an unselected operand is
never evaluated. Nested `yield` values are type-checked against the carried
result, so malformed logical and user-constructed control flow is rejected.

The embedding overload `run(env, name, mod, report)` returns execution detail
in an `Attr` dictionary. `ok` denotes successful execution, `reported` is the
entry function's boolean return, `changed` compares module revisions, `edits`
is the revision delta, and `steps` contains the same fields for nested
`Mod`-accepting calls in completion order. This keeps reporting optional and
does not add a pipeline object to the language. Every step has a `kind`:
`fn` identifies an ordinary nested function completion, while `expand` records
the source semantic symbol, selected implementation symbol, parameter and
return type patterns, and its exact revision delta. These are structural
dictionary fields, not a second event class or callback interface.

`query(env, name, mod, result, args, cached)` embeds an ordinary function as a
read-only analysis. Its first parameter is `Mod`; subsequent parameters receive
the explicit `Attr` arguments, and it returns one value representable as
`Attr`. The function runs on a verified snapshot. Any attempted IR edit makes
the call fail without changing the original module. Successful results are
revision-aware and may be reused; the optional `cached` output reports whether
that happened.

For a host-selected sequence, the embedding API also accepts
`run(env, span_of_names, mod, report)`. It executes the same ordinary functions
in order and rolls the complete sequence back if any step fails. A source
wrapper and a host sequence therefore differ only in where the list of calls is
chosen, not in their IR or function semantics.

### Open attributes

Square brackets hold an open attribute dictionary rather than a fixed set of
compiler keywords. They may precede a function or an operation statement, or
appear inline before a generic, parameter, or local binding:

```jog
[entry, stage: "select", policy: {modes: ["fast", "small"]}]
fn choose(m: Mod) -> bool { return true }

fn placed(x: tensor<f32, [4]>) -> tensor<f32, [4]> {
  [place: "edge", layout: "packed"]
  let [quant: {scale: [0.5], zero_point: [0]}] y = transform(x)
  [trace]
  return y
}
```

A bare name means `true`; values use normal `Attr` literals. Repeated brackets
are accepted and canonical printing merges them in key order. Duplicate keys
are errors. On a statement, the dictionary belongs to its root `Op`, whether
that operation is a call, loop, condition, or return. `Fn::meta` and `Op::meta`
expose those dictionaries to C++; `Val::meta` exposes binding attributes.
Overloaded `ir.has`, `ir.meta`, `ir.set`, and `ir.unset` cover all three handle
kinds for textual functions.

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
