---
title: Language reference
description: Complete syntax and semantics of the Joggle language.
---

# Language reference

This is lookup material, not a tutorial. Start with
[Get started](../guide/index.md) if you have not yet checked and transformed a
program.

`.jog` is Joggle's only source and readable IR format. It describes reusable
modules, functions, types, control flow, and explicit compile-time workflows.
It is intentionally not a second pipeline or kernel language.

The implemented surface is conventional:

```jog
mod demo
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

The language uses ordinary `mod`, `use`, `fn`, `let`, `var`, `for`, `if`,
and `return`. It has generics, structural types, attributes, and overloadable
operators. It has no `graph`, `kernel`, `compute`, `map`, `fold`, `rewrite`,
`region`, or `pass` syntax.

The matching CLI administration command is `joggle mod`. The former declaration
and command spelling `module` is rejected; there is no compatibility alias.

Module names, dependency names, and dotted function names consist of nonempty
identifier segments separated by single dots. Invalid qualified names are
rejected at their declaration rather than entering the symbol table.

Top-level functions are exported by default. A helper that belongs only to its
declaring module uses the ordinary `local fn` form:

```jog
use base

local fn flatten(type: Ty) -> list<Ty> {
  return args(type)
}

fn convert(m: Mod) -> bool {
  return true
}
```

Local functions participate in calls made by the same module and remain
visible to explicit structural reflection, but imported or qualified lookup,
CLI invocation, module information, and compatibility checks expose only the
public surface. Visibility is a property of `Fn`, available as `Fn::local()`
and `ir.local(fn)`; it is not encoded in an attribute or naming convention.

A pure compile-time helper may opt into per-run memoization with `[memo]`:

```jog
[memo]
local fn product(shape: list<int>) -> int {
  var out = 1
  for extent in shape {
    out *= extent
  }
  return out
}
```

`[memo]` is a semantic promise that equal arguments under the same IR snapshot
produce an equal result without externally visible effects. Scalar attributes,
types, IR capabilities (`Mod`, `Fn`, `Blk`, `Op`, and `Val`), and recursive
lists of those values may participate. Capability keys contain the store
identity, handle identity and generation, and current store revision. A result
that contains capabilities also retains their store revisions as dependencies.
Every dependency must still be current at a hit, and a call that changes any
input store revision is never inserted. Consequently an explicitly memoized IR
query can be reused, while an accidentally annotated transform cannot leave a
reusable stale entry. The cache lasts for one top-level `run` step; there is no
process-global cache or cross-pass invalidation protocol. External state is not
tracked, so the purity promise remains the module author's responsibility.

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

A call may also stand alone as a statement. Resolution removes the parser's
temporary result when the declaration returns nothing, while a call whose
result is merely unused retains that result in the IR. This keeps the same
source form for both cases without introducing a second call syntax. An open
call keeps its temporary result until a later verification can resolve it.

Ordinary and symbolic functions both form overload sets. Verification filters
by arity and recursive generic unification, then prefers the structurally more
specific signature; equally specific survivors are an ambiguity error.
Same-module and transitively imported public declarations participate in the
same visible family; module-local declarations participate only for calls made
by their declaring module.
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
arguments are rejected while parsing declarations. Matching preserves that
tree exactly: `opaque` and `opaque<i32>` are distinct types, and `_` is the only
structural wildcard. Constructor meaning is supplied by modules; the core only
needs the tree for matching and substitution.

The same tree is available to compile-time functions. `ir.type(value)` returns
a `Ty`; `name(type)` and `args(type)` inspect it, `int(type)` projects a numeric
term, and `str(type)` requests canonical text for a native boundary.
`ty(text)`, `ty(integer)`, and `ty(name, arguments)` construct validated trees.
The embedding API provides the corresponding `Ty(text)` and `Ty(name, args)`
constructors, so a C++ extension never has to serialize nested type arguments
just to construct a structural type.
`ir.type(mod, value, type)` writes an inferred type back to a value and its
structured carried-value family. Its list overload assigns one type per value,
computes all carried families once, rejects inconsistent requests to the same
family without mutation, and commits the batch with one revision change.
`ir.returns(mod, fn, types)` edits the other part of a function signature,
allowing a format conversion to retype parameters, body values, calls, and
returns in one transactional transform. Verification
checks every `return`, including early returns inside nested control flow. This
is ordinary type algebra, not a separate shape-expression or data-format
registry.

A named parametric type is declared with the same `fn` mechanism as every
other extension. A zero-argument function returning `Ty` is a type constructor;
its generic list is the constructor's argument list:

```jog
mod sat

