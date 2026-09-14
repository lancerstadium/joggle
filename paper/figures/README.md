# Paper figures

Every figure in this directory is rendered from preserved study records.  The
vector PDF is the manuscript asset; the PNG is a high-resolution review
preview.  No value is entered by hand.

## Extension surface

`extension-surface.pdf` compares the currently measured authored surface of
the four frozen extension tasks across Joggle, TVM, and ONNX-MLIR.  Its panels
show files, nonblank non-comment lines, and source bytes separately; they are
not combined into an ease or usability score.  Bars start at zero.  A cross
marks a task that stopped before a measurable implementation, while hatching
marks measured partial source that still stopped at a required endpoint.

Regenerate from `paper/data/extension-footprint-pilot.csv`,
`paper/data/extension-tvm-pilot.csv`, and
`paper/data/extension-onnx-mlir-pilot.csv`:

```sh
python3 paper/render_extension_figure.py
```

The intended citation location is Section 5.5 after the paragraph defining the
six separately recorded dimensions.  The present three-panel figure is a
rigorous draft: build files, registrations, dependencies, and artifact counts
will be added only after the Joggle record uses the same normalized schema as
the two external baselines.
