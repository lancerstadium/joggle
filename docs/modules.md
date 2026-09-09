# Modules

A module is the only extension and distribution unit. A module directory is:

```text
example/
  module.jog
  lib/*.jog
  native/joggle_example.*   # optional
  test/*
```

The directory is a distribution form, not a second IR object. Loading its
sources produces ordinary declarations visible in an `Env`; parsing a model
produces an ordinary `Mod`.

Pure `.jog` modules need no compiler toolchain. Native modules have one explicit
versioned C entry point and attach callbacks only to host functions already
declared in `.jog`. C++ STL containers, exceptions, RTTI, and virtual tables do
not cross that boundary.

The first standard modules will be `base`, `tensor`, `nn`, `opt`, and `onnx`.
Only `base` is needed while the structural language is bootstrapped. MLIR, JIT,
simulation, hardware description, and target experiments remain optional.

Version 0.1 searches explicit local paths. Installation means placing or
linking a directory on one of those paths; removal means taking it off the path.
Native libraries remain loaded for the lifetime of their `Env`. Network package
resolution, lockfiles, and in-process hot unloading are out of scope.