fn sat<W: int>() -> Ty;
fn add<W: int>(a: sat<W>, b: sat<W>) -> sat<W>;
```

Generic parameters are compile-time `Val`s and use ordinary types for their
constraints. `Ty` means a type argument, `int` can represent a bit width or
dimension, and `list<int>` describes a shape. An omitted annotation is `_`, the
open constraint. The same structural checker validates explicit generic
arguments, inferred bindings, and type-constructor arguments; no separate kind
or trait registry exists. A term can occupy a type position only when its
constraint is `Ty` (or open): value parameters such as `N: int`, shape lists,
and scalar literals are rejected there. A generic parameter cannot reuse an
intrinsic type name, because it participates in both value terms and type
patterns. Intrinsic types have fixed arity;
`list<T>` is the only parameterized intrinsic. `Fn::generics()` exposes the
parameter values, so embedding code reads both `name()` and `type()` through
the normal `Val` API.
An explicit generic list may bind a leading prefix while ordinary arguments
infer the remainder. For example, `invoke<bool>(m, subject, policy)` fixes the
result type and infers the subject type; supplying more explicit terms than a
declaration owns remains an error, and any uninferred term is rejected before
the selected function body executes.

The `_` term remains open when nested in a structural argument. Consequently
`tensor<f32, [_, 3]>` satisfies a `list<int>` shape constraint without claiming
that the unknown extent is a type. A named relation uses an ordinary generic,
for example `fn f<N: int>(x: tensor<f32, [N, 3]>)`; repeated `N` occurrences
retain equality through the existing unifier.

The intrinsic derived term `len<S>` relates a compile-time list to its length
without introducing a shape-expression language.  If `S` is bound to
`[2, 3, 5]`, substitution reduces `len<S>` to the integer term `3` everywhere
in the signature and body.  This is useful when a reusable function needs the
rank of a structural shape but must not duplicate it as an independently
supplied generic.  `len<S>` is well formed only when `S` has list kind.

A specialized function may retain `_` inside a concrete structural term, such
as the shape `[_, 4]`.  Such a term describes runtime variability and is not an
uninferred generic.  Specialization therefore accepts it when the corresponding
generic is used only in types; a generic consumed as a runtime value must still
be fully materializable.  The distinction keeps dynamic tensor signatures
expressible while rejecting attempts to execute an unknown compile-time value.

Type construction and value construction may share one name. For example,
`fn tensor<E: Ty, S: list<int>>() -> Ty` declares the type spelling while
`fn tensor<E: Ty, S: list<int>>(fill: E) -> tensor<E, S>` constructs a value.
They are normal overloads; the verifier identifies the zero-value-argument
`Ty` overload when checking a type and the value overload when checking a call.
Local and imported declarations are merged for both lookups, so adding a local
value function with the same name cannot hide an imported type constructor.

After `use sat`, `sat<8>` resolves to `sat.sat<8>`. A constructor does not have
to repeat its module name: after `use number`, `qreal<8>` resolves to
`number.qreal<8>` through the same visible function-family lookup. No `type`
keyword, generated class, registry callback, or metadata tag is involved.
Constructor arity, argument kinds, imports, and `local` visibility are verified
like function arity and visibility. Unloaded
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
Return constraints also remove incompatible overloads before specificity is
compared, so a concrete but wrong-result candidate cannot hide a valid generic
candidate. The choice is independent of declaration order.
Conflicting bindings, wrong argument counts, and wrong concrete types receive
source-located diagnostics.
When an unannotated intermediate is returned, the declared function result is
also propagated back to that value before call resolution.  Thus binding
`f(x)` to `y` and then returning `y` has the same result context as
`return f(x)`.  This bidirectional step is structural and applies equally to
frontend calls and user functions; it is not tied to an operation name.
Calls whose argument or expected result contains a surrounding function's
generic may remain unresolved until that generic is bound. Verification allows
this only when at least one visible overload has a structurally possible arity
and shape after masking the dependent terms; it never chooses an arbitrary
candidate. A monomorphic body resolves normally after specialization, while a
non-dependent invalid call is rejected immediately. This permits a generic
tensor algorithm to call exact scalar overloads supplied by built-in or
user-defined number formats without declaring a false catch-all function.
An unqualified call forms its overload set from the current module and visible
imports; a qualified call intentionally selects one module namespace. Generic
library code therefore uses an imported unqualified name when downstream
modules are meant to contribute overloads. Artifact modules inspect the
resolved `Fn` symbol rather than depending on whichever spelling survived in
the source text. Namespace splitting is also visibility-relative: the current
module and its dependency closure participate in one rule, and the longest
visible module that exports the remaining function name wins. A module may
therefore depend on a child namespace such as `image` using `image.codec`
without the current module's shorter name consuming the qualification first.
Loading an unrelated package with a longer name cannot shadow a function in
the caller's existing dependency closure. Structural type constructors use the
same visibility-relative split, so their meaning is equally independent of
unrelated load order.
For example,
`sat.add(a, b)` over two `sat<8>` values has result type `sat<8>`, while mixing
`sat<8>` and `sat<16>` is rejected. Unknown calls remain valid open IR so a
frontend can transport source operations before a semantic bridge is loaded.
That openness does not bypass module privacy: a qualified name that exactly
matches a `local fn` in another loaded package is rejected as inaccessible,
while a genuinely undeclared source-format operation remains open.
`Mod::find_fns` returns declarations in the concrete module being edited,
including its local structure. `Env::fns(module)` and `Env::find_fns` expose
only the public surface of an installed module; `ir.fns(module)` follows the
same rule. `Env::declared(symbol)` can test whether an exact qualified symbol
exists, including a local declaration, without exposing its handle. Their
singular `find_fn` forms intentionally return an invalid handle when the name
is overloaded. `Env::resolve(mod, op)` performs the same overload choice as
verification, allowing tools to distinguish a resolved call from open IR.
Resolution from a `Fn` uses that function's module and transitive imports, so
compile-time execution and verification have identical visibility rules.

Ordinary function signatures also describe a consumer boundary. A bodyless
declaration may use a dotted local name such as `tensor.matmul`; `ir.name(fn)`
returns that local semantic name, `ir.fns("edge")` enumerates declarations in a
loaded module, and `ir.accepts(op, fn)` applies the normal generic unifier to an
existing call. Known call results must also equal the signature after generic
substitution; `_` remains open. This supports precise element, shape, width,
and custom-format constraints without adding a target hierarchy or a second
pattern language.
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
Local binding names are simple identifiers: they cannot contain module dots or
reuse statement and literal words such as `return`, `true`, and `nil`. The text
parser and IR editing API enforce the same rule for generics, parameters, local
values, and loop variables. Top-level words remain contextual, so concise
bindings such as `for fn in functions` are valid inside a function body.

A name is declared at most once in one lexical block; parameters and generics
belong to the function's entry block for this rule. Nested blocks may reuse an
outer name because the generated target block provides a distinct scope.
Loop variables are stricter: an iterator cannot hide a visible binding, since
that name may also identify an implicit loop-carried value. Iterator names are
local to their loop, so consecutive or sibling loops may both use `i`.

Structured control carries only mutable bindings that an enclosed `for` or
`if` can change. A read-only `var` use refers directly to its dominating outer
value, and a binding forwarded unchanged by every body is omitted from the
operation inputs, block arguments, yields, and results. This normalization is
performed while the source is constructed; users keep normal lexical mutation
without paying one carried IR value per in-scope binding and control level.

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
`Blk` arguments and results created by `for` and `if` when they are changed in
that structure.

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
`keys`, `has`, and `get` also accept an `Attr` whose runtime kind is `dict`.
This lets reflected metadata such as `ir.meta(fn, "c")` keep its honest
structural return type while modules inspect nested policy dictionaries.
Mutable lists and dictionaries use the same indexed assignment syntax as a
model tensor; overload resolution selects the collection or tensor meaning:

```jog
var order = [0, 1, 2]
order[1] = 7
var report = {}
report["ops"] = len(ir.ops(m))
```

Indexed collection assignment returns an updated value internally, so it keeps
ordinary value semantics and participates in compile-time failure handling. A
list index outside its bounds is an error; assigning a dictionary key replaces
that key or inserts it without changing the language or IR operation set.
`kind` reports the structural value kind, and `len` applies to lists or
dictionaries. `int` and `str` project a checked `Attr` leaf into a typed scalar;
the same names also project structural `Ty` terms. These operations are in
`base`, leaving `ir` exclusively about `Mod/Fn/Blk/Op/Val` reflection.
`text` instead returns the canonical spelling of an `Attr` or `Ty`; unlike the
checked `str` projection, string attributes retain their quotes and escaping.
Compile-time `+` concatenates strings as well as lists and integers, so emitter
modules can assemble artifacts without a native helper. `replace` performs a
left-to-right, non-overlapping string substitution and rejects an empty search
string.

### Compile-time functions

There is no separate pass syntax. Any ordinary function accepting a `Mod` may
be selected explicitly by the embedding API or CLI:

```sh
joggle run opt.fold_add_zero model.jog -M modules
joggle query opt.untyped model.jog -M modules
joggle emit c.source model.jog -M modules > model.c
```

Parameterized module functions use repeatable `--arg` options. Each value is a
normal attribute literal, parsed by the same implementation as module metadata:

```sh
joggle query opt.count model.jog --arg '"operator +"' -M modules
joggle run c.place model.jog --arg '"static"' -M modules
```

The embedding equivalent is `parse(env, text, attr)` followed by the existing
`run` or `query` overload taking `span<const Attr>`. There is no target-specific
option registry or second configuration grammar.

### Diagnostics

Every command accepts `--diagnostics text|jog` before the command name. The
default `text` form is source-oriented. The `jog` form writes one canonical
`list<dict<str, Attr>>` to standard error:

```text
[{"column": 4, "file": "model.jog", "line": 9,
  "message": "unresolved function: missing", "severity": "error"}]
