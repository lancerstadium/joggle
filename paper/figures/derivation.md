# Compiler-function derivation

Selected figure: `derivation.png`, manuscript `fig:derive`.
Created 2026-09-16 with the built-in image-generation tool; reviewed by the
assistant against source. Accountable author verification remains pending.

## Purpose and boundaries

This is a mechanism illustration, not a performance chart. It replaces the
code-only figure; the literal API excerpts remain native LaTeX below the image.
The source procedure and copied private closure stay distinct. Only the
selection predicate changes, while type and lifetime tests remain in the
abbreviated true path. False paths continue the slot loop. Structural matching
and host verification precede successful publication; they do not establish
semantic equivalence. The diagram groups obligations rather than specifying
a literal instruction schedule: matching precedes insertion, and verification
also checks the resulting edit. The driver is retargeted in the compiler edit.
Later invocation edits a separate subject in a separate transaction.

The small function/helper graphs and a/b/c subject graph are schematic, not
literal topology. Public calls remain resolved dependencies. Storage tags are
annotations, not new model operations. The original procedure remains callable.
The lower lane is the recorded planner-to-artifact path, not another exposure
step or an implementation of dependency-sensitive recomputation.

## Evidence

- [Recipe](../experiments/reuse/module.jog): `derive`, closure cloning,
  structural matching, typed insertion, predicate replacement, erasure, and
  driver retargeting; `better` implements capacity-aware selection.
- [Driver](../experiments/reuse/compiler.jog): original
  `mem.plan(m, fn)` call inside `apply`.
