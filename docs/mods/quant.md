# Quantization

`quant@3` declares tensor-typed affine boundaries:

```joggle
fn quantize_linear<E,Q,X:list<int>,Z:list<int>>(
  input: tensor<E,X>, scale: tensor<f32,Z>, zero: tensor<Q,Z>, axis: int = 1
) -> tensor<Q,X>;

fn dequantize_linear<Q,X:list<int>,Z:list<int>>(
  input: tensor<Q,X>, scale: tensor<f32,Z>, zero: tensor<Q,Z>, axis: int = 1
) -> tensor<f32,X>;
```

They are frontend-neutral ordinary calls. Scale and zero-point tensors encode
per-tensor or per-axis parameterization through their normal types. Numerical
bodies and hardware mappings remain implementation work; there is no native
callback per quantization operation.
