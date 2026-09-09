# Language

`.jog` is Joggle's only source and readable IR format. It describes reusable
modules, functions, types, control flow, and explicit compile-time workflows.
It is intentionally not a second pipeline or kernel language.

The implemented surface is conventional:

```jog
module demo
use tensor

fn matmul<T, M, N, K>(
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
IR operation kind.

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

During verification, a call to a known local or qualified module function is
checked against its declaration. Generic arguments may be written explicitly
or inferred recursively from argument types, and the resulting substitution is
applied to every call result. Conflicting bindings, wrong argument counts, and
wrong concrete types receive source-located diagnostics. For example,
`sat.add(a, b)` over two `sat<8>` values has result type `sat<8>`, while mixing
`sat<8>` and `sat<16>` is rejected. Unknown calls remain valid open IR so a
frontend can transport source operations before a semantic bridge is loaded.

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

### Function metadata

Square brackets hold an open metadata dictionary rather than a fixed set of
compiler keywords:

```jog
[entry, stage: "select", policy: {modes: ["fast", "small"]}]
fn choose(m: Mod) -> bool { return true }
```

A bare name means `true`; values use normal `Attr` literals. Repeated brackets
are accepted and canonical printing merges them in key order. Duplicate keys
are errors. `Fn::meta` exposes the same data to C++, while `ir.has` and
`ir.meta` expose it to textual functions.

No metadata name changes parsing, binding, or the IR shape. A native library
may bind any matching body-less declaration; no marker is required and a
function with a body cannot be rebound. Useful module-defined keys include
`role`, `stage`, `target`, and `cost`, but none is owned by the core. Named type
constructor declarations and overload-set resolution remain M6 work; structural
parametric matching and result inference are already implemented.

Metadata becomes behavior only when an explicitly selected function queries
it. A transform may use `[rewrite: "lab.fused"]` to choose a replacement call;
an emitter may use `[target: "board-name"]`; a search procedure may attach a
structural `cost` dictionary. Installing such a module does not register a new
language keyword or silently execute any of these policies.
