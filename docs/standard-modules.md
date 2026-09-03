# Standard Modules

Joggle separates the language ABI from reusable module vocabularies. This is
an ownership boundary, not a lowering order.

## Prelude is the language ABI

`language/prelude.joggle` is embedded into the library and linked ambiently by
every `Compiler`. The installed copy exists for editors, formatters, and API
inspection; repository resolution never chooses another Prelude. Changing Prelude
therefore changes the language ABI and requires the same compatibility care as
changing a public C++ header.

Prelude owns only concepts that every Module must agree on:

- compiler domains: `int`, `real`, `bool`, `string`, `type`, `attr`, `bytes`,
  `function`, and `list<D>`;
- the core-represented whole-module artifact type `module`;
- native IR scalar types and `index`;
- basic type and function contracts;
- deterministic compiler-domain arithmetic, comparison, logic, and `range`.

`function` maps to the materialized `joggle::Function` body of a
`joggle::Module::FunctionDecl`; `module` maps to one `joggle::Module`. Both
mappings are supplied by the core because staged execution and whole-module
composition require a common host representation. No other artifact type is
privileged.

Prelude distinguishes two related interfaces:

- `scalar` promises a fixed `storage_bits` field;
- `numeric` promises that ordinary numeric functions are meaningful.

Native integers and floating-point types implement both. `i1` is a fixed-width
logical scalar but is not numeric. Target-sized `index` is numeric but does not
promise a fixed storage width. A custom fixed-point, posit, or packed type opts
into the exact contracts it supports.

## `arith`: Residual scalar computation

`arith` is an ordinary installable Module, not part of source syntax. It owns
the common Residual vocabulary for constants, casts, numeric arithmetic,
comparison, and selection. Familiar symbols such as `+` are aliases of these
ordinary functions.

`arith.elementwise`, `arith.commutative`, and `arith.comparison` are function
contracts used by generic transforms. There is one standard `elementwise`
identity; tensor and NN functions reuse it instead of declaring unrelated
markers with the same spelling.

Arithmetic, comparison, bitwise, shift, and logical operations are separate
ordinary functions with operator spellings. A transform therefore matches an
exact declaration such as `arith.less` or `arith.shift_left`; it never decodes
a predicate string. Their generic constraints admit custom fixed-width types
that opt into Prelude's `scalar`, `numeric`, `integer`, or `logical`
interfaces, so a four-bit Module does not need a parallel arithmetic
dialect.

Literal materializers implement `prelude.literal`. Their distinct names are
intentional: an integer token may materialize an integer or a floating-point
module value, and those declarations otherwise have the same input domain.
The literal-selection mechanism uses the interface and result constraint, not
the function name.

## `tensor`: value shape, not storage

`tensor.ranked<E, S>` represents a tensor value with compiler-known element
type and shape. `tensor.ranked_tensor` exposes these facts to generic code.
`tensor.unranked<E>` is deliberately weaker and does not claim that interface.

The Module owns structural value functions only. It does not own NN operators,
buffers, layouts, devices, or schedules. `transpose` accepts only its
permutation; the ordinary `tensor.permute_shape` compiler function derives the
result with Prelude list operations. Callers cannot repeat an inconsistent
output shape, and the evaluator remains Module source rather than compiler
core.

`tensor.constant(resource)` introduces an immutable tensor through a stable
data identifier. Element type and shape are inferred from the expected
`tensor.ranked` result. The payload lives in the owning Module's
content-addressed data table, not in the textual Op. The
`tensor.immutable_data` function interface lets transforms discover constants
from other vocabularies, such as `onnx.constant`, without depending on their
producer. This keeps large parameters out of source text while retaining a
single transactional compiler artifact.

## `nn`: common inference semantics

`nn` owns framework-independent inference functions over `tensor.ranked`.
Shapes are part of signatures and convolution or pooling extents are computed
by an ordinary Known helper. Layout-bearing names such as `conv2d_nchw` are
explicit; the Module does not infer a hidden default layout.

The Module does not prescribe an importer, quantization policy, target,
schedule, bufferization scheme, or hardware cost. Those belong to separate
Modules that call or convert the NN vocabulary.

## `mem`: extensible storage semantics

`mem.reference` is a type interface exposing `element_type`, logical `shape`,
`layout_type`, and `space_type`. `mem.ref` is one generic implementation, but a
Module may define a packed, tiled, banked, remote, or device-specific reference
type and implement the same interface. Layout and address space are themselves
open type interfaces; they are not strings interpreted by the core.

`alloc`, `view`, `load`, `store`, `copy`, and `dealloc` operate on the interface
rather than one concrete reference type. Dynamic indices remain ordinary
`index` operands. Static view offsets and strides are named compiler-domain
properties, while the result type carries its logical shape and layout.

The `read`, `write`, `allocate`, `release`, and `alias` function interfaces
describe effects for analysis and legal reordering. Memory Ops remain ordered
inside their Block, so no artificial token is required merely to preserve
program order. A target Module can add dependencies or scheduling constructs
when it needs concurrency semantics. Capacities, banks, latency, and device
instances are intentionally absent from `mem`.

## Dependency and Module rules

Standard dependencies are ordinary imports: `tensor` imports `arith`, while
`nn` imports both. A tool loading loose source files must supply direct and
transitive dependencies; an installed Module release is resolved through normal
search paths and lock files.

The installed CMake package exports `Joggle_MODULE_DIR`, which names the
directory containing these ordinary `.joggle` files. Consumers may load files
from that directory explicitly or install selected releases into their Module
repository. Joggle does not make the standard Modules ambient or search them
implicitly; only Prelude has language-wide visibility.

Add a Module when a project introduces a serializable vocabulary
or artifact contract. It should add a behavior library when declarations need
native algorithms or external I/O. It should not add a core class, keyword, or
registry merely to name a pipeline role.
