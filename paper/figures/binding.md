# Binding-preserving replacement

Selected asset: `binding.png`, manuscript `fig:binding` (Figure 4).
Built-in image generation, 2026-09-16. Source and visual checks by the assistant;
accountable author verification remains pending.

## Mechanism and source

The diagram illustrates the existing regression in `test/module.cmake`,
provider/facade/client upgrade case. The installed facade imports the provider
and declares `keep`; the client imports the facade and calls `choose(x)` with
an i32 argument. The provider declares `choose<T: Ty>(x: T) -> T`.
The candidate facade retains `keep` and adds `choose(x: i32) -> i32`.
The more-specific candidate changes the selected declaration, despite retaining
the previous exports. The regression expects upgrade rejection and compares
the installed facade's bytes against its original source.

Dashed edges denote imports; solid edges connect call/declaration ports.
Top and bottom are alternative environments, not successive committed states.
The old provider remains present and imported in the candidate environment.
`keep`, `choose<T>`, and `choose(i32)` abbreviate declaration signatures;
they are not complete source listings. The scope is previously resolved static
calls in reverse dependents installed in the destination root. No runtime
hot-swap, equivalence theorem, external-client protection, or measured
cross-system advantage is implied.

Source anchors:

- `tool/module.cpp`: `compatible` checks old public signatures;
  `affected_modules` computes the installed reverse closure;
  `preserves_resolutions` compares qualified normalized signatures;
  `validate_upgrade` checks before filesystem replacement.
