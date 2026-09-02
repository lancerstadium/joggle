# Module packages and locks

A Module is the package unit. The public C++ API has no Package or PackageStore
class; installation is a CLI concern, and Compiler resolution uses ordinary
search paths.

## Commands

```bash
joggle check bitmath.joggle
joggle check bitmath.joggle --behavior build/bitmath_behavior.dylib
joggle fmt bitmath.joggle --write

joggle install bitmath.joggle
joggle install bitmath.joggle --behavior build/behavior.dylib
joggle list
joggle uninstall bitmath@1.0.0

joggle lock miniai.joggle -o joggle.lock
```

`check` parses the source and links its complete import closure. Use `--root`
to check an extension against an isolated installed Module set; unresolved
imports, interface mismatches, operation type contracts, and pass-reference
errors make the command fail. It also instantiates every named graph in the
resolved closure through the ordinary Compiler path, catching unknown
declarations, SSA errors, and type-inference failures. `--behavior`
additionally checks an explicitly chosen
behavior library against the canonical Module identity and applies its domain
verifiers while constructing those graphs. `fmt` only needs the source file.
Parse, import-resolution, conformance, operation-contract, pass, graph, and
behavior diagnostics retain their originating `.joggle` source range.

`fmt --write`, `fmt -o`, and `lock -o` stage their complete output beside the
destination and replace it atomically. A failed write leaves the previous file
unchanged; in-place formatting also preserves its permission bits and follows
an existing symbolic link rather than replacing the link itself.

`joggle check root.joggle --with dependency.joggle` validates a local source
closure without installing it. `--with` may be repeated and does not alter the
package root.

`--root <directory>` selects an isolated module root. Otherwise Joggle uses
`JOGGLE_MODULE_ROOT`, then `$HOME/.joggle/modules`.

Installation stores canonical text at:

```text
<root>/<name>/<version>/<sha256>/module.joggle
```

An optional behavior library is validated by loading its versioned descriptor
against the exact linked Module, then stored by host target and binary digest:

```text
<module-identity>/behavior/<target>/<behavior-sha256>/behavior.dylib
```

The final filename is `behavior.so`, `behavior.dylib`, or `behavior.dll` for
the host platform. A second binary digest for the same Module and target is
rejected rather than selected implicitly. `Compiler::load_behavior("module")`
discovers the single installed candidate and rechecks both its path digest and
descriptor before binding it.

Installing the same identity is idempotent. Installing another digest under the
same name and version is rejected rather than silently replacing content.
Before publication, `install` links the complete import closure and instantiates
every named graph through the same validation path as `check`; a syntactically
valid but unresolved or ill-typed Module is never published.
Module text and an optional behavior are first assembled in a hidden directory
on the same filesystem and become visible together through one atomic rename.
Adding behavior to an existing Module uses the same staging-and-rename rule.
Failed or interrupted staging directories are neither listed nor resolved, and
reinstalling an existing identity revalidates its stored Module and behavior
content rather than trusting the directory name.
Uninstallation targets one exact `name@version`. It first renames that version
out of the visible repository namespace and then reclaims its files. Resolution
therefore sees either the complete version or no version, never a partially
deleted package; an interrupted removal tombstone remains invisible. The CLI
reports what it removed.

## Resolution

```cpp
joggle::Compiler compiler;
compiler.search(module_root);
compiler.load("miniai.joggle");

if (!compiler.link()) {
  compiler.diagnostics().print(std::cerr);
}
```

`link` resolves missing imports recursively from configured roots, chooses the
highest installed version satisfying the source import range, validates every
installed path against the parsed name, version, and digest, and then checks the
complete import closure for version mismatches and cycles.

Import aliases are local source spelling only. Lock entries always record the
real Module name, version, and digest, so a local prefix never becomes a second
package identity.

`joggle run` writes one derived Module named `<source>_<graph>_compiled`; its
imports use the exact linked versions. Distinct graph members and their source
therefore have distinct package names, and replaying a derived single-graph
Module preserves its name. A valid repository admits only one digest for a given
name and exact version, and a Compiler rejects explicitly loaded Modules with
conflicting identities. The derived text is reusable without embedding a digest
in ordinary import syntax. For transport to another repository or for an
executable behavior closure, generate and replay a lock; the lock carries the
module and binary digests.

## Lock replay

A generated lock records the exact root and dependency identities:

```text
joggle-lock 1;
root miniai@1.0.0#<digest>;
module bitmath@1.0.0#<digest>;
behavior bitmath@1.0.0#<module-digest> macos-arm64#<binary-digest>;
```

It is consumed explicitly:

```cpp
joggle::Compiler compiler;
compiler.search(module_root);
compiler.lock("joggle.lock");
compiler.load("miniai.joggle");
compiler.link();
```

Replay fails if the root differs, an entry is unused, a dependency is absent,
the locked version violates an import, or the exact digest is not installed.
For the host target it also fails if a behavior entry names another Module
identity, the content-addressed binary is missing, or the file content differs
from its locked digest. Loading behavior while a lock is active requires a
matching entry. The lock therefore selects executable content rather than
merely repeating version ranges.

`joggle lock` first performs the same closure and named-graph validation as
`check`, then records installed behavior for the machine on which the lock is
generated. That makes an executable lock target-specific; a different target
needs its own behavior entry rather than silently substituting another binary.
A behavior artifact contains executable callbacks only. Every declaration comes
from canonical Module source and is resolved through runtime handles.
