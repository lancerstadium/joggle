---
title: Graph generation
description: Construct typed constants, calls, branches, loops, clones, specializations, and function bodies with explicit insertion and ownership.
---

# Graph generation

Graph generation creates verified objects inside an existing `Mod`. The API is
not a text-template system: new values and operations carry real types,
ownership, dominance, metadata, and def-use relations immediately.

## Insertion point model

Most constructors receive `m` and `before`:

```jog
let value = ir.constant(m, before, 0, ty("i32"))
let result = ir.call(m, before, "operator +", [input, value], ty("i32"))
```

The new operations are inserted before a live operation in the same structural
region. This makes dominance explicit: operands must already be available at
that point.

## Generate a constant and call

```jog
fn add_one(m: Mod, before: Op, input: Val) -> Val {
  let one = ir.constant(m, before, 1, ir.type(input))
  return ir.call(m, before, "operator +", [input, one], ir.type(input))
}
```

Conceptual before:

```jog
return x
```

After creating `one`, creating the call, then replacing the returned operand:

```jog
let one: i32 = 1
let next = x + one
return next
```

Generation alone does not redirect old users. A rewrite composes construction
with replacement, as shown in the next chapter.

## Name-based versus function-based calls

```jog
let by_name = ir.call(m, before, "nn.relu", [input], ir.type(input))
let target = ir.find("nn.relu", [ir.type(input)])
let by_fn = ir.call(m, before, target, [input], ir.type(input))
```

The `Fn` form makes the selected declaration explicit. The name form is useful
when normal resolution should choose among overloads after the call is built.
Both are verified against the environment.

## Multi-result calls

Use the overload returning `Op` when a call has zero or multiple results:

```jog
let call = ir.call(
  m, before, "quant.dynamic", [input],
  [tensor<u8, S>, tensor<E, []>, tensor<u8, []>]
)
let outputs = ir.outs(call)
let data = outputs[0]
let scale = outputs[1]
let zero = outputs[2]
```

Each output is a separate `Val` with its own type and users.

## Clone an operation

```jog
fn duplicate(m: Mod, source: Op, before: Op) -> Op {
  return ir.clone(m, source, before)
}
```

For an operation whose operands must be remapped, use old/new value lists:

```jog
let copy = ir.clone(m, source, before, old_values, new_values)
```

The mapping is positional. Every captured block argument or earlier result that
changes identity must be covered. Nested regions require the structural clone
helper; copying only an outer call spelling is insufficient.

## Generate a loop

```jog
let loop = ir.loop(
  m,
  before,
  ["i"],       // iterator names
  [range],     // iteration sources
  [state]      // carried values
)
let body = ir.blks(loop)[0]
let args = ir.args(body)
let i = args[0]
let carried = args[1]
```

The constructor establishes the outer operation, child block, iterator block
arguments, carried block arguments, and loop result types. Populate the block
before its terminator and ensure yielded values match carried results.

## Generate a branch

```jog
let branch = ir.branch(m, before, condition, [state])
let arms = ir.blks(branch)
let then_state = ir.args(arms[0])[0]
let else_state = ir.args(arms[1])[0]
```

Both arms must yield values with matching arity/types. The branch results are
available in the parent block.

## Clone a function

```jog
let specialized = ir.clone(m, template, "project.matmul_2x4")
```

An overload accepts structural generic arguments. Function cloning also
materializes the required local helper closure and rejects inaccessible or
inconsistent dependencies.

## Bind constant parameters

```jog
let specialized = ir.bind(m, call, implementation, "kernel_4", [1])
```

Binding clones a body-bearing function and replaces selected parameters with
structural constant call arguments. Parameter indices must be unique and in
range, and bound values must be representable structurally.

## Generate metadata with the object

Create the structural object first, then attach open metadata through tracked
edits:

```jog
let value = ir.constant(m, before, 0, ty("i32"))
ir.set(m, value, "project.origin", "padding")
```

Metadata writes participate in revision tracking and rollback.

## Ownership and liveness rules

Every input handle must belong to `m` (or be a visible `Fn` allowed by the
environment), remain live, and be valid at the insertion point. Generation
rejects:

- operands from another graph;
- erased operations or values;
- insertion after a terminator;
- non-dominating operands;
- incompatible result types;
- inaccessible target functions;
- malformed loop/branch state.

## Generation versus parsing text

Avoid producing `.jog` strings and reparsing them for local graph edits.
Structured constructors preserve handle identity, give precise diagnostics,
avoid a text round trip, and participate in the active transaction.

Text generation is appropriate at an external source/artifact boundary, such
as a binary frontend returning decoded `.jog` or an emitter producing C.

## Practical generation patterns

| Goal | Primitive composition |
| --- | --- |
| insert a guard constant | `constant` + `branch` |
| introduce a semantic call | `call` + result types |
| duplicate a region | `clone` with value mapping |
| specialize implementation | `clone(fn)` or `bind` |
| expose implementation body | `ir.expand` / `opt.apply` |
| construct structured loop | `loop` + body clones + yield repair |

Continue with [Transactional rewrites](rewrites.md) to connect generated objects
to the original graph safely.
