# Local workspace

This visible directory holds machine-local files, not release source. Only this
README is tracked. The public reproduction entry point is
[artifact/README.md](../artifact/README.md); committed measurements are in
`paper/data/`. Build the current checkout in the root `build/` directory.

| Directory | Contents |
| --- | --- |
| `cache/` | Downloaded models, original experiment records, dependencies and generated outputs |
| `envs/` | Local Python environments; recreate them on another machine |
| `legacy-builds/` | Preserved earlier build trees and their experiment outputs; not current executables |
| `research/` | Earlier research-tool backups |
| `site/`, `site-gems/`, `site-checks/` | Local website build, build dependencies, and browser previews |

## Existing records

The 30 September cleanup moved `.cache/` to `local/cache/`, `.research/` to
`local/research/`, and the two `.venv-*` environments to `local/envs/`. Earlier
root-level build trees were moved to `local/legacy-builds/`. No measurement rows
or raw Agent responses were deleted or rewritten. Historical provenance may
therefore contain the old paths: resolve `.cache/X` as `local/cache/X`, and an
old `build*/X` as `local/legacy-builds/build*/X` when inspecting these records.

Do not execute relocated CMake builds: their caches contain absolute paths.
Use a fresh `cmake -S . -B build` for current source. Python environments are
machine-specific and are not part of an AE bundle.

Before distributing raw records, select the datasets required by the protocol
and review them for credentials, provider metadata and machine-local paths.
This directory is not implicitly included by a Git commit or source archive.
