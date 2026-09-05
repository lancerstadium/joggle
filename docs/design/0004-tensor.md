# Design 0004: Tensor refinement

Status: accepted; implementation in progress

Joggle uses one textual language and one `Mod`/`Fn`/`Val`/`Op`/`Blk` object
model. This does not mean that tensor values and mutable storage have the same
semantics. A sequence of explicit passes refines ordinary fns from pure tensor
values into loops, storage operations, and finally target operations. Every
intermediate Mod is printable and verifiable Joggle; there is no second kernel
language or `Kernel` owner.

## Ownership

The C++ core provides only mechanisms that do not depend on AI, tensors, or a
machine:

- parsing and canonical printing of fns, including ordinary structured loops;
- types, calls, CFG, SSA values, effects, dominance, and use-def relations;
- transactional Fn construction and editing;
- whole-Mod dependency traversal, staging, native bindings, and verification;
- the APIs needed by a compiler-time fn to construct another Fn or Mod.

Shipped Mods own all domain vocabulary and policies:

| Mod | Owns |
| --- | --- |
| `tensor` | pure tensor type, construction, indexing, reduction, and shape/index algebra |
| `nn` | bodyful mathematical definitions composed from `tensor` and scalar fns |
| `transform` | domain-independent rewriting, inlining, composition, and call-graph resolution |
| `mem` | logical views and destinations, storage effects, and tensor-to-memory refinement; no machine capacities |
| target Mods | optional layouts, instructions, mappings, cost models, and emitters |

Target Mods are future work. There is no `kernel` Mod: “kernel” is only an
informal name for a Fn after tensor refinement has made iteration and storage
explicit.

The core never switches on a tensor constructor, `nn.conv`, `mem.store`, or a
target instruction. A native implementation may resolve the exact declarations
owned by its Mod and manipulate their typed calls.

## Forms and passes

The intended compilation states are invariants, not C++ class hierarchies:

```text
semantic Fn
    -> normalize / inline / rewrite
tensor form
    -> fuse and simplify
realized form
    -> storage and layout passes
target form
    -> emit
```

- **Tensor form** contains pure tensor construction, indexing, reduction, and
  scalar calls. It contains no allocation, mutation, address space, or target
  mapping.
- **Realized form** contains explicit iteration and logical storage effects. It
  contains no unresolved tensor construction or reduction.
- **Target form** replaces selected logical operations with declarations from a
  target Mod. It need not name an ISA or HDL in the core language.

A pass is an ordinary compiler-time fn such as `fn pass(mod, ...) -> mod` or
`fn pass(fn, ...) -> fn`. “Lowering” is not another framework; it describes a
pass that moves a program to a stricter invariant. Invocation stays explicit
through `@call`.

## Tensor definitions, not operator legalization

An NN fn is lowerable when it has a body expressed through other semantic fns
and ultimately through the small tensor basis. The compiler does not maintain
an ONNX-op-to-NN-op-to-target-op table. Importers resolve source operations to
ordinary semantic declarations; they do not decide implementations.

Adding a new bodyful semantic fn therefore requires no new realization rule.
An external or bodyless fn cannot be implemented from its name: it must be
given either a semantic body or an explicit external implementation before
the implementation closure is complete.

Tensor construction and reduction remain visible in the tensor form because
they state where output coordinates and reduction coordinates are bound. The
surface spelling may be compact, but their semantics cannot be inferred from a
plain `for` statement. A loop never returns or implicitly collects a tensor.

## Generic refinement

Tensor refinement is destination directed. Its central judgement is:

```text
realize(e : tensor<E, S>, destination : sink<E, S>)
  -> effect<sink<E, S>>
```

It recursively emits a command that writes the value of pure expression `e`
to `destination`:

- construction emits loops over its coordinate domain and stores its scalar
  body;
- reduction emits initialization, a reduction loop, and the final store;
- elementwise composition fuses into the current coordinate expression;
- reshape, transpose, broadcast, and slicing compose index maps and do not
  require storage by themselves;
