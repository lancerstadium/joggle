# Standard Modules

Joggle separates the language ABI from reusable module vocabularies. This is
an ownership boundary, not a lowering order.

## Prelude is the language ABI

`language/prelude.joggle` is embedded into the library and linked ambiently by
every `Compiler`. The installed copy exists for editors, formatters, and API
inspection; repository resolution never chooses another Prelude. Changing Prelude
therefore changes the language ABI and requires the same compatibility care as
changing a public C++ header.

Prelude owns only concepts that every extension must agree on:

- compiler domains: `int`, `real`, `bool`, `string`, `type`, `attr`, `bytes`,
  `function`, and `list<D>`;
- the core-represented whole-module artifact type `module`;
- native IR scalar types and `index`;
- basic type and function contracts;
- deterministic compiler-domain arithmetic, comparison, logic, and `range`.

`function` maps to the materialized `joggle::ir::Function` body of a
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

`arith.elementwise` and `arith.commutative` are function contracts used by
generic transforms. There is one standard `elementwise` identity; tensor and
NN functions reuse it instead of declaring unrelated markers with the same
spelling.

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
buffers, layouts, devices, or schedules. `transpose` currently receives both
the permutation and `output_shape`: this is explicit because the core does not
pretend to prove that a permutation produces a shape. A future list-algebra
Module may compute and verify this relation without changing the tensor type.

## `nn`: common inference semantics

`nn` owns framework-independent inference functions over `tensor.ranked`.
Shapes are part of signatures and convolution or pooling extents are computed
by an ordinary Known helper. Layout-bearing names such as `conv2d_nchw` are
explicit; the Module does not infer a hidden default layout.

The Module does not prescribe an importer, quantization policy, target,
schedule, bufferization scheme, or hardware cost. Those belong to separate
Modules that call or convert the NN vocabulary.

## `buffer`: explicit effects without a device model

`buffer.buffer<E, S, Space>` describes storage by element type, logical shape,
and an open string address-space name. It does not declare capacities, banks,
latencies, or a global machine.

Memory functions thread `buffer.token` explicitly. Their `index...` operands
are module values, so dynamic access is representable; shape and address space
remain Known type parameters. `reads`, `writes`, and `allocates` are function
contracts for effect-aware transforms. Buffers may contain any module type,
not only Prelude scalars.

## Dependency and extension rules

Standard dependencies are ordinary imports: `tensor` imports `arith`, while
`nn` imports both. A tool loading loose source files must supply direct and
transitive dependencies; an installed Module release is resolved through normal
search paths and lock files.

An extension should add a Module when it introduces a serializable vocabulary
or artifact contract. It should add a behavior library when declarations need
native algorithms or external I/O. It should not add a core class, keyword, or
registry merely to name a pipeline role.
