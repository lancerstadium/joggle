# Paper figures

Only submission candidates rendered from frozen records belong here. The old
pass table and three-panel source-line chart were removed: both described
implementation inventory rather than testing a systems claim.

The figure selection and evidence order are maintained in
[the research plan](../plan.md). The following inventory describes existing
draft assets, not completed evidence for the revised thesis:

1. **Unit-of-change figure (implemented in `submission/main.tex`):** a vertical
   co-design revision crosses source relation, graph rewrite, tensor body,
   schedule, target hook, and artifact ABI in a phase-aligned stack. The Joggle
   row keeps those roles distinct while placing them in one module lifecycle
   above a progressive-program rail. It is a conceptual vector figure, not a
   measured comparison.
2. **Mechanism figure (implemented in `submission/main.tex`):** a checked trace
   of `examples/edge`, not a synthetic three-state sketch. `edge.apply` selects
   compatible external matrix and multi-result calls while retaining an
   incompatible-layout convolution; `c.prepare` subsequently exposes the
   remaining portable bodies as loops without disturbing the external calls.
   The three excerpts are revisions of one serialized program and use the same
   typed object model and verifier. Replaced operation/value handles may still
   become stale; the figure therefore claims object-model continuity, not
   identity preservation for every node. Reproduce columns B and C from a
   configured tree with:

   ```sh
   ./build/joggle run edge.apply examples/edge/model.jog \
     -M examples -M build/modules
   ./build/joggle run edge.apply c.prepare examples/edge/model.jog \
     -M examples -M build/modules
   ```

No placeholder chart is checked in before its complete record set exists.
Figures use the venue's two-column geometry and native vector/PDF or LaTeX
output. The user-supplied visual examples define the desired density and shared
alignment; they do not authorize synthetic values.
