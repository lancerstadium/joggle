# Joggle documentation

This tree is also built and deployed as the project's GitHub Pages site by
`.github/workflows/pages.yml`; [index.md](index.md) is its landing page.

Three directories, by who is reading:

| If you want to | Start at |
| --- | --- |
| write an extension, a policy, or a target | [`guide/`](guide/README.md) |
| look up a fact about `.jog` or the bundled mods | [`reference/`](reference/language.md) |
| change the compiler itself | [`internals/`](internals/design.md) |

## Guide

Task-oriented walkthroughs. Each one ends in something you can run.

| Chapter | Builds | Proven by |
| --- | --- | --- |
| [Reuse network semantics](guide/01-reuse-network-semantics.md) | a model that keeps calling installed `nn` and `tensor` bodies | `c-execution`, `tile-execution` |
| [Fold proved conditions](guide/02-fold-proved-conditions.md) | `bounds.fold` plus `opt.fold` interval folding | `bounds-report`, `locality-extension` |
| [Materialize a function template](guide/03-materialize-a-function-template.md) | a mod that copies and specializes a function in the program | `workflow` (`script.clone_*`) |
| [Define a fusion policy](guide/04-define-a-fusion-policy.md) | a mod reusing the generic chain matcher | `onnx-zoo-mobilenet` (`script.fuse_onnx`) |
| [Import an official ONNX model](guide/05-import-an-onnx-model.md) | the pinned model matrix and the ONNX frontend | `model` (`onnx-zoo-*`), `onnx-codec` |
| [Add a data format and primitive](guide/06-add-a-data-format.md) | the `sat` module and its type-directed selector | `sat`, `sat-c-execution` |
| [Emit an exposed kernel as C](guide/07-emit-a-kernel-as-c.md) | the `c` module: expose, plan, emit, compile, run | `c-execution`, `qlinear-c-execution`, `mem-execution` |

## Reference

- [Language](reference/language.md): the complete `.jog` grammar and semantics.
- [Bundled mod catalogue](reference/module-catalogue.md): every mod the
  project ships, grouped by the boundary it covers.

## Internals

- [Design](internals/design.md): system model, core IR, invariants, non-goals.
- [Mods](internals/modules.md): package layout, discovery and lifecycle,
  composition, responsibility map, and the checklist for adding one.
- [Roadmap](internals/roadmap.md): engineering priorities and release gates.

## Where the documentation and the tests meet

The suite is the executable form of this guide: every walkthrough maps to at
least one named test above. The function-materialization and fusion chapters
reuse the checked `script.clone_*` and `script.fuse_onnx` subjects rather than
maintaining a second tutorial-only implementation.

To run what proves a row, use the kind labels documented in the
[project README](https://github.com/lancerstadium/joggle#running-a-subset-of-the-tests):

```sh
ctest --test-dir build -L c           # the generated-C walkthroughs
ctest --test-dir build -L extension   # the out-of-tree module walkthroughs
ctest --test-dir build -L model       # the pinned ONNX gates; minutes
```
