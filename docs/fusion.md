# Fusion module

`fusion@1.0.0` is the first independently installable transformation Module.
It demonstrates the extension mechanism; it is not a compiler-core fusion
registry or a claim of broad operator coverage.

The Module declares two generic `conv_relu` functions, with and without bias.
Each is an ordinary source-defined function whose body is the portable
`tensor.conv` followed by `tensor.relu` meaning. The bodyless `run` overloads
accept either a `function` or a `module`:

```joggle
fn run(input: function) -> function;
fn run(input: module) -> module;
```

A user pipeline remains ordinary Joggle source:

```joggle
fn compile(input: bytes, name: string) -> module {
  model = @onnx.read(input, name);
  return @fusion.run(model);
}
```

## Correctness path

For each single-use `tensor.conv` result consumed by `tensor.relu`, the native
implementation builds two concrete typed expression Functions: the original
pair and a call to the matching generic `fusion.conv_relu` overload. It then
uses bounded source-body equivalence before opening the existing atomic
replacement transaction. A failed proof or edit returns no transformed value.

This division is intentional:

- the Module source declares the operation and its reference meaning;
- native C++ discovers a domain-specific opportunity;
- the core supplies Function construction, verification, equivalence, and
  transactional replacement;
- the transformed IR contains an ordinary call and therefore derives its
  `fusion@1.0.0` dependency without hidden registration state.

No pattern object, schedule class, pass manager, target hierarchy, or second
graph is involved.

## Real-model evidence

The hash-pinned ONNX Model Zoo SqueezeNet 1.1 test composes `@onnx.read` and
`@fusion.run` through a source-defined compiler function. All 26 Conv/ReLU
pairs become reference-bodied `fusion.conv_relu` calls, reducing the Function
from 117 to 91 calls while retaining 52 constants and all fused-call ONNX
locations. A separate whole-Function definitional-equivalence check succeeds,
and the transformed Module verifies.

The whole-model check exposed and fixed an important scaling bug: normalizing
a shared tensor DAG as a tree repeatedly expanded common ancestors. The
normalizer now memoizes canonical encodings by existing `Value` identity. This
is only a cache over the authoritative Function; it is not another IR.

## Deliberate limit

This module handles only Conv followed by ReLU. It does not make scheduling,
layout, memory, device, or profitability decisions. Those remain later
extension work, and additional transformation control will be added only when
a second real optimization cannot compose with ordinary `fn` and `@call`.
