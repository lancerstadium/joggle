# Complete-model mixed-state trace

This is a mechanism record, not a numerical or performance result. The input is
the existing ignored ONNX MobileNetV2 semantic IR at
`build-study/c-current/semantic.jog` (SHA-256
`7a9a1d442068d5d346109808fca0b15ac77a453ad649289ea3b488d5821f93e3`).
Its model provenance belongs to the existing MobileNetV2 study; the large IR
must be regenerated or transferred with that provenance before external replay.
The installed `edge` and `c` modules are unchanged.

From the repository root:

```sh
cmake -S . -B build
cmake --build build --target joggle-progressive-trace -j2
./build/joggle-progressive-trace build-study/c-current/semantic.jog examples modules build/modules
mkdir -p build-study/progressive-trace
./build/joggle run edge.apply build-study/c-current/semantic.jog \
  -M examples -M modules -M build/modules > build-study/progressive-trace/selected.jog
./build/joggle run c.prepare build-study/progressive-trace/selected.jog \
  -M examples -M modules -M build/modules > build-study/progressive-trace/prepared.jog
./build/joggle check build-study/progressive-trace/selected.jog \
  -M examples -M modules -M build/modules > /dev/null
./build/joggle check build-study/progressive-trace/prepared.jog \
  -M examples -M modules -M build/modules > /dev/null
```

The optional C++ runner traces one **in-memory** `Mod` without reparsing
between transitions. It verifies each stage, finds the first Conv and ReLU by
their imported output names, and compares their original `Fn`, `Op`, and `Val`
handles after each edit. Its observed output on 2026-09-15 was:

```text
revisions=1,56,411 main_same_after_select=true main_same_after_prepare=true
selected_conv_callee=conv2d semantic_convs_before=54 external_convs_selected=54 external_convs_prepared=54
semantic_relus_before=36 semantic_relus_selected=36 semantic_relus_prepared=0
loops_before=0 loops_selected=0 loops_prepared=102
conv_op_live_after_select=false conv_val_live_after_select=false
relu_op_live_after_select=true relu_val_live_after_select=true
conv_op_live_after_prepare=false conv_val_live_after_prepare=false
relu_op_live_after_prepare=false relu_val_live_after_prepare=false
```

The selected call prints unqualified `conv2d(...)` because the model now uses
`edge`; this is the target declaration, not a newly exposed Conv loop. The old
Conv operation and result are replaced at selection. The old ReLU operation
and result survive selection, then are replaced during preparation. The
enclosing `main` function retains its handle in this in-memory run. Handle
equality is store-local and does not carry across serialization/reparsing.

The two serialized snapshots are ignored build artifacts, not checked-in
fixtures. Their current SHA-256 values are:

| Snapshot | SHA-256 |
| --- | --- |
| selected | `e63050feee32e8708522bb0c8cccd9ff926214cb39b8e96522c7945de268a5f1` |
| prepared | `6c453c397ea3ce2d08ca334dd3a78812aa4a56d300717b787c9401ba1b884c19` |

The printed first layer changes from `nn.conv2d(data, weight, [2,2],
[1,1,1,1], [1,1], 1)` to `conv2d(data, weight, 1, 224, 224, ...,
strides)` and remains a call after preparation. The adjacent `nn.relu(bn0)`
remains semantic in the selected snapshot; preparation prints an allocated
output, `for i_1 in 0..size`, and `if value > f32(0)` assigning the output.

The selected external declaration has not been compiled or compared with
the MobileNetV2 numerical oracle here. Verification establishes typed IR
legality, not semantic equivalence. The observed 54/36/102 counts describe
this one model and selector, not generic transformation coverage.
