# Joggle overview

Figure 1 is a conceptual overview of one central obstacle and two operational
pressures, not three equal contribution claims or a measured comparison.
The selected asset is `overview.png`, used at full two-column width.

## Argument and layout

| Role | Diagram | Response | Implemented capability |
| --- | --- | --- | --- |
| Central problem: restricted customization | A parameter-only procedure boundary leads to copying its body | Derivable compiler functions | Derive and edit a source function while retaining the original |
| Supporting pressure: fragmented integration | One definition touches several interfaces and revisions | Modular management | Compose definitions through imports and checked replacement |
| Supporting pressure: redundant work | Whole-body exposure exceeds local consumer demand | Consumer-directed exposure | Retain abstract calls alongside selectively exposed bodies |

The upper panels illustrate situations an extension can encounter, not universal
limitations of MLIR, TVM, or another named system. Crosses locate these obstacles.
Checks identify Joggle facilities, not measured superiority, exclusive novelty,
or three co-equal contributions.
Dependency-sensitive invalidation and recomputation remain **planned**. Only the
corresponding inset uses a dashed enclosing border and it has no green check.

The six surrounding panels stay general; only the center uses a concrete model.
Three different graphical forms distinguish the concerns: a body boundary and
source derivation, scattered integration files and a triangular module graph,
and a dependency graph with broad versus local exposure. Large repeated
red/green checklist cards are removed. Crosses and checks sit next to the
obstruction or facility they explain.

The edit nib meets a boundary, one changed definition fans out across files,
and a whole-graph revisit surrounds a local demand. Below, the source remains
unchanged while the derived operation changes; independently named modules
compose; and only B exposes a body while A/C/D/E remain compact calls.
The repeated right-hand graph is deliberate correspondence, not duplicated
decoration. The generic graph is not an additional measured model.

Code braces, file sheets, target glyphs, edit nibs, and symbol ports denote
roles rather than product affiliations. No vendor logos or newly invented
Joggle logo are introduced. The small integration-file snippets are schematic,
not literal Joggle source listings. Source op2 and derived op2′ must differ.

## Source anchors and boundaries

- The center abbreviates the first Conv–BatchNorm–ReLU chain in the
  [MobileNetV2 trace](../experiments/progressive-trace.md). Input is
  `[1,3,224,224]`, weight `[32,3,3,3]`, output `[1,32,112,112]`.
- The external Conv call and exposed BatchNorm/ReLU bodies share the enclosing
  `main` Fn. Auxiliary operands are omitted. BatchNorm labels summarize
  computation; they are not literal registered operation names.
- ReLU follows `modules/nn/module.jog`: initialize the output tensor to zero
  before the loop, read each input, and write only positive values. The false
  path retains zero. The drawing is abbreviated control/data flow, not an
  exact serialized IR listing.
- Source and derived procedures occupy a compiler Mod. Invocation acts on a
  separate subject Mod; their shared Fn object model does not merge their roles.
- Mod A/B/C and their type/function slots are schematic composition examples,
  not concrete module names or a mandatory three-layer hierarchy.
- The trace establishes typed structure and enclosing-function identity, not
  numerical validation of the selected external implementation. No speedup,
  memory reduction, or completed incremental compiler is claimed by this figure.

## Recent visual references

The previous white figure and the user's nested function-body crop remain the
primary visual foundation. Black-background candidates are not reused.