```

`file`, `line`, and `column` are omitted when a failure has no source location.
The representation is ordinary `Attr` syntax, not a second data model: an
embedding, script, or generated procedure can parse it with the same attribute
parser used for module metadata and `--arg`. Successful command output retains
its normal program, query, or artifact representation.

Calls inside that function remain ordinary calls. Compile-time execution
supports structured `for` and `if`, scalar operators, lists, and the universal
`Mod`, `Fn`, `Blk`, `Op`, and `Val` handles exposed by `ir`. Failed execution is
transactional. It does not evaluate arbitrary model functions or silently run
transforms while parsing.

Compile-time overload resolution retains the declared element type of an empty
list. Thus `let dims: list<Ty> = []` selects a `list<Ty>` overload even though
the runtime value has no member from which to recover `Ty`. This is part of the
ordinary type system, not a tensor-specific evaluator case.

`assert(condition, message)` is the ordinary failure boundary for module code.
A true condition returns `true`; a false condition stops compile-time execution
with the supplied message. When invoked through `run`, it participates in the
same whole-module rollback as every other runtime failure.

`ir.ops(m)` walks every function body in deterministic structural preorder,
including nested loops and conditions. Use `ir.ops(f)` for one function or
`ir.ops(b)` for one `Blk`. The optional fourth argument to `ir.replace` names a
single user operation; omitting it redirects every use after checking exact
structural type equality and dominance. `_` is not a rewrite wildcard; type
refinement uses `ir.type` explicitly. Successful edits advance
`Mod::revision()`, while a failed run restores both the IR and its prior
revision.

`ir.replace(m, op, literal)` is the corresponding definition edit. It turns a
single-result expression into a typed constant in place, preserving the `Op`
and `Val` handles, result name, type, metadata, source location, and printable
binding form. Frontend data normalization can therefore remove a source call
without inserting a replacement value or teaching memory planning a callee
name.

The list overloads of `ir.replace` and `ir.erase` apply one checked batch edit.
Replacement chains are collapsed before dominance checking, all uses are
redirected in one traversal, and all selected operations are removed together.
This is the scalable primitive for a transform; a module does not need a
special C++ rewrite class to avoid quadratic graph walks.

`ir.set(m, values, key, data)` accepts one metadata item per selected `Val`.
It computes loop- and branch-carried value families once, rejects conflicting
assignments to the same family, and applies the batch atomically. Scalar
`ir.set` remains convenient for one binding; analyses that annotate many
bindings do not repeatedly rediscover the same structural families.

`ir.type(m, values, types)` follows the identical batch contract for structural
types. It also treats making an inferred result annotation explicit as an IR
change even when the type tree itself is equal, because canonical source then
changes. Revision-based analysis caches therefore never observe a silent text
mutation.

`ir.fold(m, calls, fns)` is the explicit partial-evaluation primitive. Each
selected `Fn` must accept its corresponding call. Calls whose operands are not
static are left unchanged; successful scalar results replace their calls as a
single batch while retaining names, types, attributes, and source form. The
standard `opt.fold(m)` selects visible `base` functions, and its overload
accepts a user-selected `list<Fn>`. Parsing and emission never invoke it
implicitly.

The same list overload accepts a homogeneous set of `loop` and `branch`
operations plus the functions allowed during evaluation. It visits the
selected controls in structural order and commits every successful fold in one
transaction. A folded outer control invalidates its now-removed descendants,
which the batch skips. Non-static controls remain unchanged; a diagnostic or
invalid edit restores the complete batch. This avoids one full-program
snapshot per control without adding a control-flow dialect or a pass-specific
API.

`ir.def(v)` returns the operation defining a value; parameters have an invalid
definition detectable with `ir.live`. Together with `ir.users`, this completes
both directions of ordinary dataflow traversal. Byte attributes remain opaque
storage by default. `base.size(value)` and `base.byte(value, index)` provide
checked access to small payloads, while `base.hex(value, separator)` formats a
complete payload in one bounded linear operation. They do not add file or
ambient-memory access. `bytes + bytes` concatenates immutable payloads; it is
the same ordinary overloaded `+` used for integers, strings, and lists, not a
second binary-buffer API.

`base.ident(text)` encodes a source name as an injective ASCII identifier
fragment. Letters, digits, and ordinary single underscores remain readable;
`Z`, dots, reserved underscore runs, and other UTF-8 bytes use the unambiguous
`ZZ`, `ZD`, `ZU`, and `ZXhh` escapes. Emitters decide whether a role needs a
prefix: the C emitter preserves source names, gives anonymous compiler values
short role-based names such as `tmp_`, `slot_`, and `out`, and escapes C
keywords with a trailing underscore. A pointer result derived from a named
returned value uses a collision-checked `_out` suffix, leaving the source value
itself unchanged inside the function. This keeps name policy outside core IR
while preventing punctuation, Unicode, and prior underscore replacement from
silently collapsing distinct source names.

`ir.name(v)` and `ir.rename(m, v, name)` are the symmetric readable-name
operations. They matter when one source call is decomposed into several normal
calls: a module can preserve the externally meaningful result name without
accessing internal storage or generated identifiers.

`ir.rename(m, v, stem, ordinal)` combines collision-free local-name selection
and the rename into one atomic edit. It tries `stem + ordinal`, then increasing
ordinals until the name is unused in the value's function. The evaluator keeps
an invocation-local reservation set, initializes it from the live function on
first use, observes ordinary value renames, and invalidates it after edits such
as cloning or loop construction that can introduce named values. The overload
therefore avoids repeated whole-function scans while preserving the same
function-local uniqueness and transaction semantics. It is intended for
conversion and generation procedures; it is not a global symbol allocator.

Construction also uses ordinary overloaded functions. `ir.constant` and
`ir.call` insert leaves before a named operation. Passing a `str` to `ir.call`
deliberately creates an open or dynamically chosen symbol. Passing a live `Fn`
instead adds its module dependency when necessary and proves that the new call
resolves to that exact overload before committing. The typed form keeps the
short spelling when unambiguous and qualifies a collision. Qualified operators
use the compact callable form `base.+(left, right)`, so an exact binding remains
readable and round-trips without pretending to be the module's overloaded
infix expression. Pass a `Fn` whenever a transform must preserve an already
proved implementation identity.

`ir.clone` deep-copies an operation and its nested `Blk`s, while `ir.move`
changes order within one function only when all operands and users remain
dominated. A cross-`Blk` move is limited to immutable calls and constants and
must not introduce a binding collision; it cannot move a terminator, cross a
function, or place an operation inside its own nested body.
`ir.kind(op)` returns `call`,
`constant`, `loop`, `branch`, `return`, or `yield`; `ir.blks(op)` exposes
nested bodies. A terminator supplies an insertion point even for an otherwise
empty `Blk`, so there is no stateful builder object. Constructed result types
are `Ty` or `list<Ty>` values rather than strings; reflected and computed types
therefore pass back into the editor without serialization.
Cloning deterministically freshens a copied `let` or `var` declaration at the
insertion scope, so the source and clone may coexist without textual capture.
Loop and condition results keep the name of the mutable binding they update;
their iterators and carried arguments are already nested in fresh `Blk`s.
The five-argument overload remaps selected values captured from outside the
copied subtree:

```jog
let copy = ir.clone(m, loop, before, [old_base], [new_base])
```

The two lists are a parallel typed lookup table. Entries unused by a particular
subtree are allowed, so one accumulated mapping can be reused while cloning a
sequence of operations. Passing `list<Op>` instead of one `Op` clones that
ordered sequence as one edit: values produced by an earlier source operation
are rewired to the corresponding earlier copy, names are freshened across the
whole sequence, and use lists are rebuilt once. This is the preferred form for
unrolling or rebuilding a `Blk`; it preserves the same checks without making
every source operation a separate global mutation. Every replacement that is
actually selected must have
exactly the same type and dominate the insertion point; values defined inside
the source cannot be replaced through this interface. All checks precede
mutation. A checked operation-clone rejection returns an invalid `Op` to
textual code, so a module can inspect it with `ir.live` and issue its own
diagnostic; it never becomes a half-created operation. Argument type or arity
errors remain compile-time call errors rather than invalid handles.
This is the small structural primitive used by removable loop split, fusion,
reorder, and unroll modules; it does not introduce a schedule object or a
second loop IR.

`ir.assign(m, before, target, value)` is the matching construction primitive
for an existing mutable binding. It accepts only an equal-typed value that
dominates the insertion point, and only a `var` or a loop/branch-carried value
may be the target. The operation prints as the ordinary source assignment
`target = value`; it is not an operator hook or a tensor-specific update.
Structural passes use it when rebuilt control flow must keep the next carried
value explicit and round-trippable. An immutable parameter or `let`, a type
mismatch, and a dominance violation all reject without changing the module.

The overload `ir.clone(m, fn, name)` copies an entire function signature,
metadata, generics, and nested body into `m`. This is the function-level
primitive for generated helpers and local template materialization; it does not
introduce a builder or a kernel class. A source from another module becomes
visible through the normal dependency graph in the same revision commit. Calls
retain their short spelling when it remains unambiguous, conflicting references
are qualified, and recursive calls target the new local function. External
declarations and duplicate overload signatures are rejected without mutation.
The four-argument overload binds every generic at once:

```jog
let relu4 = ir.clone(
  m, template, "relu4", [ty("f32"), ty("[4]")]
)
```

A non-empty argument list must match the generic arity and constraints and may
not contain `_`. The copied `Fn` has no generic parameters: bindings are
substituted through parameter, result, local, and call types. Integer, Boolean,
and recursively typed list parameters that occur as ordinary operands are
materialized as normal constants and `base.list`; type parameters are replaced
structurally in types and explicit call arguments. A source that attempts to
use a type object as an ordinary runtime operand is rejected transactionally.
Passing no generic arguments retains the generic function as described above.
`ir.bind(m, op, fn, name, params)` is the call-aware form used by reusable
specialization policies. It infers generic terms from `op`, clones `fn`,
materializes the selected structural constant arguments inside the clone,
removes their parameters, marks the clone local, and retargets the call with
the remaining values. Indices must identify constants or recursively literal
`base.list` values. The whole edit is atomic; it never leaves an uncalled clone
or a partly rewritten signature.
`ir.rename(m, fn, name)` changes a local function symbol and every call that
resolves to that exact overload. It retains short call spelling when resolution
stays unique and qualifies only collisions. An alpha-equivalent overload at the
new name rejects the edit. A generic-dependent call that still includes the
function in its candidate set also rejects the rename because one call spelling
cannot preserve two overload families after only one candidate moves.
`ir.erase(m, fn)` rejects a function with callers
outside its own body. This includes a generic-dependent call that cannot yet
select one overload but still contains the function in its visible candidate
set; deleting that overload would otherwise change the meaning of a later
specialization. Calls already resolved to a different overload do not block
deletion. Otherwise the function, generics, parameters, nested blocks,
operations, and results become invalid together.

`ir.constant` checks the representation of intrinsic literals, including
nested lists, before editing the module. User-defined type constructors retain
their own representation policy, so a custom format may deliberately wrap an
integer, byte string, or another attribute without adding a core case.
Constructed and renamed callees must use the same qualified-name, explicit
generic, or operator spelling accepted by the parser, preserving textual
round trips without reserving a module vocabulary. `module.+(a, b)` is the
qualified spelling of `module.operator +`; ordinary `a + b` remains the
unqualified overload expression.

Structured construction follows the same rule. `ir.loop` receives iterator
names, source values, and carried values, then returns an `Op` with one body and
an initial `yield`. `ir.branch` returns an `Op` with two initially forwarding
arms. All carried values are validated before their source bindings become
mutable, so failure leaves both text and revision unchanged. `ir.args(blk)`
obtains `Blk` arguments and `ir.args(m, op, values)`
reconnects any operation, including `return` and `yield`, while enforcing its
arity, dominance, structured-control types, and the complete signature of a
resolved call. The C++ editor accepts the current `Env` explicitly for the same
check; textual functions receive it from their evaluator. A named local
selected as carried state prints as the corresponding ordinary `var`; users
never construct `Blk` objects or source-presentation records themselves.

`ir.rename` treats the versions of a carried mutable binding as one lexical
name. Calling it on the incoming value, a loop or branch `Blk` argument, a
yielded update, or the structure result renames the whole chain atomically.
Calling it on a loop iterator also updates the loop header. This makes generic
`Blk` traversal safe to edit without exposing the printer's bookkeeping.

Function signatures are reflectable without a second symbol API.
`ir.find(m, "body")` returns the exact local `Fn`, `ir.live` tests whether it
was present, and `ir.params`/`ir.returns` expose its inputs and result types.
`ir.find("module.name")` performs exact lookup in the loaded environment and
returns the same invalid handle on absence or ambiguity. This form lets one
module pass one of its ordinary functions to a reusable library without a
registry or generated binding; qualification keeps that choice visible in
source. When a symbol is overloaded, `ir.find(name, [parameter types])`
selects the unique nongeneric declaration with that exact parameter list; the
three-argument local form adds the edited `Mod` first. This avoids inventing
alias names merely to pass an overloaded function as a value.
`ir.generics(fn)` exposes the declared generic `Val`s. `ir.generics(op)` instead
returns the explicit structural `Ty` terms written on a call, or an empty list
when none were written. This is enough for a transport module to represent a
nested source graph as an ordinary function and for a separate relation module
to reason about it.

Value and function contracts are edited through `ir.type(m, value, type)` and
`ir.returns(m, fn, types)`. Explicit call terms use
`ir.generics(m, op, types)`. The latter reconstructs the call from its base
symbol and structural terms, then applies the same visibility, generic-kind,
argument, and result checks as `ir.rename`. An invalid binding leaves the call
and module revision unchanged. A format module can consequently recurse over
`Ty` once and apply the result to values, function results, and type-constructor
calls without inspecting printable callee text.

Renaming a call checks any visible destination against the existing arguments
and results; an unknown destination remains an open call. Call conversion that
also changes operands uses `ir.retarget(m, op, callee, args)`. It applies the
same visibility, overload, generic, argument, result, and dominance checks as an
ordinary source call before changing either the callee or its operands. A
failed match returns `false` with the original call intact. Bridge modules can
therefore try a semantic function without constructing a parallel legality
system or relying on whole-pipeline rollback.

`ir.retarget(m, op, fn)` selects an already resolved function directly. When
the function belongs to another module, the edit adds that dependency and
retargets the call as one transaction. The selected function must be the
unique overload for the existing arguments and results. This overload is
suited to implementation selection because it carries a checked `Fn`, not a
string alias or a duplicate signature test.
The four-argument form `ir.retarget(m, op, fn, args)` performs the same exact
function selection while replacing operands. A specialization can therefore
reuse an existing instance after removing bound compile-time parameters.

Function bodies are exposed by an explicit edit, never by loading a module.
`ir.resolve(m, op)` applies normal import, qualification, overload, and generic
resolution to a call and returns an invalid `Fn` when the call remains open.
`ir.target(m, op)` returns the selected function's qualified symbol, or the
surface call spelling when resolution remains open, so ordinary passes do not
duplicate this normalization.
`ir.expand(m, op, fn)` substitutes the selected ordinary
function body at that call, remaps its parameters and nested control flow, and
preserves the caller's visible result bindings. The edit is atomic; a missing
body, signature mismatch, unrepresentable compile-time argument, or metadata
whose policy has not been chosen leaves the module unchanged.

Expansion preserves the implementation's lexical module context. Calls that
already resolve to the same public function in the destination retain their
short spelling; a collision is retargeted to the exact qualified declaration.
Local helper dependencies are copied only inside the transaction, recursively
expanded at their call sites, and removed before the edit commits. They do not
become public exports or permanent helper functions, and an unrelated local
function with the same name in the caller cannot capture the call. A recursive
cycle formed entirely by imported local helpers is rejected transactionally;
such recursion must remain a public callable boundary or be rewritten as
structured control flow before expansion.
Likewise, a body containing mutable statements or nested control flow is not
inserted into a short-circuit expression region, because that edit has no
faithful surface syntax and could change evaluation order. The caller can keep
the function boundary or place the call in an ordinary statement block, where
the same body expands normally.

`ir.expand(m, ops, fns)` performs the same edit for aligned `list<Op>` and
`list<Fn>` arguments. Pairs are applied in order and commit as one transaction;
length mismatch or failure of any pair restores the module. Use the list form
when a traversal has already selected a network-wide frontier. It avoids one
full module snapshot per call without introducing a pass class or another IR.

`ir.match(op, fns)` applies the same specificity ordering to an explicit list
of function handles. No match returns an invalid `Fn`; equally specific matches
are an execution error rather than a declaration-order choice. When the chosen
function is passed back as `ir.match(op, fn)`, the result is the ordered
`list<Ty>` of generic terms inferred for that exact call. The two overloads use
one resolver, including explicit terms and known result types; the second form
therefore feeds `ir.clone` without reconstructing shapes or element types.
When the chosen function comes from another module and its local name denotes
the resolved source symbol, `ir.expand` treats it as an alternative
implementation, adds its owning module only if not already visible, and rolls
back both changes on failure. The dependency and substituted body form one
revision commit. Thus an unqualified source call and a module-supplied
implementation still use ordinary symbol resolution rather than a string alias
table.

The same matching rule can select a bodyless implementation. In that case
`opt.apply` uses `ir.retarget` rather than `ir.expand`: the high-level model
keeps calling its semantic function, while the selected module supplies the
external ABI declaration. Body presence is therefore the only distinction
between inspectable replacement code and an external implementation; neither
requires an operation registry or a target object.

The policy overloads of `opt.apply` and `opt.instantiate` accept ordinary
functions. A `fn(Mod, Op, Fn) -> bool` predicate filters each type-compatible
candidate before the usual specificity check. A
`fn(Mod, Op, list<Fn>) -> list<Fn>` selector instead sees the complete
compatible set and returns zero or one member, which lets user code resolve
equally specific implementations by shape, layout, cost, or device metadata.
Configured overloads pass one ordinary dictionary to the corresponding
`fn(Mod, Op, Fn, dict) -> bool` or
`fn(Mod, Op, list<Fn>, dict) -> list<Fn>` callback. The dictionary is owned by
the invoking module: the compiler assigns no names or units to its entries.
Both forms must be read-only and are checked by module revision. The selector
result is also checked for cardinality and membership. These constraints
therefore stay beside implementation modules instead of becoming core
attributes or operation-specific branches.
`opt.candidates(m, op, impls)` exposes the same symbol and type-compatible set
for inspection without applying a policy or editing the module. Repeated
handles are removed while distinct equal-signature implementations remain
visible to policy.

Open attributes can also define module-owned relations without a second rule
language. `ir.where(items, key, value)` filters an explicit `list<Fn>`,
`list<Op>`, or `list<Val>` by exact metadata; a list-valued attribute matches
when it contains the requested value. The result retains the input handle
type, so a transform can pass selected operations or values directly to the
safe editing API. `ir.invoke<R>(m, fn)` executes an ordinary
`fn(Mod) -> R`, including a whole-module analysis or generator, without a
dummy subject. For example:

```jog
let report = ir.invoke<dict>(m, ir.find("stat.summary"))
```

`ir.invoke<R>(m, op, fn)` executes a selected ordinary
`fn(Mod, Op) -> R` in the current transaction. Its overloads pass one or two
typed compile-time values to a matching callback; a dictionary can carry
named policy, while a `Fn` can identify one candidate implementation without
an option class, serialized handle, or wrapper object. For example,
`ir.invoke<bool>(m, op, policy, candidate, config)` requires a
`fn(Mod, Op, Fn, dict) -> bool` callback.
The subject type is generic and inferred, so the same overload can invoke a
typed relation over a whole `list<Op>` candidate, such as a
producer/consumer pair, without encoding live handles in serializable
metadata. Empty and nonempty lists use the same callback type. A handle passed
directly, inside the subject, or inside either extra argument must be live.
The explicit result type keeps dynamic invocation typed even though `Fn` is a
runtime handle. It rejects
generic or incompatible callback signatures before execution, validates the
returned value, and rolls the enclosing compile-time entry back normally on an
error. A callback that attempts an IR edit rejected by the safe editing API is
also an invocation error, even if the callback catches the edit's false/invalid
return; this prevents a work-queue driver from silently committing the rest of
a partially rejected wave. A directly invoked entry may still probe a rejected
edit and inspect its return without changing the module. A relation driver uses
`ir.invoke<bool>`; a cost traversal can use
`ir.invoke<int>` without adding another callback API. Attribute names, result
types, and selection policy remain module-owned: core does not reserve `on`,
operator names, relation kinds, or measurement units.

Invocation also accepts an inactive function copied with `ir.clone`: edit its
body through the existing IR API, then invoke the copy. The original definition
is not changed. This is the same execution path, not a special kind of pass or
a claim that arbitrary compiler functions can be optimized automatically.
Invocation inherits the enclosing execution boundary: it does not grant a
read-only query mutation rights, and failure rolls back the enclosing entry.
The result is one compile-time value of the explicit type, not arbitrary
runtime tensor execution.
Execution structure and dispatch caches distinguish source revisions, so
re-invoking a copied function after editing its inactive body observes the new
body. Cached snapshots live only for the enclosing evaluation. This does not
specify safe self-modification of an executing function or fine-grained
incremental recompilation.

`ir.uses(m)` returns the module's declared dependencies and
`ir.use(m, name)` adds one idempotently. Dependency edits advance the same
module revision and participate in compile-time rollback. The named module
must already be loaded in the current environment; the edit never performs a
hidden load or creates an unresolved dependency. This lets an
explicit frontend bridge introduce the semantic library whose qualified
functions it selects; parsing a frontend never does so implicitly.
`ir.trim(m)` removes dependencies that no longer own a resolved external call
or a visible structural type constructor. Type dependencies use the same
visible constructor-family lookup as verification, including constructors whose
name differs from their module. It prefers a direct provider and keeps a
declared transitive provider only when the program still needs it.
Metadata strings are provenance rather than symbol references, so exposing a
module body does not retain its source package accidentally. The operation is
deterministic, idempotent, and covered by the enclosing transaction.
Module verification likewise rejects declared dependencies absent from the
environment, self-dependencies, and duplicate dependencies, so textual modules
and embedding edits have the same closure rule.
Verification may fill types that follow uniquely from visible signatures and
structured control flow. Those refinements commit only when the complete
module verifies; a failed verification restores every prior value type while
retaining its diagnostics.
`ir.revision(m)` exposes the monotonically increasing whole-program revision to
module code. `ir.revision(f)` exposes the executable-body revision of one
function. An edit whose owner is known advances only that function revision;
dependency-set and other structure-wide edits conservatively advance every
live function. This is the invalidation key for cached execution plans, while
the `Mod` revision remains the transaction and reporting clock.
It is intended for convergence and invalidation checks; it is not serialized
into the model and cannot be used as a stable model identifier.
`ir.key(v)` similarly returns a constant-time identity for a live `Val` in the
current `Mod`. It is useful for temporary maps and generated register names and
remains valid only while that handle is live. It is not stable across printing,
reparsing, or another construction history and must not enter a persisted
semantic contract.

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
`ir.meta(op)` support structural comparison. `limit` is checked: a final round
that still changes the module is a transactional failure, not a successful
partial fixed point.

A policy entry may be an unqualified call spelling or a resolved symbol such
as `base.operator +`. The former deliberately covers every matching call the
policy author selected; the latter covers only that module's function. Built-in
identity folding and cleanup use resolved `base` symbols, so defining a custom
`+`, `-`, or `*` never inherits the built-in algebra or purity assumptions.

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
return type patterns, and its exact revision delta. `clone` records the source
template, copied symbol, concrete generic bindings, signature, and revision
interval of a function-level materialization. These are structural dictionary
fields, not a second event class or callback interface.
Each top-level step also contains `calls` and `cached` dictionaries. Their
keys are qualified source-function names and their values are invocation
counts for that step,
including read-only helpers that do not appear in the mutation trace. Counts
in `cached` identify calls served by `[memo]`. Both are deterministic
structural evidence, not timings or native-intrinsic counts;
they make repeated compile-time analysis visible without changing report
reproducibility.
The named entry is selected by the same overload resolver as an ordinary DSL
call, using `Mod` as its argument type. A module may therefore expose both
`convert(m)` for the default workflow and `convert(m, rules)` for explicit
composition without making the CLI name ambiguous.
The embedding API also accepts a `Fn` handle in
`run(env, function, target, report, args)` and
`query(env, function, target, result, args)`. The function can belong to a
separate, verified `Mod` holding compiler definitions; only `target` is edited
or inspected. This is useful when deriving a compiler function without copying
it into a model that will later be emitted. It is not a new IR kind: both
modules have the same representation and editing rules. A handle query uses
the same verified read-only execution boundary and does not reuse the
name-based query cache; a handle run uses the same target verification and
rollback as a named run. The caller
verifies the compiler-definition module after editing it. A copied function
does not acquire an automatic proof of equivalence to its source.
When a cloned source function calls private helpers, `Mod::clone` copies its
resolved transitive private call closure as private functions in the compiler
module and rebinds those calls to the copies. Public calls keep explicit
module dependencies. The whole derivation rolls back if any helper cannot be
copied; cyclic private-helper closures are currently rejected. This avoids
requiring the source module to make its implementation helpers public, but
copied code growth and execution cost are the caller's responsibility. The
closure covers resolved direct calls, not dynamically selected function
handles or opaque native implementation bodies. The
native handle overload takes `Attr`-representable trailing arguments; typed
`Fn` or `Op` trailing values currently require a source-module call instead.
The same report is available from the CLI without mixing it into printed IR:

```sh
joggle run edge.prepare model.jog --report run.attr -M modules
joggle run edge.convert edge.plan edge.prepare model.jog \
  --report run.attr -M modules
