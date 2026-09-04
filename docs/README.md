# Joggle documentation

Joggle has one current documentation set. Each document owns a distinct part
of the project; none is a migration log.

- [Getting started](getting-started.md) builds, checks, and runs one Module.
- [Architecture](architecture.md) defines the compiler's scope and public
  concepts.
- [Intermediate representation](ir.md) specifies Ops, SSA, properties,
  transformations, and Module-owned data.
- [Language](language.md) is the source-language reference.
- [Compiler functions](compiler-functions.md) defines composition and the
  reusable facilities required by transforms, analyses, and emitters.
- [Research position](research-position.md) separates implemented evidence from
  the hypotheses, comparisons, and experiments required for publication.
- [Module design](modules.md) defines the clean extension and target model.
- [C++ API](cpp-api.md) documents the in-process library surface.
- [Module repository](module-repository.md) specifies installation,
  resolution, and lock files.

The repository currently contains only the compiler core. AI vocabulary and
target Modules are being rebuilt against the rules in [Module design](modules.md)
and are not part of the current release surface.