- [LoopFrog, MICRO 2025, Figures 2–3](https://www.cl.cam.ac.uk/~tmj32/papers/docs/erdos25-micro.pdf):
  inspected the original PDF renderings. Consistent loop components connect
  static/dynamic views; the architecture drawing distinguishes retained and
  modified structures. This informed object correspondence and local highlights.
- [LUT Tensor Core, ISCA 2025, Figures 5–6](https://arxiv.org/pdf/2408.06003):
  inspected the original PDF rendering as a cross-check of the composition.
  A compact workflow connects concrete computation to a mechanism close-up.
  Its hardware hierarchy, numerical results, and claims are not copied.

These are observations about specific figures, not a venue-wide style standard.
A white page, restrained pale fills, clean routing, serif headings, and brief
monospace fragments preserve readability. Red and green also carry cross/check
shapes so color is not the only cue.

## Production

Selected tool output: `exec-b77138e9-1307-4d6a-a54a-2526b83a8c2f.png`,
copied to `overview.png`.

Generated and corrected with the built-in image tool, using the previous white
asset as the editing reference. This directory retains only the selected image;
rejected tool outputs stay outside the repository. Raster lettering approximates
the requested serif/monospace style rather than guaranteeing embedded fonts.
The figure and caption do not turn planned updating into an implemented result.
Quantitative tables remain native LaTeX and are not generated images.

Validation: 1774 × 887 pixels, opaque RGB. The manuscript rebuilt to 13 pages;
the updated page 2 was rendered and inspected at two-column width without
clipping. Call/body contrast and source/derived distinction were rechecked.
Small raster labels and existing ACM reference-format/balance warnings remain
production limitations, not evidence of submission readiness.

## Generation record

These prompts document the current asset, replacing the superseded production
brief rather than accumulating successive redesign plans.

<details>
<summary>Symbolic refinement</summary>

```text
Use case: infographic-diagram. EDIT the attached Joggle white research overview. Improve symbolic communication, layout variety and visual polish, without changing the scientific claims. This is a contemporary computer-systems conference mechanism figure, not a marketing dashboard.

PRESERVE: the white landscape page, three aligned challenge/response columns, the SAME CENTRAL MobileNetV2 detailed mixed-state graph, exact central tensor shapes and computation sequence, the six main headings, serif typography and small planned-update inset. Keep all center semantics: external conv2d then explicit BatchNorm and ReLU; output zero initialization precedes ReLU loop; Source Fn unchanged and derived Fn locally edited; compiler Mod and subject Mod separate. Do not introduce results, timing numbers or claims about specific competitors.

CHANGE: the peripheral composition is too repetitive: repeated three-operation chains, stacks of identical boxes and six oversized pink/green checklist rectangles. REMOVE those six side checklist cards. Redistribute that space to the actual diagrams. Place short red-cross / green-check takeaways directly NEXT TO the relevant obstruction or changed structure. Give EACH COLUMN a distinct visual grammar:
column 1 = body boundary and function derivation;
column 2 = scattered files versus connected modules;
column 3 = work scope and local demand.
Use small meaningful flat pictograms IN the diagrams: code braces for source, an edit nib for intended modification, file sheets for separate integration sites, interlocking symbol ports for imports, a target cursor for consumer demand, circular arrows for repeated scans. No vendor logos or invented brand logos. These glyphs are readable semantic symbols, NOT decoration. No padlock suggesting access control/security. One compact motif per concept.

STYLE: pristine OPAQUE WHITE background and corners. Print-quality at high resolution, landscape 2:1. Fine consistent charcoal strokes and restrained pastel blue, teal, amber. Gentle solid color fields, no gradients, drop shadows, glow, transparency, checkerboard, dark background, sketching, noisy wiring or rainbow. Serif text like Times New Roman, brief monospace snippets. Keep clean gutters and hierarchy. Small tinted numbered tabs can align upper/lower columns. Avoid enclosing every detail in a box. Favor varied silhouettes and neatly routed short lines. Dense with meaningful relationships but no paragraph blocks.

TOP LEFT "1  Restricted Customization":
Draw a generic compiler procedure as a horizontal thin-framed BODY with op1→op2→op3. Program m enters and m′ leaves. Config enters through an exposed small port on the boundary, labeled "Parameters". A visible edit-nib arrow attempts to reach op2 but STOPS at a red barrier on the body boundary, with a red × and "Body access" right at the collision. This must visually show why configuration does not enable arbitrary internal revision. Below, a small offset duplicated source-sheet glyph with tiny same-operation motif connects via a detour arrow labeled "Copy"; a coral changed line on this duplicate and red × "Duplicated logic". Avoid another full-width three-box chain. Point: blocked local revision forces copying. Not a universal claim about every compiler.

BOTTOM LEFT "1  Unified metaprogramming":
Two compact SOURCE SHEET shapes labelled "Source Fn" and "Derived Fn" inside a light boundary "Compiler Mod". Each contains a real miniature operation graph, not textual paragraphs. Source: gray op1→gray op2→gray op3. Derived: gray op1→coral op2′→gray op3. Linking arrow "clone + edit", edit-nib icon landing successfully on derived op2′ and adjacent green ✓ "Body editing". Mini helper() tab under each, unchanged, with green ✓ "Source reuse". Below the compiler boundary a separate subject-program glyph "Subject Mod", reached ONLY from derived function via "invoke". Preserve role separation. Source op2 MUST NOT have prime. This is editable body reuse, not just a generic copy icon.

TOP MIDDLE "2  Fragmented Integration":
Small source-definition glyph at left with short "type T" and "fn f" lines. A single coral edit marker Δ on that definition fans out to FOUR integration sites arranged compactly as offset document sheets with different meaningful tiny icons and short labels "Types", "Transforms", "Targets", "Emission". Use braces, a rewrite-arrow, a small target/chip outline, and an output-file respectively. Each document has ONE coral changed line and neutral retained lines. Route the fanout as a clean shared trunk. Red × "Scattered edits" beside the fanout. A red broken-link / mismatched-port symbol on the connection between two documents marks "Revision coupling". Show the coordination obstacle, not a random network of rectangles. No fabricated numbers or vendor logos.

BOTTOM MIDDLE "2  Modular management":
Three distinct compact MODULE tiles arranged as a shallow TRIANGLE, labels "Mod A", "Mod B", "Mod C". Each exposes the SAME small type/fn symbol ports; use interlocking connection/port glyphs, not jigsaw toys. Three short import connections A→B, B→C, A→C labelled once "imports"; no crossing lines. Green ✓ "Shared resolution" at the common symbol connections. At bottom, one compact candidate-file → checkmark-in-diamond → installed-module path. A solid failure return path to "Keep prior". Green ✓ "Checked replacement" next to the checking decision. No big duplicate subpanel framing, no hierarchy implication. Modules remain independent, composition is not merging all code into one monolith.

TOP RIGHT "3  Redundant Work":
Use a compact generic dependency DAG with five compact CALL capsules A, B, C, D, E (A branches to B/C; B/C flow toward D; D→E). A small target cursor labelled "Demand" points ONLY to B. Yet ALL call bodies are exposed: miniature nested body rectangles below/inside each node, using minimal unlabelled operation dots rather than five copies of op1/op2/op3. Bodies outside B are muted amber and marked with small red crosses; a nearby short label "Unneeded detail". A broad coral CIRCULAR REVISIT arrow encircles the entire dependency structure, with a short red × "Repeated work" annotation. This visibly contrasts narrow demand with large work scope, not a quantitative timing plot.

BOTTOM RIGHT "3  Reactive updates":
The SAME compact A/B/C/D/E dependency silhouette but B alone becomes an enlarged nested loop body, others remain compact call capsules. Target cursor "Demand" points at B. Green ✓ "Selective exposure" placed beside B, green ✓ "Mixed representation" beside a retained call/body boundary. Do not highlight all nodes as changed. This responds directly to the upper work-scope problem.
Below, retain a SMALL dashed-outline inset "Dependency update (planned)" with a red changed node→affected descendants, and a muted separate branch "Reuse". This inset has NO green check. No claims of implemented dependency-sensitive recomputation. All other panel enclosures solid; dashed enclosing borders mean planned only.

CENTER: Keep the supplied detailed MobileNetV2 stem diagram as the stable visual anchor, with its external call capsule, nested BatchNorm loop, ReLU conditional write and tensor ports. Preserve exact annotations and sequence. Do not simplify it to generic icons or enlarge it at the expense of the six peripheral panels. It alone uses a specific model example. It is labelled "Abbreviated IR trace", not performance evidence.

Finish with a tiny unobtrusive legend for "Unchanged", "Edit", "Call", "External", "Body", "Planned". Make red × marks visually attached to the actual blocked access, coordination break, and wasteful scope; green ✓ marks attached to the corresponding implemented mechanism. Semantic variety and directly visible pain points are the priority. Do not add extra title, slogan, prose or decorative badges.
```

</details>

<details>
<summary>Exposure and composition correction</summary>

```text
Correct ONLY the two lower-right mechanism panels of this figure, preserving the white page, upper panels, central MobileNetV2 graph, bottom-left source derivation, typography and icons.

BOTTOM RIGHT "Reactive updates": The current drawing incorrectly exposes bodies of A C D E. REPLACE nodes A, C, D, E with small BLUE CALL CAPSULES containing ONLY their single letter. ERASE ALL the little dots and internal miniature bodies inside A C D E; no internal nodes inside those four capsules. Keep each capsule in the same location and retain the same connecting edges. ONLY B remains a large expanded body enclosing its operation chain and ports, now with an AMBER outer boundary and pale amber fill to match the Body legend. Label B "B (expanded)" unchanged. Demand still points at B. This contrast is essential: top-right shows all five bodies; bottom-right shows only ONE body and four closed calls. Keep planned inset and checks unchanged.

BOTTOM MIDDLE "Modular management": Make the Mod A/B/C composition a compact SHALLOW TRIANGLE, not another horizontal pipeline. Mod A at upper left, Mod C at upper right, Mod B centered slightly lower. Use short clean import arrows A→B, B→C and A→C, each arrow single-direction, never bidirectional. Keep their type/fn symbol ports and three distinct colors. Place "Shared resolution" beside these connections without overlap. Preserve the lower candidate→Check→Install/Keep prior workflow, but its enclosure must be SOLID LIGHT BLUE, NOT dashed. Only the Dependency update (planned) inset should have a dashed enclosing frame.

Everything else unchanged. Opaque white background; precise clean lines; do not introduce labels or performance claims.
```

</details>