- `test/module.cmake`: additive-overload case, currently lines 303--335.
- [Evidence map](../studies/extension-study.md#manuscript-mechanism-evidence):
  C-module-binding / E-module-upgrade and E-module-tests;
  C-module-scope describes the publication boundary.

## Production and validation

The source is `exec-82be9a7d-c490-4ddd-b2e3-5829f5abff2a.png`, copied without
pixel editing, cropping, or compositing. Size: 1509 by 1042 pixels. Only the
selected image is stored here. Two corrective generations attach the installed
binding to actual call/declaration ports and route it clear of their headings.
Raster lettering approximates serif/monospace typography; editable embedded
labels remain a production task. Labels and line styles carry meaning in
addition to color. Native LaTeX caption and Description explain the diagram.

Only minimal diagram facts from the requested project were sent for generation.
No full manuscript, reviewer material, raw model data, or external illustration
was uploaded. No third-party logos or borrowed graphical elements are included.
Publication disclosure/policy checks and author approval remain open. This
turn inspects existing implementation and regression source; it does not rerun
the module test or claim new empirical measurements.

Final layout check: the manuscript rebuilds to 15 pages including references;
the complete conclusion and start of references are on page 12. Pages 5--7
were inspected for the definition, equation, figures, and surrounding text,
and page 12 for the technical-content boundary. The new single-column diagram
has legible labels and no connector crossing text. The build has no overfull
boxes or undefined citations/references. The pre-existing ACM-reference-format
warning and publication-ready editable typography remain unresolved. This is
a scoped source/layout check, not a complete submission audit.

## Generation record

<details>
<summary>Initial specification</summary>

```text
Use case: infographic-diagram.
Create one compact, precise computer-systems paper mechanism diagram explaining module upgrade rejection caused by changed overload resolution. Intended for SINGLE COLUMN width ~3.3 inches, so generous large lettering, no fine-print code. High resolution, landscape about 1.45:1. White opaque page, thin consistent charcoal rules, uniform pale blue installed-state fill and pale warm ivory candidate-state fill. Elegant dark serif like Times/Libertine; module names and short function labels monospace. Flat modern scientific look. No black background, texture, gradients, glow, vendor logos, decorative icons, caption or performance data.

Exactly TWO stacked state panels and one small check/outcome strip at bottom. The states are alternatives, NOT a pipeline of transformations.

TOP PANEL heading "(a) Installed".
Three module cards horizontally left-to-right "client", "facade", "provider".
client has a small rounded call port labelled "choose(x)" and a separate compact line "x: i32".
facade contains one neutral small declaration port "keep".
provider contains one neutral small declaration port "choose<T>".
Short DASHED horizontal arrows between card HEADERS client -> facade -> provider labelled "use" each. These represent imports. Leave a separate clear band above card contents for the solid binding edge:
A thin BLUE SOLID curved/orthogonal arrow FROM client choose(x) call port TO provider choose<T> declaration port, spanning ABOVE facade's contents without touching keep. Label that arc "Resolves to". This edge must bypass facade's keep declaration.
No direct solid edge from client call to keep; no bidirectional arrows.

BOTTOM PANEL heading "(b) Staged candidate".
Same three module cards at identical positions with same existing ports.
client remains choose(x), x: i32. provider remains choose<T>.
facade retains the keep port and ADDS a coral-outline declaration port "choose(i32)" below it, with small coral "+" to left marking addition.
Keep exactly the same dashed use import chain client -> facade -> provider.
The BLUE binding arc to provider is NOT present here. Instead draw one clearly SOLID CORAL arrow FROM client choose(x) TO facade's new choose(i32) port. Label compactly "New target".
provider still exists unchanged and is still imported, but its generic function is not selected in this staged environment.
A tiny dark label next to the added port "More specific" is optional only if it fits legibly.

BOTTOM CHECK STRIP in two compact columns:
left green CHECK "Exports retained"
right coral CROSS "Binding changed"
Below a single outcome bar "Reject upgrade" -> "Keep installed".
Show no install success for this candidate, no behavior-equivalence claim, no live hot swap, no automatic update propagation. Rejection happens during staged validation, installed modules remain intact.
Tiny but legible legend at VERY bottom: dashed arrow "Import", solid arrow "Call target". No other legends.
Names choose<T>, choose(i32), keep are abbreviated declaration labels, not full function definitions: do not invent function bodies or wrappers.
The meaningful contrast is a preserved export set with a changed resolved call target. Emphasize graph correspondence, port-to-port arrow accuracy, and large readable text. Total textual density modest enough for single-column reproduction; diagrams, not paragraphs.
```

</details>

<details>
<summary>Port attachment</summary>

```text
Correct ONE semantic detail of this figure; preserve all labels, module cards, palette, lower state, outcome strip, and layout.
In TOP (a) Installed panel, the blue solid "Resolves to" arrow currently connects module headings instead of the exact function ports. Change it to originate on the TOP edge of the WHITE "choose(x)" port in client, run vertically upward through a CLEAR area WITHOUT crossing the word client (route around the left side of that heading), then across the existing upper route, then descend to terminate on the TOP edge of the WHITE "choose<T>" port in provider, routing around the word provider so it never crosses letters. Arrowhead must touch the choose<T> declaration port, not the provider card or title. Exactly one continuous route with exactly one arrowhead. Keep imports as the unchanged dashed horizontal arrows. Do not connect anything to keep. Everything else unchanged. Keep crisp white background and flat pastel fills.
```

</details>

<details>
<summary>Clear routing</summary>

```text
Fix ONLY the blue solid route in the top panel. It currently CROSSES and OBSCURES client/provider lettering. REMOVE both vertical blue segments through those words.
Route it OUTSIDE the headings: originate at the LEFT EDGE of white choose(x) port (approximately x=85,y=244), go left to x=60 then UP to y=98, across to x=1450, DOWN to y=244, then LEFT with the single arrowhead touching the RIGHT EDGE of choose<T> port (approximately x=1430,y=244). All coordinates refer to this 1510x1042 source image. Thus two vertical segments near the outermost margins of their module cards, completely away from words. The arrow terminates HORIZONTALLY into the declaration port, not above provider. Keep the existing Resolves to label and all other image content unchanged. Exactly one continuous blue path with one arrowhead. No blue lines crossing any text. Preserve both state panels, all dashed import arrows, bottom red arrow and all text.
```

</details>
