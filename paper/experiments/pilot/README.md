# Pilot: superseded schema-1 manifests

These two files are the earlier protocol manifests for the same-job system
comparison. They are schema 1 and unreferenced: they name subjects with `name`
rather than `system`, and they carry an absolute compiler path.

The current manifests are [`../mnist.json`](mnist.json) and
[`../mobilenetv2.json`](mobilenetv2.json), which are schema 2. Those are the
files the promoted records cite: `paper/data/mnist-systems-pilot.json` records
its manifest path as `paper/experiments/mnist.json`. No record anywhere cites
this directory.

## Why it is kept

It is the only remaining description of the earlier protocol, and the schema
change it records is the reason the two manifest generations are not
interchangeable. It is history, not an input: nothing reads it, no command
depends on it, and it should not be used to reproduce anything. A reader looking
for the current protocol wants the files one level up.
