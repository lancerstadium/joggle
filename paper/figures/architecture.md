# Architecture figure

Selected asset: `architecture.png` (Figure 2, `fig:progressive`).
Generated 2026-09-16 with the built-in imagegen tool, then revised after visual inspection. This replaces the three-card TikZ trace; it does not replace the trace evidence.

## Scientific meaning

The main view separates import dependencies, compiler definitions, a subject program, and the core services. It is not a mandatory pass sequence. Source definitions in Mod S, derived definitions in Mod K, and the subject in Mod P are distinct. Copying a private helper is not sharing its mutable body. An invocation acts on P; compiler definitions are not emitted as model code.

The subject graph is schematic. External calls, portable calls, and explicit bodies illustrate coexistence, not a measured model or an execution trace. Its three independent branches must not be interpreted as executing a call followed by its expansion. The C files are one example output path, not a claim that every illustrated abstract state is directly acceptable to C emission.

## Evidence and boundaries

- C-architecture-imports / E-module-headers: `modules/c/module.jog:2-6`, `modules/vm/module.jog:2-5`, `modules/nn/module.jog:2-3`, `modules/mem/module.jog:2-5`. Five selected edges: c→mem, c→opt, vm→opt, nn→tensor, mem→tensor. Additional imports intentionally omitted.
- C-architecture-derivation / E-reuse-path: `paper/experiments/reuse/module.jog`; `paper/experiments/derive-prepare.md`; the source, derived compiler, and subject distinction in the manuscript's metaprogramming section.
- C-architecture-identity / E-progressive-trace: `paper/experiments/progressive-trace.md`. The enclosing main handle survives the measured trace; replaced Op/Val handles expire. This is not a promise that arbitrary edits preserve every enclosing identity.
- C-architecture-output / E-c-source: `modules/c/module.jog` definitions of `prepare`, `source`, `header`, `data`. The diagram elides target preparation and validation before emission.
- C-architecture-mlir / E-transform-implementation: [official MLIR Transform tutorial, chapter 2](https://mlir.llvm.org/docs/Tutorials/transform/Ch2/), the C++ apply implementation example. [MLIR language reference](https://mlir.llvm.org/docs/LangRef/) supports coexistence of dialects. T1/T2/T3 and the payload graph are schematic; no named fake Transform operations. This is a comparison to a documented extension path, not an ecosystem-wide impossibility claim.

Agent inspected the local sources and tool output; accountable human scientific verification remains pending. No inference speed, update speedup, unique-DAG claim, or implemented dependency-sensitive recomputation is implied.

## Visual review

The rejected first image confused exposure with sequential execution and aimed the invocation arrow outside P. The selected revision repairs both and distinguishes S/K/P. Relative positions show organization rather than order. Dashed gray edges are imports, blue edges editing/invocation, and black internal edges computation. Other black arrows between containers are labeled with their relation.

Submission-size typography still needs a full-paper audit: EuroSys 2027 requires at least 10-point figure text. A high-resolution bitmap does not by itself establish that minimum. This asset is an architecture draft, not typography-certified submission artwork.

## Generation record

Initial prompt:

```text
Use case: infographic-diagram. Create an original publication-quality COMPUTER SYSTEMS CONFERENCE architecture diagram for Joggle. White background, landscape approx 2.1:1, very high resolution and crisp readable text. Dense meaningful vector-like scientific drawing; Times-style serif headings/body; code in restrained monospace. Thin clean navy outlines, pale blue groups, pale teal source/functions, pale amber program bodies, muted gray infrastructure. No gradients, drop shadows, decorations, logos, huge whitespace, or three equal columns. This is architecture, NOT a staged pipeline poster.

Composition: narrow LEFT comparison inset (22% width), large RIGHT Joggle architecture (76%). On right, an upper composition area, a central live-program area, and a thin shared infrastructure foundation. Relative positions mean organization, not sequential stages. Keep every stated relationship exact.

LEFT heading "MLIR Transform example". Small cyan code card "Transform IR" with miniature chain of 3 op rectangles. Black invocation arrow down into pale gray card "C++ apply implementation" with braces icon and 3-line pseudo-body of plain graphical strokes (NOT invented code). Black arrow down into white card "Payload IR" containing mixed labeled nodes "tensor", "scf", "arith". Short footer "Mixed levels are supported". A fine divider between Transform IR and C++ card labeled "Implementation boundary". NO red X or claim MLIR cannot compose modules, no stack of compulsory IRs. This inset is a documented example, not a blanket assessment.

RIGHT main heading "Joggle architecture".
Upper area uses two adjoining regions of unequal widths:
At upper-left (~38% of main right area) "Selected module imports": a compact DAG of rounded rectangles. Three upper nodes "c", "vm", "nn"; two middle nodes "mem", "opt"; one lower node "tensor". Draw ONLY these six thin dashed directional import edges, arrows importer -> dependency: c -> mem; c -> opt; vm -> opt; nn -> tensor; mem -> tensor. There are FIVE edges, not six: exactly those five. Avoid overlapping unrelated nodes; route edges cleanly. Note below "Subset of imports". No connection from nn to c and no execution-order implication.
At upper-right (~60%) a bordered mint-tinted container "Compiler definitions   Mod K". Two small function-body diagrams in it: "Source Fn" with nodes A -> B -> C, and "Derived Fn" with nodes A -> B' -> C. B' amber with pencil icon. Curved blue arrow source to derived labeled "clone + edit"; source retained. Small helper rectangle below both labeled "Private helper closure" with individual unobtrusive connectors from each function; label closure "copied with definition" to avoid shared mutable helper implication. Better draw separate helper h and h' beneath source and derived respectively and connect h -> h' by clone arrow. No formula notation in node names. Footer short "Source-defined analysis and transformation". A tiny code strip inside container uses exact real signature "fn prepare(m: Mod) -> bool". This is declaration excerpt, not a complete body.
A blue solid invocation arrow from the derived function downward to the central program boundary labeled "invoke on P". A subtle connector from selected-imports group to compiler definitions labeled "resolve"; no claim DAG itself executes.

Central wide area under upper regions: container "Subject program   Mod P" (separate from Mod K). A genuinely useful MIXED COMPUTATION GRAPH. Input circular port "x" at left branches into a capsule "External call" with tiny label "h(x)" on upper track AND an explicit body region on lower track. Lower track is entered from x, region labeled "Exposed body"; inside show nested rectangular loop frame "for i in 0..n" containing y[i] -> op1 -> op2 -> z[i], clean return loop arrow along frame bottom. Both track outputs merge at plus node then lead to output port "y". This is schematic typed dataflow, not a numerical model trace. Place a compact unexpanded capsule "Portable call" on the lower track BEFORE exposed body, with an open small folded-code glyph and a blue curved arrow down indicating "expose locally"; represent this as a transformation zoom: dashed outline from Portable call to body underneath, NOT execution of portable call followed by its body. Important: x -> portable call -> plus is the executable lower branch; the expanded body appears as a local magnification below that call with dotted guide lines to the call, not a second sequential computation.
At right outside this subject container, 3 small aligned output file icons connected by ONE black arrow from subject labeled "c.source / header / data". Files labeled ".c", ".h", "weights". This is an example artifact path. Fit them compactly.
Under Subject program footer within container: "Same Fn identity; replaced Op and Val handles expire".

Bottom across Joggle only: thin gray foundation strip "C++ core" then distinct short items separated by slim rules: "Types" | "Resolution" | "Verification" | "Editing" | "Invocation". Above the strip a small shared label "Common objects: Mod · Fn · Blk · Op · Val". Both K and P use this common object model but remain distinct stores/subjects. No arrow suggesting compiler code emitted into model artifact.
Bottommost compact legend: dashed gray arrow "import", solid black arrow "data / invocation" (prefer separate blue invocation arrow and black data flow labels), dotted guide "body detail".
No incremental recomputation claim, no speedup numbers, no made-up code, no three stages. Make text comfortably readable in a full-width two-column paper. Strong visual hierarchy and tasteful restrained scholarly color. No external figure number/caption.
```

Correction prompt (selected):

```text
Edit this scientific architecture figure, preserving its colors, crisp line quality, overall asymmetric layout, font hierarchy, module DAG and C++ core strip. Fix scientific semantics precisely:
1. The blue "invoke on P" arrow MUST end on the TOP BORDER of the large "Subject program Mod P" container, not in whitespace beside files. Route from Derived Fn down through the gap, then left to a point on the subject container top border. No arrow aimed at artifacts.
2. REPLACE ALL CONTENT inside Subject program except its title and footer. Draw one clean mixed-representation DAG: circle x at left branches into THREE independent horizontal branches; upper capsule "External call" with small h(x), middle capsule "Portable call" with small g(x), lower pale amber nested region "Exposed body" with loop label "for i in 0..n" and x[i] -> op1 -> op2 -> z[i] nodes. Route all three outputs separately into a single circular plus node, then output y. The exposed body IS the third executed branch, NOT the implementation of g(x); NO arrow between Portable call and Exposed body; NO dotted magnification lines; NO exposed-body connection from External call. Input x feeds each of the 3 branches. Keep clean non-overlapping connectors. This schematic represents coexistence of states, not sequential exposure. Footer exactly "Same Fn identity; replaced Op and Val handles expire". This body should look dense and precise but be readable. Output files stay OUTSIDE the program.
3. In top right, heading becomes "Compiler definitions" without Mod K applying to both. Change "Source Fn" to "Source Fn in Mod S", and "Derived Fn" to "Derived Fn in Mod K". Keep source helper h and copied helper h', retained source and clone arrows. The source and derived compiler stores are distinct.
4. MLIR Transform IR miniature operation boxes must read "T1", "T2", "T3" (schematic) instead of invented transform.match/rewrite/apply names. In left bottom replace "produces" arrow label with "edits". Keep the honest footer "Mixed levels are supported".
5. Remove the dotted body-detail entry from legend since there is no magnification. Legend dashed gray = "import"; black solid = "data flow"; blue solid = "invocation / edit". "resolve" arrow stays separate.
6. Do not introduce any numbers, timing claims, extra code, crossed arrows, labels floating over connectors or new content. Keep all five selected import DAG edges unchanged.
Final must be a polished dense full-width computer systems conference diagram, white background, fine strokes, no shadows.
```

Selected tool output: `exec-b2510387-8692-4195-99ba-38400c1ae981.png`. Only the selected image is stored here. The user's reference diagrams guided organization and visual density; their mechanisms and data were not copied.