- a shared value is fused, recomputed, or materialized according to an explicit
  policy;
- a selected target primitive becomes an ordinary call to its target-Mod
  declaration.

The first implementation may preserve public tensor-returning signatures by
allocating a fresh logical destination inside a wrapper. A later whole-Mod
destination-passing pass changes internal call signatures and rewrites all call
sites together; externally visible entry wrappers remain stable. Neither step
requires a per-operator bridge.

## Loop surface

Realized Joggle uses normal loops. A multi-dimensional loop is written:

```joggle
for i, j in M, N {
  out[i, j] = value;
}
```

It is exactly the lexicographic nesting:

```joggle
for i in M {
  for j in N {
    out[i, j] = value;
  }
}
```

Each binder has one domain. An integer extent denotes indices in the half-open
interval `[0, extent)`. A loop is a statement and has no result type. There is
no implicit `range`, Cartesian interpretation of a shape list, tail value, or
tensor collection.

Generic loops contain no `parallel`, GPU, FPGA, RISC-V, vector, pipeline, or
unroll marker. Dependence-aware and target-aware passes may transform the loop
nest or replace it with target calls. Those choices are implementation facts,
not tensor semantics.

## Implementation order and gates

1. **Core loop surface — complete.** Parse, print, verify, evaluate, and
   residualize paired multi-binder/multi-domain loops. Canonical round-trip and
   structural equivalence to explicitly nested loops are covered by tests.
2. **Pure tensor basis — complete.** `tensor@7` contains pure construction,
   `map`, indexed `reduce`, `[]`, and constants. MatMul and Relu have
   type-correct pure bodies and expand without compiler name cases.
3. **Logical memory Mod — complete.** `mem@1` separates immutable `view` from
   write-only `sink`; `alloc`/`store`/`seal` thread the core's affine effect
   value. It expresses generated multi-dimensional writes without layout,
   capacity, alias, or target concepts.
4. **Initial tensor refinement — complete.** `mem.realize` expands bodyful
   semantic fns and handles only the tensor basis: construction, `map`, indexed
   reads, scalar composition, and serial reduction. MatMul and Relu become
   verified explicit loop/store Fns without an NN-name case. Recursive
   coordinate sampling also fuses nested maps into one allocation and store
   nest.
5. **Whole-Mod destination passing.** Work on a copy of the input Mod. For
   each internal tensor-producing fn, add a same-name overload whose final
   inputs are `sink` and its effect state and whose result is the updated
   state. Rewrite exact call sites only after all overloads exist. Stable entry
   overloads remain allocation/seal wrappers; no `$dps` naming convention or
   new declaration kind is introduced. Gate: a multi-fn pipeline retains call
   boundaries, contains one entry allocation, and has no hidden intermediate
   tensor allocation. Failure must leave the original Mod unchanged.
6. **Optimization substrate.** Derive access and dependence summaries, then add
   fusion, index simplification, loop interchange, tiling, and storage reuse as
   separate ordinary passes. Gate: every pass preserves verification and emits
   a replayable before/after Mod.
7. **Target extensions.** Register data formats, layouts, primitives, mapping,
   cost, and emission in independent Mods. Gate: the core has no target-name or
   operator-name branches.

Performance claims, machine capacities, cycle models, and paper experiments
begin only after the first five gates form a complete semantic-to-realized
path.

## Rule for promoting mechanisms into core

The next gates start inside the owning module and use the existing public
`Mod`, `Fn`, CFG, dominance, effect, and edit APIs. A facility moves into C++
core only if it is domain-independent, cannot be expressed safely through
those APIs, and is needed by more than one module. In particular:

- tensor coordinate recursion, destination passing, access interpretation,
  fusion policy, and storage policy remain module code;
- physical layout, address space, memory capacity, instruction selection, and
  cost remain target-module code;
- atomic mutation, use-def/dominance, effect linearity, generic natural-loop
  discovery, and declaration-safe call rewiring may become core mechanisms if
  the whole-Mod prototype proves an API gap.

No target-facing concept is promoted merely to make one pass shorter.
