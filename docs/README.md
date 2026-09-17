# Joggle documentation

Three directories, by who is reading:

| If you want to | Start at |
| --- | --- |
| write an extension, a policy, or a target | [`guide/`](guide/README.md) |
| look up a fact about `.jog` or the bundled modules | [`reference/`](reference/language.md) |
| change the compiler itself | [`internals/`](internals/design.md) |

## Guide

Task-oriented walkthroughs. Each one ends in something you can run.

| Chapter | Builds | Proven by |
| --- | --- | --- |
| [Reuse network semantics](guide/01-reuse-network-semantics.md) | a model that keeps calling installed `nn` and `tensor` bodies | `c-execution`, `tile-execution` |
| [Fold proved conditions](guide/02-fold-proved-conditions.md) | `bounds.fold` plus `opt.fold` interval folding | `bounds-report`, `locality-extension` |
| [Materialize a function template](guide/03-materialize-a-function-template.md) | a module that copies a function into the program | no test; the `localize` module is illustrative |
| [Define a fusion policy](guide/04-define-a-fusion-policy.md) | a module reusing the generic chain matcher | no script test; the fusion study lives under `paper/` |
| [Import an official ONNX model](guide/05-import-an-onnx-model.md) | the pinned model matrix and the ONNX frontend | `model` (`onnx-zoo-*`), `onnx-codec` |
| [Add a data format and primitive](guide/06-add-a-data-format.md) | the `sat` module and its type-directed selector | `sat`, `sat-c-execution` |
| [Emit an exposed kernel as C](guide/07-emit-a-kernel-as-c.md) | the `c` module: expose, plan, emit, compile, run | `c-execution`, `qlinear-c-execution`, `mem-execution` |

## Reference

- [Language](reference/language.md): the complete `.jog` grammar and semantics.
- [Bundled module catalogue](reference/module-catalogue.md): every module the
  project ships, grouped by the boundary it covers.

## Internals

- [Design](internals/design.md): system model, core IR, invariants, non-goals.
- [Modules](internals/modules.md): package layout, discovery and lifecycle,
  composition, responsibility map, and the checklist for adding one.
- [Roadmap](internals/roadmap.md): engineering priorities, paper readiness, and
  the compatibility policy.

## Where the documentation and the tests meet

The suite is meant to be the executable form of this guide, so a walkthrough
should have a test that proves it and a test should trace back to a walkthrough.
The third and fourth chapters are where that breaks down: `localize` and the
example fusion policy exist only inside the guide, so nothing detects it if the
mechanisms they describe change. Either they need a test or the guide should say
they are sketches. The rest of the table above is real coverage, not intent.

To run what proves a row, use the kind labels documented in the
[project README](../README.md#running-a-subset-of-the-tests):

```sh
ctest --test-dir build -L c           # the generated-C walkthroughs
ctest --test-dir build -L extension   # the out-of-tree module walkthroughs
ctest --test-dir build -L model       # the pinned ONNX gates; minutes
```
