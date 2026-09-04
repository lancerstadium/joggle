# QDQ composite module

`qdq@1.0.0` is an independently installable library of transparent QDQ
composites. It is not an IR level, a quantized-operator hierarchy, or a kernel
backend. Each composite is an ordinary source-bodied `fn`; its visible body is
the semantic contract that transformations and later implementations must
preserve.

## Separation of concerns

The quantized path has three independent owners:

- `quant` owns operator-independent affine quantize/dequantize semantics and
  the executable numerical oracle;
- `tensor` owns ordinary tensor operations such as convolution; and
- `qdq` names useful compositions of those functions without changing their
  meaning.

The core `joggle::outline` primitive performs bounded, equivalence-checked
reverse inlining. `qdq` supplies only profile-specific eligibility and argument
mapping. It does not add a QDQ node kind, matcher language, pass hierarchy, or
target assumption.

This split follows the useful common denominator in established systems.
[MLIR Quant](https://mlir.llvm.org/docs/Dialects/QuantDialect/) represents the
stored/expressed relation and Q/DQ casts independently of individual tensor
operators. [StableHLO quantization](https://openxla.org/stablehlo/quantization)
likewise defines quantized element types and conversions, with reversible
QDQ-to-quantized-operation legalization. ONNX keeps
[QuantizeLinear](https://onnx.ai/onnx/operators/onnx__QuantizeLinear.html) and
[DequantizeLinear](https://onnx.ai/onnx/operators/onnx__DequantizeLinear.html)
around normal operators while also permitting concrete QOperator forms.
[TVM's custom datatype mechanism](https://tvm.apache.org/2020/09/26/bring-your-own-datatypes)
registers a representation and its per-operation lowerings rather than making
one operation the datatype abstraction. Joggle differs in mechanism—ordinary
functions and definitionally checked outlining—but preserves the same
separation.

## First composite

The first function is deliberately named `nchw_conv`, not an unqualified
`conv` and not a per-operator top-level Module. Its name exposes the
layout-specific profile. It accepts quantized input, weight, bias, and output
parameters plus the ordinary convolution properties. Its signature fixes the
audited storage contract while keeping tensor shapes generic:

- input and output storage are `u8`;
- weight storage is `i8`;
- bias storage is `i32`;
- scales and expressed tensors are `f32`;
- NCHW activation/output axes are 1; and
- weight/bias axes are 0.

The body contains exactly three `quant.dequantize` calls, one `tensor.conv`,
and one `quant.quantize` call. A future `qdq.nchw_conv` integer implementation
must refine this visible meaning. Other layouts, operators, or storage profiles
belong in separate, explicitly named ordinary functions in this Module (or in
another user Module); they do not require per-operator compiler subsystems.

## Transformation

The two compiler functions are ordinary overloads:

```joggle
fn run(input: function) -> function;
fn run(input: module) -> module;
```

`@qdq.run` currently recognizes only the typed NCHW Conv profile above. Its
native selector returns the 15 semantic arguments of `qdq.nchw_conv`; the
general outlining primitive constructs and instantiates the call, locks those
arguments to the selected SSA values, proves equivalence, and performs a
bounded atomic sweep. Unsupported expressions remain unchanged.

Pure shared ancestors are preserved by the core transformation. If two Conv
branches consume one activation Dequantize, the first replacement retains it
for the other branch and a later bounded sweep removes it after its last use.
The transformation neither clones that call nor needs a public graph, anchor,
pattern, or pass object.

A source pipeline is normal composition:

```joggle
fn compile(input: bytes, name: string) -> module {
  model = @onnx.read(input, name);
  return @qdq.run(model);
}
```

## External-model evidence

On the hash-pinned ONNX Model Zoo QDQ SqueezeNet 1.0 graph, all 26 eligible
regions become `qdq.nchw_conv`. Sixteen candidates form eight branch pairs
that share one activation Dequantize per pair. The transformation absorbs 70
unique Dequantize calls, 26 Conv calls, and 26 output Quantize calls, then adds
26 transparent composite calls. The complete Function changes from 399 to 303
calls/constants while retaining all 228 Module-owned constants and every
composite-call source location.

The original and transformed whole Functions pass bounded definitional
equivalence, the transformed Module verifies, and its `qdq`, `quant`, and
`tensor` dependency closure is derived from the resulting IR.

This evidence validates transparent composition and shared-DAG rewriting. It
does not establish an integer convolution, numerical equivalence beyond the
visible reference body, or latency, memory, and energy gains.
