# QConv module

`qconv@1.0.0` is a source-grounded semantic seam for the standard NCHW QDQ
convolution profile. It is an independently installable Module, not a compiler
operation kind or an integer backend.

## Source contract

The Module's ordinary `conv` function accepts quantized input, weight, bias,
and output parameters plus the four normal convolution properties. Its type
signature fixes the audited storage contract while keeping every tensor shape
generic:

- input and output storage are `u8`;
- weight storage is `i8`;
- bias storage is `i32`;
- scales and expressed tensors are `f32`;
- NCHW activation/output axes are 1; and
- weight/bias axes are 0.

The source body is exactly three `quant.dequantize` calls, one `tensor.conv`,
and one `quant.quantize` call. There is no separate semantic callback or
trusted operation table. An implementation that later replaces `qconv.conv`
must refine this visible meaning.

## Transformation

The two compiler functions are ordinary overloads:

```joggle
fn run(input: function) -> function;
fn run(input: module) -> module;
```

`@qconv.run` recognizes only the typed storage and axis profile above. It also
requires the floating Conv result to feed only the output Quantize. Its native
selector returns the 15 semantic arguments of `qconv.conv`; the general
`joggle::outline` primitive constructs and instantiates the call, locks those
arguments to the selected SSA values, proves equivalence, and performs the
bounded atomic sweep. Unsupported expressions remain unchanged.

Pure shared ancestors are preserved by the general replacement primitive. If
two Conv branches consume one activation Dequantize, the first replacement
retains it for the other branch and the next bounded sweep removes it when its
last user disappears. The transformation neither clones that call nor needs a
graph, anchor, pattern, or pass object.

A source pipeline is therefore just normal composition:

```joggle
fn compile(input: bytes, name: string) -> module {
  model = @onnx.read(input, name);
  return @qconv.run(model);
}
```

## External-model evidence

On the hash-pinned ONNX Model Zoo QDQ SqueezeNet 1.0 graph, all 26 eligible
convolutions become `qconv.conv`. Sixteen candidates form eight branch pairs
that share one activation Dequantize per pair. The transformation absorbs 70
unique Dequantize calls, 26 Conv calls, and 26 output Quantize calls, then adds
26 QConv calls. The complete Function consequently changes from 399 to 303
calls/constants while retaining all 228 Module-owned constants and every
fused-call source location.

The original and transformed whole Functions pass bounded definitional
equivalence, the transformed Module verifies, and its `qconv`, `quant`, and
`tensor` dependency closure is derived from the resulting IR.

## Deliberate boundary

This result packages an exact floating-reference QDQ expression behind a
normal user function. It does not yet execute an integer convolution, remove
quantization error, or establish latency, memory, or energy gains. The next
gate is a bit-accurate integer realization tested against the existing affine
oracle and a trusted runtime, followed by measured edge execution. That work
must remain an extension of this source contract rather than a hidden backend
rule.
