---
title: Joggle
description: Typed, graph-scoped compiler extensions from model semantics to emitted artifacts.
home: true
---

<section class="hero">
  <div class="eyebrow">Compiler infrastructure</div>
  <h1>Build compiler extensions in one place.</h1>
  <p class="lead">Joggle is a typed compiler infrastructure for defining model semantics, transforming IR, converting representations, and emitting inspectable artifacts.</p>
  <div class="actions">
    <a class="button" href="getting-started/">Get started</a>
    <a class="button secondary" href="design/">Read the design</a>
  </div>
</section>

<div class="card-grid">
  <section class="card">
    <h3>Readable IR</h3>
    <p>Models and compiler functions use the same typed <code>.jog</code> source format.</p>
  </section>
  <section class="card">
    <h3>Explicit stages</h3>
    <p>Import, transformation, planning, and emission stay visible and composable.</p>
  </section>
  <section class="card">
    <h3>Portable packages</h3>
    <p><code>mod</code> packages declare their dependencies and load from explicit search roots.</p>
  </section>
</div>

## Follow one path

<ol class="path">
  <li><a href="getting-started/">Build Joggle</a><br>Check and transform a small program.</li>
  <li><a href="tutorials/write-mod/">Write a mod</a><br>Add an out-of-tree transformation.</li>
  <li><a href="tutorials/import-onnx/">Import ONNX</a><br>Decode and convert explicitly.</li>
  <li><a href="tutorials/emit-c/">Emit C</a><br>Prepare, plan, compile, and run.</li>
</ol>

## Look up a detail

| Need | Read |
| --- | --- |
| syntax, types, generics, and execution semantics | [Language reference](reference/language.md) |
| bundled package responsibilities and public functions | [Bundled mod catalogue](reference/module-catalogue.md) |
| implementation structure and object model | [System design](design/index.md) |
| package layout, discovery, and installation | [Module organization](design/modules.md) |
| transactions, dependency tracking, and fallback behavior | [Execution and updates](design/execution.md) |
