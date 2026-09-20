---
title: Mod system
description: Package layout, discovery, lifecycle, composition, and extension boundaries.
---

# Mod system

Modules are Joggle's only extension unit. They define types, semantics,
analyses, transformations, codecs, and artifact generation with the same
`.jog` functions used by application code.

This document explains module boundaries and the bundled module set. The
public declarations printed by `joggle mod info` are the authoritative API
of an installed package.

## Package layout

A source-only module needs one file:

```text
my_module/
  module.jog
```

Larger modules may add sorted source fragments and one native library:

```text
my_module/
  module.jog
  lib/
    shapes.jog
    transforms.jog
  native/
    libmy_module.dylib
```

`module.jog` begins with a `mod` declaration and optional dependencies:

```jog
mod my_module
use ir
use tensor

local fn helper(op: Op) -> bool {
  return ir.callee(op) == "nn.relu"
}

fn apply(m: Mod) -> bool {
  var changed = false
  for op in ir.ops(m) {
    if helper(op) {
      changed = ir.set(m, op, "implementation", "lut") || changed
    }
  }
  return changed
}
```

`module.jog` is the package entry source, not a second interface language.
Together with the sorted `lib/*.jog` fragments it forms one module. Top-level
`fn` declarations in any source are public; `local fn` declarations are
implementation details. No generated header, export list, registration
routine, or version suffix in a symbol name is required.

Module identities are dot-qualified names whose segments contain only letters,
digits, and underscores and begin with a letter or underscore. The parser,
embedding loader, and command-line loading commands use the same validation;
empty names, empty segments, path separators, and traversal components are
rejected before the search roots are inspected.

The C++ `parse` overload accepting `std::span<const Source>` applies the same
rule to embedded callers. Each `Source` owns its text and display path; the
parser combines their declarations into one `Mod` while retaining the original
file and line on every diagnostic. The command-line loader, inspection, and
module lifecycle commands all use this interface, so splitting a large module
does not degrade error locations.

## Discovery and lifecycle

Module roots are supplied with repeatable `-M` options. Resolution is by
module name, and `use` dependencies close transitively. Commands operate on
the same public surface used by embedding code:

```sh
joggle mod list -M modules
joggle mod info tensor -M modules
joggle mod check tensor -M modules
joggle mod install path/to/source installed-modules -M modules
joggle mod upgrade path/to/source installed-modules -M modules
joggle mod uninstall my_module installed-modules
```

Install and upgrade never treat the candidate's parent directory as an implicit
module root. Every dependency must already be installed in the destination or
be reachable through an explicit `-M` root. This makes validation reproducible
after the source checkout is moved or deleted; physical source adjacency is not
part of a module's dependency contract.

Installation validates the candidate and its dependency closure before making
it visible. Upgrade additionally reloads the transitive reverse-dependency
closure against the staged candidate and checks that every previously resolved
call still selects the same qualified declaration. Removing a transitive
namespace edge or introducing an overload that retargets an installed caller is
therefore rejected before any installed bytes change. Source-only modules
remain readable and portable. A native boundary is optional and should be used
only for facilities that cannot be expressed economically in `.jog`, such as
binary decoding or executing a host artifact.

The native value ABI checks declarations in both directions. `nil`, `bool`,
floating scalars, `int`/`index`/digit-width integer scalars, `str`, and `bytes`
map to their corresponding tagged values; `_` and `Attr` accept any of those
encodable tags. A user-defined type is not inferred from its spelling and
cannot accidentally cross as an integer merely because its name begins with
`i` or `u`; a module must provide an explicit representation boundary for such
a type. Lists, dictionaries, and IR handles stay inside compile-time execution
until the ABI grows an explicit representation for them.

## Composition

There is no built-in pipeline object. Users compose module functions explicitly:

```sh
joggle read onnx.read model.onnx -M modules > model.jog

joggle run onnx.nn.convert opt.basic model.jog \
  -M modules > semantic.jog

joggle run c.prepare bounds.fold opt.fold opt.basic \
  tile.scalarize opt.basic mem.plan c.noalias semantic.jog \
  -M modules > prepared.jog

joggle emit c.source prepared.jog -M modules > model.c
```

This example uses `c.noalias` only because its application ABI guarantees
disjoint input, output, and weight buffers. It is an explicit caller contract,
not an inferred property of arbitrary exported functions.

Several `run` functions form one transaction. If a later function fails, all
earlier mutations in that invocation are rolled back. `query` and `emit`
use the same resolver but do not establish separate analysis or backend
registries.

Functions can also accept a function handle. For example, a loop-fusion policy
is an ordinary `fn` selected with `ir.find` and invoked by `ir.invoke`.
This allows an experiment to replace policy without modifying the mechanism.

## Responsibility map

The bundled modules are grouped here for explanation only. The runtime does not
hard-code these categories.

| Module | Public responsibility |
| --- | --- |
| `base` | Compile-time collections, structural type construction, text utilities, assertions, and scalar operator declarations |
| `ir` | Reflection, resolution, safe editing, cloning, expansion, replacement, and function invocation |
| `tensor` | Tensor type constructor, shape algebra, indexing, broadcasting, arithmetic and comparison overloads, reductions, reshaping, and inspectable tensor bodies |
| `nn` | Frontend-neutral neural-network semantics expressed in terms of tensor and scalar functions |
| `quant` | Quantization, dequantization, and quantized tensor computation |
| `math` | Portable scalar mathematical functions |
| `opt` | General simplification, dead-code elimination, common-subexpression elimination, policy-controlled loop motion, exposure, implementation selection, and call fusion |
| `bounds` | Conservative integer ranges, representation checks, and exact predicate folding |
| `stat` | Structural program measurements through user-supplied measurement functions |
| `mem` | Static tensor-buffer reuse planning and inspectable slot annotations |
| `tile` | Loop/access analysis plus explicit splitting, unrolling, and pointwise fusion |
| `onnx` | ONNX binary decoding and source-format calls |
| `onnx.nn` | ONNX type refinement and explicit conversion to shared semantics |
| `tflite` | TFLite binary decoding and source-format calls |
| `tflite.nn` | TFLite type refinement and explicit conversion to shared semantics |
| `c` | C capability checks, preparation, ABI/API description, header/data generation, and source emission |
| `vm` | Deterministic VM capability checks, preparation, image emission, and execution |
| `sat` | Example parametric saturating type, overloads, selection, and materialization |
| `sat.c` | C-specific representation of `sat` |
| `sat.vm` | VM-specific representation of `sat` |


## Extension checklist

A module is ready to share when:

- its name and public functions describe concepts rather than a development
  phase or version;
- the intended public surface is visible in `joggle mod info`, with helpers
  marked `local`;
- dependencies are explicit and minimal;
- unknown metadata is preserved;
- failure is transactional and diagnostics identify the rejected operation;
- output is deterministic;
- at least one out-of-tree use works without modifying core files;
- examples show both invocation and resulting IR or artifact;
- claims about numerical correctness or speed are backed by stored inputs,
  reference outputs, commands, and measurements.

See [Write a mod](../guide/write-mod.md) for a guided example and
[design.md](design.md) for the invariants behind these rules.