- [Study](../experiments/derive-prepare.md#manuscript-evidence-map):
  C-derive-composition / E-reuse-path and C-derive-boundary / E-reuse-check.
- [Recorded outcome](../experiments/reuse/result.json): separate numerical
  and declared-slot checks; no values are embedded in the mechanism image.

Only these minimal diagram facts and short source excerpts were supplied to
the generator, under the user's request for image-generated mechanisms. No
full manuscript, reviewer material, raw model data, or external figure was
uploaded. No borrowed logos or source illustrations are included. Publication
policy/disclosure and human approval remain submission checks.

## Production

Final tool file: `exec-cc1e0dd8-3bf0-4f4e-b3f2-bfd6d9126545.png`.
Copied without cropping, compositing, or pixel editing into this directory.
Earlier candidates remain outside the repository. Corrections removed invented
function signatures/helper names, removed a duplicate false edge, and restored
the compiler-to-execution invocation route. Long code moved to native LaTeX to
avoid tiny raster text. Short diagram labels remain raster serif/monospace
approximations, not guaranteed embedded fonts. The caption and Description
supply a textual explanation independent of color.

The figure uses labeled before/after paths, explicit T/F branches, and named
invocation rather than color alone. Model execution data are not generated.

Validation: the selected asset is 2060 by 763 pixels. The rebuilt manuscript
has 15 pages including references, with technical content ending on page 12.
Page 7 was rendered at 1700-pixel page height to inspect the diagram, native
code, caption, and surrounding text; page 12 was separately checked after
compressing repeated Discussion explanations. No clipping or overlapping
labels were observed in these views. The build reports no overfull boxes or
undefined references/citations; the existing ACM-reference-format warning
remains. This bounded visual/source check is not full submission approval.

## Generation record

<details>
<summary>Initial specification</summary>

```text
Use case: infographic-diagram.
Create ONE high-resolution, publication-quality computer-systems research mechanism figure, landscape about 2.5:1. Dense but exceptionally readable at double-column paper width. It must explain a real compiler-function derivation, NOT performance results or a generic workflow. White opaque page, flat pale blue / warm cream / pale teal grouped regions, precise thin charcoal strokes, dark serif lettering like Times or Libertine, exact small monospaced code. Bold compact panel headings. Modern MICRO/ISCA academic diagram aesthetic. No glow, shadows, gradients, black background, marketing icons, vendor logos, watermark, full figure caption, or invented numerical results. Visually varied code, nested control-flow, and dependency glyphs rather than repeated big cards. Generous enough inter-element spacing to avoid collisions, but no empty large panels.

Layout: upper band about 70% height consists of THREE tightly aligned panels (a), (b), (c), overall heading "Compiler definitions". Lower band about 30% height is a distinct pale gray-blue wide lane heading "Subject program". No arrow may imply that compiler IR is converted into model IR. Invocation is the only bridge. Use visible numbered tiny callouts for three edit operations and an actual code excerpt in middle; avoid paragraphs.

(a) "Derive"
Small installed-source sheet labelled "mem.plan", linked downward to a compact set of two overlapping helper sheets labelled "Private helpers". Adjacent separate sheet labelled "ranked" linked to its OWN separate copied helper set labelled "Copied helpers". Two parallel short horizontal arrows from source to corresponding copy, labelled together "Clone closure". Public dependency is a small unfilled capsule below "Public calls" with arrows from the two planner sheets, denoting references kept explicit rather than copying public functions. Small green check beside original labelled "Original retained". At foot of (a) exact code in two lines:
let source = ir.find("mem.plan",
  [ty("Mod"), ty("Fn")])
let plan = ir.clone(code, source, "ranked")
Make source and copy distinct, do not visually imply in-place editing of mem.plan.

(b) "Edit the decision"
Schematic nested slot-loop body with two short side-by-side paths labelled "Before" and "After".
Before: "selected < 0" small amber diamond -> "Type match" rectangle -> "Disjoint lifetime" rectangle -> "Select slot" rectangle.
After: coral changed node "reuse.better(...)" -> identical "Type match" rectangle -> identical "Disjoint lifetime" rectangle -> identical "Select slot" rectangle.
Each path means conjunction of conditions; only TRUE arrows continue downward, marked T; one clean side rail labelled "Otherwise continue" collects false branches without crossing text. The type and lifetime conditions are visibly retained. The whole pair has a tiny heading "Slot loop (abbreviated)". Changed predicate only is coral; retained tests are pale blue. A small coral arrow horizontally between first nodes is labelled "Replace". Beneath graph, compact exact code excerpt (real insertion API, not pseudo syntax):
let need = ir.call(code, guard,
  "operator []", [counts, item], ty("int"))
let replacement = ir.call(code, guard,
  ir.find("reuse.better"),
  [selected, slot, need, capacities], ty("bool"))
Small footer "ir.replace  →  ir.erase" denotes actual editing operations.
No giant red cross over old algorithm: original is valid, not erroneous. Do not describe lifetime checks as a theorem. Do NOT put tensor icons among these compiler-code operands.

(c) "Check and select"
At upper left small magnifier over three linked nodes, labelled "Structural match". Tiny attached chips "Loop state", "Uses", "Writeback". Below diamond labelled "Checks" with two branches: green "Pass" → "Edited definitions"; red "Fail" → "Rollback edit". Adjacent compact driver diagram:
"apply" -> "mem.plan" (original call)
and below
"apply" -> "ranked" (changed call with coral arrow).
A small curved arrow between these two call edges labelled "ir.retarget".
Footnote exactly "Structure and types ≠ semantic equivalence".
Green check beside "Original callable". Do not suggest two separate apply definitions must both be present in one module; these are Before / After driver snippets, label them.
Output is checked edited compiler definitions, not a model or executable.

LOWER WIDE LANE:
Left: compact program sheet labelled "Prepared model" with a tiny unlabelled generic three-node computation graph inside; below tiny label "Separate Mod".
Center: same-style program sheet labelled "Storage annotations" with miniature list of slot tags attached to its graph nodes. Horizontal arrow from prepared model to annotated model labelled "Run derived driver".
From upper (c) "Edited definitions", a single blue downward invocation arrow joins the CENTER of that horizontal transformation arrow, labelled "Invoke"; it must NOT terminate on a model node as if merging the two IRs. Use a visible small function-execution circle on the lower horizontal arrow for this junction if helpful.
Right: two linked compact boxes "c.place" -> "C emission" -> small stacked output file glyphs ".c  .h  weights". Bracket over these boxes labelled "Unchanged consumers".
Footer along lower lane: "Compiler edit and subject execution are separate transactions".
No extra graph conversion stage, no exposure event, no dependency recomputation, no performance numbers. The lower model graph is illustrative, not a purported literal model topology.
Tiny legend at edge: gray dot "Retained", coral outline "Edited", blue arrow "Invocation".
Ensure all text exact and distinguish arrow types by labels, not color alone. Whole diagram is compact, aligned, detailed, pristine, with real meaningful relationships and crisp readable typography.
```

</details>

<details>
<summary>Source corrections</summary>

```text
Edit this academic mechanism figure. Preserve overall layout, headings, palette, all middle-panel real ir.call code, condition graphs, checks, transaction footer, and high-resolution readability. Correct only the following invented snippets/labels and associated semantics, with exact text:
1. In panel (a), REMOVE all invented source-code lines inside the upper mem.plan and ranked sheets (fn plan(ctx,m), select_slot, etc.). Replace contents with a miniature unlabelled three-operation chain, matching across the two sheets. No fictitious helper routine or function signatures. In both lower Private helpers / Copied helpers sheets REMOVE slot_cost and lifetime_ok fake code and replace with simple unlabelled connected function nodes. Keep the sheet headings. In Public calls capsule REMOVE ir.*, type.*, util.* and replace with exact label "Resolved declarations". Keep arrows from each planner sheet to this capsule. Source source-definition graph and copied graph must be distinct objects connected by clone arrows, not in-place mutation. Keep real let source / ir.clone code at bottom unchanged.
2. In panel (c), driver snippets: remove entire invented fn apply(m: &Mod) wrapper. The upper Before (original call) white box should contain ONLY monospaced "mem.plan(m, fn)" with a small plain label "inside apply" above the code. Lower After (changed call) box should contain ONLY "ranked(m, fn)" with same small "inside apply". The new name ranked is coral, operands black. There is NO &Mod, no fake function signature, no single-argument call. Preserve ir.retarget arrow.
3. Lower Subject program lane: the graphs before and after must have IDENTICAL three graph-node identities a (top), b (bottom left), c (bottom right) and same a->b / a->c arrows. The first sheet is still Prepared model with these a b c labels. In Storage annotations sheet do NOT rename graph nodes S0,S1,S2. Keep a b c and attach separate tiny slot-tag rectangles beside nodes, visually annotations not operations. Remove ALL "S0: param", "S1: weight", "S2: act" text. Add a tiny label "Schematic graph" beside the lower lane heading. Only one "Separate Mod" label, below Prepared model. Below the annotated model replace duplicate Separate Mod by "Same subject". This schematic is NOT a literal model topology or allocation.
4. Middle-panel before path's "Otherwise continue" is awkwardly left of graph while its rail is on the right; move that label directly under the before rail, with adequate spacing, keeping both control paths aligned. No new text or numbers.
5. Preserve exact inserted code in middle, readable serif/mono typography. Make flat fills uniform, reduce any remaining gradient texture. Keep opaque white page. Do not add invented code, performance numbers, extra arrows, or logos.
```

</details>

<details>
<summary>Print production</summary>

```text
Make a final production edit of this figure. Preserve the facts and all graphical structures except the specific corrections below. Need legibility at 7-inch double-column print width.
1. Remove the TWO large small-font code boxes at the bottom of panels (a) and (b). These exact source listings will be typeset separately in native LaTeX below the image, so do not include them anywhere in the raster. Keep "ir.replace → ir.erase" as a compact action label. Do not add replacement prose. Redistribute the freed height to make all existing graphs and their labels larger, and compress the overall composition vertically if appropriate. Increase small diagram labels by ~20%, preserving clean spacing. White page, uniform flat pastel panels, no gradients, no texture. Target aspect about 2.7:1.
2. The BEFORE condition graph in panel (b) has a wrong EXTRA arrow running from LEFT corner of selected<0 diamond down the left edge to Otherwise continue. DELETE that entire left arrow and left Otherwise continue label. There must be ONLY TWO outputs from the diamond: T DOWN to Type match, F RIGHT to the shared false rail. The other conditions' F exits also feed that SAME RIGHT rail. At bottom of that right false rail put compact two-line label "Continue". AFTER side identical topology: T down, F right shared rail ending in "Continue". Do NOT add third exits to any test. Both Select slot boxes remain on the TRUE chain, separate from Continue.
3. In panel (a), original "mem.plan" and copy "ranked" sheets retain miniature unlabelled three-operation chains. Helpers remain unlabelled graphs. Public calls contains only "Resolved declarations". No invented code or names.
4. In panel (c) keep exact call snippets "mem.plan(m, fn)" and "ranked(m, fn)" with "inside apply". No fake signature. Keep Structural match (Loop state / Uses / Writeback), checks and pass/fail, edited definitions and rollback.
5. Lower subject lane remains unchanged semantically: Schematic graph, a/b/c identities retained across Prepared model to Storage annotations, annotation tags outside nodes, invoke enters execution circle not a model node, c.place -> C emission -> .c .h weights. Transactions separate. Keep legible legend.
Do not introduce new words, performance claims, additional arrows, or equations. Maintain crisp dark serif type and mono for function/API identifiers. The new asset is the graphical portion only; real longer API listings will be added outside it.
```

</details>

<details>
<summary>Invocation routing</summary>

```text
Correct ONE missing connection in this image. Preserve EVERYTHING else exactly, including all text, panel layout, control-flow arrows and size.
The blue Invoke arrow in the bottom Subject program lane is disconnected from the compiler definitions. It must originate at the bottom edge of the GREEN "Edited definitions" box in upper-right panel (c). Route a THIN BLUE ORTHOGONAL LINE from that box downward, around the LEFT edge of the pale "Structure and types ≠ semantic equivalence" strip (do not cross any letters), then leftward along the narrow WHITE GUTTER between upper and lower bands, then join the existing vertical blue Invoke arrow above the f() circle. This is ONE continuous directed route: Edited definitions -> Invoke -> f(). Only one arrowhead at f(). Do not add any arrow to a model node. Do not erase or change labels or graph nodes. White background and current palette unchanged. No other modification.
```

</details>
