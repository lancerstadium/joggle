# Spatial convolution

This source-only module supplies one alternative implementation family for
`nn.conv2d`. One `convolve` function owns the loop semantics; ordinary
overloads adapt the compact, explicit-layout, and bias/activation signatures.
It keeps reduction order unchanged for each output but moves output rows and
columns inside the reduction loops. Consecutive output columns then update
consecutive input and output elements, giving a C compiler a vector-friendly
innermost loop.

Apply it before target preparation:

```sh
joggle run spatial.apply semantic.jog \
  -M examples -M build/modules > selected.jog
joggle run c.prepare mem.plan selected.jog \
  -M examples -M build/modules > prepared.jog
```

`spatial.apply` uses compiler-owned call-site instantiation. Equal concrete
configurations share one private helper, static configuration becomes part of
its body, and weights remain normal parameters. The module does not edit ONNX,
the shared `nn` semantics, memory planning, or C emission.

The compact overload uses NCHW input, OIHW weights, and NCHW output. The
general overloads instead consume the same explicit axis lists as `nn`, so a
non-default layout is never silently treated as NCHW. The element type remains
a generic `Ty`; the selected target still decides which concrete scalar types
it can represent. The `[impl: "spatial"]` tag belongs entirely to this module:
`impls` discovers the family without a core registration table.
