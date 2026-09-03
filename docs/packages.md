# Packages and reproducibility

A release is one serialized `joggle::Module`: one canonical source file, one
semantic version, and one SHA-256 content identity. Packaging is repository
metadata, not another language object or IR owner. The CLI manages installed
releases and `Compiler` resolves ordinary search paths.

## Commands

```bash
joggle check module.joggle [--with dependency.joggle] [--behavior library]
joggle fmt module.joggle [--write | -o output.joggle]
joggle install module.joggle [--behavior library]
joggle uninstall name@1.2.3
joggle list
joggle lock root.joggle -o joggle.lock
```

All repository commands accept `--root directory`. Without it, Joggle uses
`JOGGLE_MODULE_ROOT`, then the default user repository.

`check` parses, resolves the import closure, and validates declarations,
contracts, body structure, and call shapes. It also instantiates every defined
Function whose signature is concrete from defaults alone. A body requiring
caller-supplied Known generics is checked independently as far as its abstract
environment permits, then fully type-checked at each concrete specialization;
package validation does not invent representative generic values. Repeated
`--with` options add uninstalled local dependencies without modifying a
repository. `--behavior` additionally validates an exact behavior library and
runs registered verifiers.

`fmt` produces canonical text. In-place and explicit-output writes stage a
complete replacement beside the destination, preserve an existing file on
failure, and preserve permission bits for in-place formatting.

## Installed layout

Canonical Module source is content-addressed beneath:

```text
<root>/<name>/<version>/<module-sha256>/module.joggle
```

An optional behavior library is stored beneath the exact Module identity by
host target and binary digest:

```text
<module-identity>/behavior/<target>/<binary-sha256>/behavior.<suffix>
```

The suffix is platform-specific. Installing the same identity is idempotent.
A different digest for the same name and exact version is rejected instead of
silently replacing content. A second behavior digest for the same Module and
target is likewise rejected.

Install validates the complete closure before publishing. Module text and
behavior are assembled in a hidden same-filesystem staging directory and
become visible atomically. Uninstall first removes the exact version from the
visible namespace, then reclaims storage. Resolution therefore never observes
a partial package.

## Resolution

```cpp
joggle::Compiler compiler;
compiler.search(module_root);
compiler.load("root.joggle");
if (!compiler.link()) {
  compiler.diagnostics().print(std::cerr);
}
```

For each import, linking chooses the highest installed version satisfying its
range. It validates stored paths against parsed names, versions, and digests,
then rejects conflicting identities, incompatible ranges, missing imports, and
cycles.

Aliases affect source spelling only. Stable symbols and locks record the real
Module identity.

## Locks

A lock records the exact root, dependency Modules, and target-specific behavior
binaries:

```text
joggle-lock 1;
root model@1.0.0#<digest>;
module tensor@1.0.0#<digest>;
behavior tensor@1.0.0#<module-digest> macos-arm64#<binary-digest>;
```

Generate and replay it explicitly:

```bash
joggle lock model.joggle -o joggle.lock
```

```cpp
joggle::Compiler compiler;
compiler.search(module_root);
compiler.lock("joggle.lock");
compiler.load("model.joggle");
compiler.link();
```

Replay fails when the root differs, an entry is absent or unused, a locked
version violates an import, a digest differs, or the required behavior for the
host target is unavailable. With a lock active, behavior discovery must match
its entry. A lock therefore selects executable content, not only source version
ranges.

Locks containing behavior are target-specific. Generate a lock for each target
instead of substituting another platform's binary.
