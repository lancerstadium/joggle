# RFC 0006: Module bundles

Status: repository persistence gate complete

## Problem

A `Module` may own immutable bytes through `Module::store`. Canonical source
intentionally does not inline those bytes, so source text alone cannot preserve
an imported model. Losing payloads while printing or installing a Module would
leave valid-looking calls whose content digests cannot be resolved.

This must be solved without introducing an Artifact, Package, Program, model
container, or binary manifest language. The `Module` remains the only owner
and its existing digest remains the release identity.

## Repository representation

An installed identity has this form:

```text
<root>/<name>/<version>/<module-digest>/
  module.joggle
  data/
    <payload-sha256>
  native/
    <target>/<binary-sha256>/native.<suffix>
```

`data` is absent for a source-only Module. Every filename is the lowercase
64-digit suffix of the corresponding `sha256:<digest>` data name. There is no
manifest: loading verifies each filename against its bytes, restores it through
`Module::store`, and then requires the reconstructed `Module::digest()` to
equal the identity directory. A missing, added, renamed, symlinked, or modified
payload therefore invalidates the release.

Source, data, and an optional native library are written into the existing
same-filesystem staging directory and published by one directory rename.
Idempotent installation re-reads and verifies all three components.

## External bundle boundary

The repository layout is an installed representation, not yet the user-facing
transport syntax. A later gate will let CLI commands read and write one bundle
directory containing `module.joggle` and `data`. Until that gate is complete,
printing a data-bearing Module as source is not a lossless export.

The external bundle must reuse the same source and data rules. It must not add
a second identity, archive format, manifest object, generated header, or
special ONNX ownership path.

## Implementation gates

- [x] Atomically persist and hydrate Module-owned data in the repository.
- [x] Verify payload filenames, payload bytes, reconstructed Module identity,
  idempotent installation, and corruption failure.
- [ ] Add lossless CLI directory-bundle input and output.
- [ ] Install, resolve, and reload the imported SqueezeNet Module through the
  public CLI workflow.
