# Joggle documentation

Joggle has one current documentation set. Each document owns a distinct part
of the project; none is a migration log.

- [Getting started](getting-started.md) builds, checks, and runs one extension.
- [Architecture](architecture.md) defines the compiler's scope and public
  concepts.
- [Language](language.md) is the source-language reference.
- [Extensions](extensions.md) covers Modules, C++ behavior, and compiler
  functions.
- [C++ API](cpp-api.md) documents the in-process library surface.
- [Packages](packages.md) specifies installation, resolution, and lock files.

The checked-in declarations in [`modules`](../modules) and executable examples
in [`examples`](../examples) are part of the reference. If prose and behavior
disagree, tests and public headers describe the implemented release; please
report the documentation mismatch.
