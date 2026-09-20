# Define a fusion policy

A project mod can reuse the generic chain matcher while choosing its own
source and destination functions:

```jog
mod my_opt
use opt

[entry, target: "my_accelerator"]
fn fuse_conv_norm_act(m: Mod) -> bool {
  return opt.fuse(
    m,
    ["frontend.conv", "frontend.norm", "frontend.relu"],
    "my_accelerator.conv_norm_act"
  )
}
```

Run it explicitly with `joggle run my_opt.fuse_conv_norm_act model.jog -M ...`.
The names and `target` tag belong to this mod; Joggle does not register or
interpret them. `opt.fuse` follows only single-use chains, while `ir.fuse`
checks the selected region's order, live-in/live-out boundary, and dominance
before committing the rewrite.

The `onnx-zoo-mobilenet` test exercises the same contract through
`script.fuse_onnx`, checks all three source calls decrease by the fused count,
round-trips the result, and verifies that a second application is idempotent.
