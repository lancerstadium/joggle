# Packages

A Joggle `Mod` is one versioned, installable package. It is simultaneously a
namespace, dependency unit, declaration source, body container, and owner of
immutable data. It is not an IR level or a pass object.

## Admission rule

Create a new mod only when its vocabulary must be independently named,
versioned, installed, and reused. A single pass, fusion pattern, operator
combination, schedule, benchmark, or paper experiment does not justify a mod.

The shipped boundary is:

| Mod | Responsibility |
| --- | --- |
| `arith` | scalar operations |
| `tensor` | tensor values, access algebra, fusion, loop expansion |
| `nn` | frontend-independent NN semantics |
| `transform` | domain-independent Fn rewriting and resolution |
| `quant` | quantize/dequantize semantics |
| `onnx` | optional external byte reader |

The declared dependency edges are small and acyclic:

| Mod | Declared imports |
| --- | --- |
| `arith` | none |
| `tensor` | `arith` |
| `nn` | `arith`, `tensor` |
| `quant` | `tensor` |
| `transform` | none |
| `onnx` | none |

`tensor` uses Arith only to construct scalar loop control. `nn` defines its
portable bodies with Arith and Tensor. Quantization uses Tensor types. The
optional ONNX reader is deliberately open: it resolves records to semantic
fns that the caller linked, rather than fixing one operator package into its
own ABI.

There is no generic `mem`, `graph`, `kernel`, `device`, or `target` mod. A
future target package is admitted only with a real backend and a demonstrated
portable boundary.

## Members

Only three member forms exist:

- `import` names a versioned dependency;
- `type` defines an immutable parameterized type;
- `fn` declares or defines computation.

Import, analysis, conversion, optimization, simulation, and emission are
ordinary fns. A compiler-time fn is called explicitly with `@`.

Types and fns are private unless prefixed with `pub`. Package authors use
private declarations for shape arithmetic and implementation helpers, and
publish only names that downstream mods or format readers may resolve. `pub`
is visibility on the same declaration, not an interface block, export table,
or second schema.

```joggle
mod project@1.0.0 {
  import tensor@8 as t;

  pub fn prepare(input: fn) -> fn {
    fused = @t.fuse(input);
    return @t.loops(fused);
  }
}
```

The order is ordinary user code. Joggle does not discover pass names or impose
a universal pipeline.

## Semantic functions

Portable operators should have source bodies. A compiler pass may inspect and
compose those bodies without knowing the function name. Bodyless residual fns
are opaque implementation boundaries and must remain visible until a target
package replaces or implements them.

Do not create parallel declarations for a frontend operator and its semantic
equivalent. An importer resolves directly to the linked semantic fn. Any
source-format mismatch is handled while binding the source record, not by
inventing an intermediate operation family.

## Native implementation

A bodyless compiler-time fn may be implemented in C++:

```cpp
void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diag&) {
  compiler.bind(mod, "read", [](const joggle::Bytes& input) {
    return decode(input);
  });
}
```

The `.joggle` source remains the ABI authority. `joggle_mod(...)` embeds its
declaration identity in the native library, so no generated header or duplicate
schema is required.

## Target packages

A target package may define types for packed formats, layouts, storage handles,
or instructions and ordinary fns that transform a loop Fn into those calls.
The target is not required to inherit a base class. The minimum useful target
package must provide:

- a conversion fn with a precise accepted-form contract;
- verification or diagnostics for unsupported operations;
- one executable emitter or simulator;
- tests on a real computation rather than declaration-only fixtures.

Physical capacities and cycle costs belong to a concrete target description,
not the compiler core or tensor package.

## Review checklist

Before adding a mod, verify:

1. Existing types and ordinary fns cannot express the boundary cleanly.
2. The package has an independent version and at least one real consumer.
3. Its public surface contains domain concepts, not workflow scaffolding.
4. It introduces no per-operator C++ registration requirement.
5. Unsupported cases fail explicitly.
6. Its documentation distinguishes implemented behavior from planned work.
