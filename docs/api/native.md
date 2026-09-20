---
title: Native mod ABI
description: Bind codecs and host functions through the versioned C-compatible value ABI.
---

# Native mod ABI

Use the native ABI only when `.jog` must cross a binary boundary, such as
Protobuf/FlatBuffers decoding or VM execution. Analyses and transforms should
normally remain source mods.

## Source declaration first

```jog
mod codec

[role: "read"]
fn read(data: bytes) -> str;
```

The source declaration is the type contract and visible API. Native code binds
the body-less symbol; it does not register a pass or operator kind.

## ABI objects

| Object | Purpose |
| --- | --- |
| `joggle_api` | versioned host function table |
| `joggle_module` | opaque binding context |
| `joggle_call` | one invocation state |
| `joggle_value` | nil/bool/i64/f64/string/handle/bytes transport |
| `joggle_fn` | bound native function callback |

## Entry point

```cpp
static bool read(joggle_call* call, void*) {
  if (call->api->arg_count(call) != 1)
    return call->api->fail(call, "codec.read expects one argument");

  joggle_value input{};
  if (!call->api->arg(call, 0, &input) || input.kind != JOGGLE_BYTES)
    return call->api->fail(call, "codec.read expects bytes");

  // Decode and keep returned storage alive for the required call lifetime.
  return true;
}

JOGGLE_MODULE_EXPORT bool joggle_module(
    const joggle_api* api, joggle_module* module) {
  if (!joggle::compatible(api)) return false;
  return api->bind(module, "read", &read, nullptr);
}
```

The abbreviated callback shows validation order. Maintained complete examples
are `modules/onnx/src/onnx.cpp`, `modules/tflite/src/tflite.cpp`, and
`modules/vm/src/vm.cpp`.

## Layout and verification

```text
codec/
  module.jog
  src/codec.cpp
  native/
    joggle_codec.so
```

- reject an incompatible API version/table size;
- check argument count and kind before reading unions;
- call `fail` with stable actionable diagnostics;
- publish results only after successful work;
- expose no C++ objects across the C ABI;
- test source checking, load failure, invalid arguments, and a real invocation.
