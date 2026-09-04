# Repository

A release is one serialized `joggle::Mod`: one canonical source file, one
semantic version, and one SHA-256 content identity. Release metadata belongs
to the repository; it is not another language object or IR owner. The CLI
manages installed releases and `Compiler` resolves ordinary search paths.

This repository identity is `Mod::digest()`, which covers Fn bodies.
It is intentionally stricter than `Mod::declaration_digest()`: two artifacts
with identical declarations but different implementations cannot occupy the
same installed release, even though their versioned member names are
compatible.

## Commands

```bash
joggle check mod.joggle|bundle [--with dependency.joggle] [--native library]
joggle fmt mod.joggle [--write | -o output.joggle]
joggle install mod.joggle|bundle [--native library]
joggle uninstall name@1.2.3
joggle list
joggle lock root.joggle|bundle -o joggle.lock
```

All repository commands accept `--root directory`. Without it, Joggle uses
`JOGGLE_MOD_ROOT`, then the default user repository.

`check` parses, resolves the import closure, and validates declarations,
contracts, body structure, and call shapes. It also instantiates every defined
Fn whose signature is concrete from defaults alone. A body requiring
caller-supplied Known generics is checked independently as far as its abstract
environment permits, then fully type-checked at each concrete specialization;
mod validation does not invent representative generic values. Repeated
`--with` options add uninstalled local dependencies without modifying a
repository. `--native` additionally validates an exact native library and
runs registered verifiers.

`fmt` produces canonical text. In-place and explicit-output writes stage a
complete replacement beside the destination, preserve an existing file on
failure, and preserve permission bits for in-place formatting.

## External bundles

Normal source-only Mods remain one `.joggle` file. When a compiler fn
returns a Mod with owned data, `run` writes a directory bundle:

```bash
joggle run pipeline.joggle import model.onnx \
  --load-native onnx=/path/to/native \
  -o model-bundle

joggle check model-bundle
joggle install model-bundle
```

The directory contains `mod.joggle` and `data/<payload-sha256>` and uses
the same verification rules as an installed identity. A data-bearing result
without `-o` is rejected, because canonical source alone would be lossy.
Bundle publication is atomic and refuses to overwrite an existing path.

`check`, mod-valued `run` inputs, `install`, and `lock` accept a bundle
directory directly. `fmt` remains a source-text operation. A native library
is supplied independently with the existing `--native` or `--load-native`
option; the bundle adds no manifest or platform-specific identity.

## Installed layout

Canonical Mod source is content-addressed beneath:

```text
<root>/<name>/<version>/<mod-sha256>/mod.joggle
```

An optional native library is stored beneath the exact Mod identity by
host target and binary digest:

```text
<mod-identity>/native/<target>/<binary-sha256>/native.<suffix>
```

Mod-owned immutable bytes are stored without a manifest:

```text
<mod-identity>/data/<payload-sha256>
```

Each lowercase filename is verified against its exact bytes and restored
through `Mod::store`. The reconstructed Mod digest must still equal the
identity directory, so missing, additional, renamed, symlinked, or modified
payloads invalidate the installation. A source-only Mod has no `data`
directory.

The suffix is platform-specific. Installing the same identity is idempotent.
A different digest for the same name and exact version is rejected instead of
silently replacing content. A second native digest for the same Mod and
target is likewise rejected.

Install validates the complete closure before publishing. Mod text,
Mod-owned data, and native are assembled in a hidden same-filesystem staging
directory and become visible atomically. Uninstall first removes the exact
version from the visible namespace, then reclaims storage. Resolution therefore
never observes a partial Mod release.

## Resolution

```cpp
joggle::Compiler compiler;
compiler.search(mod_root);
compiler.load("root.joggle");
if (!compiler.link()) {
  compiler.diag().print(std::cerr);
}
```

For each import, linking chooses the highest installed version satisfying its
range. It validates stored paths against parsed names, versions, and digests,
then rejects conflicting identities, incompatible ranges, missing imports, and
cycles.

Aliases affect source spelling only. Stable symbols and locks record the real
Mod identity.

## Locks

A lock records the exact root, dependency Mods, and target-specific native
binaries:

```text
joggle-lock 1;
root model@1.0.0#<digest>;
mod tensor@1.0.0#<digest>;
native tensor@1.0.0#<mod-digest> macos-arm64#<binary-digest>;
```

Generate and replay it explicitly:

```bash
joggle lock model.joggle -o joggle.lock
```

```cpp
joggle::Compiler compiler;
compiler.search(mod_root);
compiler.lock("joggle.lock");
compiler.load("model.joggle");
compiler.link();
```

Replay fails when the root differs, an entry is absent or unused, a locked
version violates an import, a digest differs, or the required native for the
host target is unavailable. With a lock active, native discovery must match
its entry. A lock therefore selects executable content, not only source version
ranges.

Locks containing native are target-specific. Generate a lock for each target
instead of substituting another platform's binary.