joggle run edge.prepare model.jog --timing run-timing.attr -M modules
```

`print(Attr)` and `print(Mod)` are ordinary overloads in the embedding API;
the CLI writes their corresponding deterministic textual forms. With multiple
function names, `run` uses the same host-sequence overload described below:
the report contains one entry per function and any failure rolls the complete
sequence back. `--timing` writes the separate `RunTiming` record described
below; it may be combined with `--report`, and failed runs still write the
timing trace while leaving the input IR unchanged.

`query(env, name, mod, result, args, report)` embeds an ordinary function as a
read-only analysis. Its first parameter is `Mod`; subsequent parameters receive
the explicit `Attr` arguments, and it returns one value representable as
`Attr`. A currently verified mod is inspected directly under the evaluator's
read-only guard; otherwise a private copy is verified first. Mutation
intrinsics fail before editing, and the monotonic revision remains a backstop,
so the original mod is unchanged. Successful results are dependency-aware and
may be reused. During execution Joggle records
the subject functions observed through `Fn`, `Blk`, `Op`, and `Val` handles.
Function operation/value enumeration records ordered handle collections;
operation and value reads record exact typed snapshots. Name lookup and
dependency enumeration additionally record the mod's structural revision;
module-wide intrinsics without a narrower read set conservatively record the
whole `Mod` revision. A local edit therefore preserves an unrelated answer,
while entity, function, symbol/dependency-structure, and whole-mod reads
invalidate at their respective boundaries. The `cached` field reports whether
reuse occurred. `QueryReport` names cold, environment, whole-revision,
structure-revision, function-generation/content/shape,
operation-generation/revision, and value-generation/revision misses; reports
observed function, collection, operation, and value counts and scope; reports
whether execution reused a successful verification stamp; and
separates cache lookup from snapshot copying, snapshot verification, evaluator
execution, result validation, and total miss execution time. These counters are
measured inside the boundary and exclude caller-side parsing and artifact
emission. Every public IR edit must advance the monotonic revision.
The CLI `query` command invokes the same boundary for a function whose only
argument is `Mod` and prints its canonical `Attr` result. Analyses needing
explicit arguments continue to use the embedding overload; the command line
does not invent an argument mini-language.

Embedding code that repeatedly applies an ordered stage set can keep a
`ReactiveSchedule`. On its first `run`, the schedule executes every named
stage and records function generation/content, exact operation/value
collections and entities, structural or whole-mod reads, and changed functions
observed at each boundary. Later calls compare those exact snapshots and
generation/revision stamps. A directly stale stage is selected; its
recorded outputs then invalidate downstream stages whose inputs intersect
them. All selected stages use the ordinary sequence transaction, so a later
failure restores earlier edits. `ReactiveRunReport` distinguishes cold,
environment, argument, revision, and upstream misses and reports selected and
reused stage counts. This is an embedding facility rather than new `.jog`
syntax, and the stage list is configured once rather than rediscovered from a
hard-coded compiler pipeline.

Classified collection and entity dependencies can distinguish unrelated edits
inside one large `Fn`. A stage can use `ir.affected` or `opt.update` internally
to restrict its operation-level work; the schedule decides which stage waves
need to run. Structure-sensitive, whole-mod, and still-unclassified
observations retain their corresponding conservative invalidation boundaries.

The CLI `emit` command uses that same read-only boundary but requires a `str`
or `bytes` result and writes its contents verbatim. An emitter is therefore an
ordinary module function, not a target interface or a privileged pass kind.
Mutation during emission is rejected by `query`, and redirecting standard
output is sufficient to create text or binary artifacts.

For a host-selected sequence, the embedding API also accepts
`run(env, span_of_names, mod, report)`. It executes the same ordinary functions
in order and rolls the complete sequence back if any step fails. One snapshot
covers the complete sequence; validation and reports remain per function. The
CLI form is `joggle run fn1 fn2 ... model.jog`. A source wrapper, embedding
sequence, and CLI sequence therefore differ only in where the list of calls is
chosen, not in their IR or function semantics.

Individual `ir` mutations are composable steps rather than transaction
boundaries. A transform may create a replacement with the old binding name and
then remove the old definition before it returns. Final verification mirrors
the source parser's lexical rules; unresolved duplicate declarations or a loop
variable that hides a visible binding reject and roll back the complete `run`.

Embedding code may additionally call
`run(env, names, mod, report, timing)`. `RunTiming` separates the transaction
snapshot and initial verification from per-function resolution, evaluation,
and commit verification. Metadata-only transactions keep per-object undo
records and avoid copying the structural store. The `structural_snapshot`
flag reports whether a later structural intrinsic caused the transaction to
materialize its full rollback snapshot; `snapshot` includes both the initial
rollback-state setup and any delayed structural copy. Each step also records
its function, input/output revisions, verification-stamp reuse, and success.
Research builds configured with `-DJOGGLE_EVAL_COUNTERS=ON` additionally set
`counters_enabled` and report evaluated operations, frame lookups/probes/writes,
and dynamic dispatch hits/misses. Default builds leave those counters disabled
so measurement hooks do not tax the evaluator hot path. Timing is intentionally
absent from `report`, so the same run with or without measurement produces
equal structural reports. A failed transaction keeps its timing trace while
rolling back the mod, making failed work measurable rather than silently
discarding it.

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
are errors. Keys follow the same dotted-identifier spelling in parsed source
and IR edits, ensuring every annotated module remains printable and
round-trippable. On a statement, the dictionary belongs to its root `Op`, whether
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

Metadata edits advance the owning function revision, so cached queries that
observed that function are invalidated while unrelated function-local answers
remain reusable. They preserve an existing structural-verification stamp:
metadata is deliberately outside parser binding, type inference, dominance,
and IR-shape validity, and the verifier never interprets extension-owned keys.
Whole-mod queries still observe the new `Mod` revision. Editing metadata does
not imply that a module policy which consumes it may reuse its own result; that
policy's recorded function or whole-mod dependency determines invalidation.
