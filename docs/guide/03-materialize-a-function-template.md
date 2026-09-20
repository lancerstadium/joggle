# Materialize a function template

A mod may copy a normal function into the program when a transform needs a
named helper or a local template boundary:

```jog
mod localize
use ir
use nn

fn apply(m: Mod) -> bool {
  for fn in ir.fns("nn") {
    if ir.name(fn) == "relu" {
      return ir.live(ir.clone(m, fn, "edge_relu"))
    }
  }
  return false
}
```

The copied `Fn` retains its generic signature, nested loops, metadata, and
ordinary calls. Joggle adds the source mod to the program's dependency graph
in the same transaction. A recursive template calls the new local function;
only references that would become ambiguous are qualified. A duplicate overload
or invalid destination name leaves both text and revision unchanged. There is
no generated declaration, stateful builder, or separate kernel representation.

Pass concrete generic terms to create a monomorphic helper through the same
operation:

```jog
let relu4 = ir.clone(
  m, template, "relu4", [ty("f32"), ty("[4]")]
)
```

The result is `fn relu4(x: tensor<f32, [4]>) -> tensor<f32, [4]>` with a fully
substituted body and no generic parameters. Shape or width generics used as
ordinary values become constants or list literals in the entry block. Invalid
bindings and collisions with an existing concrete overload leave the program
unchanged.

For a real call, the ordinary matcher already knows those terms:

```jog
let template = ir.resolve(m, call)
let generics = ir.match(call, template)
let local = ir.clone(m, template, "edge_relu", generics)
assert(ir.live(local), "could not materialize function")
assert(ir.rename(m, call, ir.symbol(local)), "could not retarget call")
```

This retains the call-specific dtype and shape without a target descriptor or
a second generic-inference interface. If either edit fails inside a `run`
entry, its assertion restores the module before the clone, so no unused local
function remains.

Later passes may rename that exact overload or erase it after its callers are
gone:

```jog
assert(ir.rename(m, local, "packed_relu"), "could not rename helper")
```

Rename follows resolved calls, not every matching string. After another
transform has removed or retargeted every caller, the helper can be removed:

```jog
assert(ir.erase(m, local), "helper still has callers")
```

Erase invalidates the complete function body and rejects helpers that are still
used elsewhere.

The `workflow` test executes these paths through `script.clone_relu`,
`script.clone_relu4`, `script.clone_matched`, and the rollback case
`script.clone_then_fail`.
