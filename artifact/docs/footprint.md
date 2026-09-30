# Change footprint

[Evaluation entry point](../README.md). Run all commands from the repository root.

The ownership study uses complete native extension packages, independently of
the single-file Agent tasks. Its measurement boundary and admission conditions
are recorded in `PROTOCOL.md`. The current native-package collector measures
the observed deployment and maintenance patches, including publication files;
these counts are not hunk-minimized. The older Git-patch mode remains available
for reproducing source-patch experiments. Its minimizer uses an isolated archive
and private index, without changing the live checkout or creating a worktree.

The low-bit and fused-convolution directories also contain native xDSL package
manifests. Their wheels register a transform pass and C-emission target through
`xdsl.universe`, reusing each package's `reference.py` implementation. A clean
consumer discovers `study-lowbit-lower` / `study-lowbit-c` and
`study-qconv-fuse` / `study-qconv-c` through `xdsl-opt`; no driver edits or
compiler-tree patches are needed. Build wheels from source copies in a scratch
directory and install into a separate target directory so the frozen Agent
environment stays unchanged. Package admission checks the installed entry
points, transformed graph, emitted C, and runtime outputs. It is a prerequisite
for the ownership comparison, not a change-footprint result.

The same directories contain native MLIR CMake packages. Each builds and
installs a pass plugin that `mlir-opt --load-pass-plugin=...` discovers.
`study-lowbit-lower` / `study-qconv-fuse` transform the graph;
`study-lowbit-c{output=...}` / `study-qconv-c{output=...}` emit the C artifact.
The plugin resolves runtime symbols from its host, preserving one MLIR pass
registry. Joggle packages install the corresponding Jog implementation as
`module.jog` using `joggle mod install`. All three systems use the same
semantic fixtures and independent executable checks.

The complete ownership matrix is fixed in `manifests/package-changes.json`:
two initial integrations and six independent maintenance changes, each paired
across the three systems. `manifests/package-sources.csv` lists the exact
implementation and publication files included in each package.

```sh
python3 artifact/minimize_patch.py \
  --repo /path/to/system --base BASE --head CANDIDATE \
  --output-patch local/cache/artifact/task.patch \
  --oracle-log local/cache/artifact/task-oracle.log \
  --log local/cache/artifact/task-minimization.json \
  -- ./task-oracle

python3 artifact/collect_footprint.py \
  --cases local/cache/artifact/footprint-cases.csv \
  --output local/cache/artifact/figure-05-footprint.csv
```
