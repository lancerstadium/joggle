# Spatial convolution

This source-only module supplies one alternative body for the ordinary
six-argument `nn.conv2d` function. It keeps reduction order unchanged for each
output but moves output rows and columns inside the reduction loops. Consecutive
output columns then update consecutive input and output elements, giving a C
compiler a vector-friendly innermost loop.

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

The implementation currently covers static NCHW input, OIHW weights, and NCHW
output through the compact six-argument overload. Its element type remains a
generic `Ty`; the selected target still decides which concrete scalar types it
can represent. Other layouts and fused-bias calls deliberately remain on their
existing implementations.
