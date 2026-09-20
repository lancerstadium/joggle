---
title: Joggle
description: Typed, graph-scoped compiler extensions from model semantics to emitted artifacts.
---

<section class="hero">
  <div class="eyebrow">Compiler infrastructure</div>
  <h1>One language across the compiler.</h1>
  <p class="lead">Joggle uses ordinary typed functions to define model semantics, inspect and transform IR, convert representations, and emit artifacts.</p>
  <div class="actions">
    <a class="button" href="guide/">Build and run</a>
    <a class="button secondary" href="internals/design/">Understand the design</a>
  </div>
</section>

<div class="card-grid">
  <section class="card">
    <h3>Unified</h3>
    <p>Operators, analyses, rewrites, conversions, and emitters share one typed language and object model.</p>
  </section>
  <section class="card">
    <h3>Graph-scoped</h3>
    <p><code>mod</code> packages group an extension by responsibility instead of scattering it across compiler layers.</p>
  </section>
  <section class="card">
    <h3>Reactive</h3>
    <p>Observed dependencies determine which query and stage results remain valid after a local change.</p>
  </section>
</div>

## Follow one path

<ol class="path">
  <li><a href="guide/">Build Joggle</a><br>Check and transform a small program.</li>
  <li><a href="guide/write-mod/">Write a mod</a><br>Add an out-of-tree transformation.</li>
  <li><a href="guide/import-onnx/">Import ONNX</a><br>Make conversion stages explicit.</li>
  <li><a href="guide/emit-c/">Emit C</a><br>Prepare, plan, compile, and run.</li>
</ol>

## Look up a detail

| Need | Read |
| --- | --- |
| syntax, types, generics, and execution semantics | [Language reference](reference/language.md) |
| bundled package responsibilities and public functions | [Bundled mod catalogue](reference/module-catalogue.md) |
| IR, transactions, dependency tracking, and invariants | [Architecture](internals/design.md) |
| package layout, discovery, installation, and composition | [Mod system](internals/modules.md) |

> This site documents implemented behavior. It intentionally excludes paper plans, tentative experiments, and unverified performance claims.
