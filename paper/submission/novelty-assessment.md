# Novelty and positioning assessment — Joggle ("malleable compilation"), EuroSys

Assessed against `paper/submission/main.tex` (Intro §1, Related Work §6), plus `paper/plan.md`
and `paper/studies/literature-map.md`. Searches run 2026-09-17 via arXiv search and web search.
Every work relied on is linked; anything I could not verify is flagged as unverified.

Target claim, decomposed (as stated by the parent, matching §1 and §3.2):

- **(a)** derive a copy of a compiler function together with its private helper closure
- **(b)** revise a single internal decision in that copy
- **(c)** run the copy on a separate subject while the original stays callable and verified

---

## 1. Closest prior work, by area

### 1.1 Programmable / IR-level transformation

**MLIR Transform dialect** — Lücke, Zinenko, Moses, Steuwer, Cohen, CGO 2025 ([arXiv 2409.03864](https://arxiv.org/abs/2409.03864), [docs](https://mlir.llvm.org/docs/Dialects/Transform/)). Transformation strategies are IR over a separate *payload* IR, with typed handles, effects, and failure semantics; scripts can be inlined, simplified, and introspected, and composing them requires no rebuild. Adding a *new* action is a C++ `TransformOpInterface::apply` ([tutorial Ch. 2](https://mlir.llvm.org/docs/Tutorials/transform/Ch2/)).
Coverage: (a) **no** — an action's implementation cannot be derived in IR. (b) **partial** — you can compose and parameterize existing ops. (c) **yes at the script level** (transform IR is a separate program from the payload, and scripts can be optimized), **no for the action implementation**.

**MLIR PDL / PDLL** — [pdl dialect](https://mlir.llvm.org/docs/Dialects/PDLOps/), [PDLL](https://mlir.llvm.org/docs/PDLL/). Rewrite patterns are represented *as IR* and interpreted by `pdl_interp`: a rewrite rule can be added with no rebuild and no C++. But `pdl.apply_native_constraint` / `pdl.apply_native_rewrite` require externally registered C++, and PDL expresses mnemonic-shaped pattern rewrites only — it cannot express a pass's *decision procedure* (a slot-ranking rule, a register-allocation priority). **This is the closest existing "edit compiler behavior without a rebuild" facility in MLIR and the paper does not mention it at all.**
Coverage: (a) no. (b) partial, for pattern-shaped rewrites only. (c) n/a.

**TVM Relax / BYOC** — Lai et al., ASPLOS 2025 ([DOI 10.1145/3676641.3716249](https://dl.acm.org/doi/10.1145/3676641.3716249), [arXiv 2311.02103](https://arxiv.org/abs/2311.02103)); [TVM architecture](https://tvm.apache.org/docs/arch/). One IRModule holds graph functions, tensor programs, and external calls; partial lowering, analysis feedback, and joint caller/callee rewriting are documented capabilities. Extending it with a new pass or BYOC backend is a C++/Python change plus rebuild — which the paper itself measures.
Coverage: (a) no. (b) no for pass bodies (parameters and registered passes only). (c) no.

**Exo 2** — Ikarashi et al., ASPLOS 2025 ([DOI 10.1145/3669940.3707218](https://dl.acm.org/doi/10.1145/3669940.3707218), [arXiv 2411.07211](https://arxiv.org/abs/2411.07211)). Scheduling libraries built from inspect/action/reference primitives, with versioned cursors that survive edits. Already cited and fairly credited.
Coverage: (a) no (the editable unit is a schedule, not a compiler procedure's closure). (b) **yes, for schedules** — this is the closest published "revise one decision, keep the rest" mechanism. (c) **partial** — versioned cursors forward references across edits, but the subject and the schedule are different objects, not two modules under one verifier.

### 1.2 E-graph / equality saturation

**egg** — Willsey et al., POPL 2021 ([DOI 10.1145/3434304](https://dl.acm.org/doi/10.1145/3434304), [arXiv 2004.03082](https://arxiv.org/abs/2004.03082)). Fast, extensible e-graphs with e-class analyses: many equivalent programs coexist in one structure. **Tensat** — Yang et al., OOPSLA 2021 ([arXiv 2101.01332](https://arxiv.org/abs/2101.01332)); already cited. **SPORES** — Wang et al., VLDB 2020 ([DOI 10.14778/3407790.3407799](https://dl.acm.org/doi/10.14778/3407790.3407799)); not cited.
**eqsat** — Merckx et al. ([arXiv 2505.09363](https://arxiv.org/abs/2505.09363)) and **Tamagoyaki / "E-Graphs as a Persistent Compiler Abstraction"** — Merckx et al. ([arXiv 2602.16707](https://arxiv.org/abs/2602.16707)). E-graphs represented natively *in the compiler IR*, non-destructively, with variants persisting and interleaving with ordinary passes.
Coverage: (a) no. (b) no. (c) **this is the closest existing "several versions of the program stay alive inside one compiler IR"**. Joggle's §6.4 cites Tensat for search-space size but never mentions non-destructive rewriting, which is the mechanism closest to "keep the original live".

### 1.3 Multi-stage programming and staged DSLs

**LMS** — Rompf & Odersky, GPCE 2010 ([DOI 10.1145/1868294.1868314](https://dl.acm.org/doi/10.1145/1868294.1868314)); **Delite** — Rompf et al. ([arXiv 1109.0778](https://arxiv.org/abs/1109.0778)); **Terra** — DeVito et al., PLDI 2013 ([DOI 10.1145/2491956.2462166](https://dl.acm.org/doi/10.1145/2491956.2462166), [project page](https://terralang.org/publications.html)); **AnyDSL/Thorin** (already cited); **Weval** (already cited).
Coverage: staging *derives specialized definitions from* compiler/interpreter definitions, which is strictly stronger than (a) for the specialization case. (b) only by rewriting the staging program. (c) **partial** — the original definition remains a source program, not a live, verified, callable object in the same IR as the subject. Terra is worth naming explicitly: it is "compiler as a library" with Lua metaprogramming and an LLVM JIT, i.e. the same "no compiler rebuild" ergonomics the paper claims.

### 1.4 Reflective / self-modifying compilers, open compilers, compile-time MOPs — **the paper's biggest gap**

- **Open implementation** — Kiczales et al., "Open Implementation Design Guidelines", ICSE 1997; a readable summary and bibliography is in [Stodghill's survey "Open and Extensible Compiler Implementations"](https://www.cs.cornell.edu/stodghil/bernoulli/notes/oc/oc/). Its taxonomy is exactly this paper's design space: Style A (no strategy control), B (client declares usage), C (client selects from a menu), D (client *supplies the strategy*, e.g. a function pointer). Joggle's derivation is Style D carried one level deeper: the client supplies a **modified copy of the implementation body**.
- **Lamping, Kiczales, Rodriguez, Ruf, "An Architecture for an Open Compiler"**, IMSA 1992 ([Semantic Scholar](https://www.semanticscholar.org/paper/An-Architecture-for-An-Open-Compiler-Lamping-Kiczales/fbf7b68b097cedf1f51426fa98671fbcda11ad39)). The abstract states the goal as decomposing "the description of an implementation into a combination of many small, partially interacting choices" with "a decision making process that allows implementation choices to be made by a collaboration between user interventions and default decision making". **This is Joggle's thesis, stated in 1992.**
- **Open C++** — Chiba, OOPSLA 1995 ([DOI 10.1145/217838.217868](https://dl.acm.org/doi/10.1145/217838.217868)): a compile-time MOP where metaobjects are the AST classes and user meta-programs change how a program is compiled.
- **Engler, "Interface Compilation: Steps Toward Compiling Program Interfaces as Languages"**, IEEE TSE 25(3), 1999 ([DOI 10.1109/32.798327](https://dl.acm.org/doi/10.1109/32.798327)); the lcc hack lets user modules be **dynamically linked into the compiler and invoked**, so a user adds transformations and optimizations *without rebuilding the compiler*.
- **CLOS MOP** — Kiczales, des Rivières, Bobrow, *The Art of the Metaobject Protocol*, MIT Press 1991: object layout, dispatch, and inheritance are changed by specializing meta-level methods, and the default remains reachable via `call-next-method` — i.e. (b) with the original still callable.
- **Procedural reflection / 3-Lisp** — Smith, "The Implementation of Procedurally Reflective Languages", LFP 1984 ([DOI 10.1145/800055.802050](https://dl.acm.org/doi/10.1145/800055.802050)): a language reflecting on and modifying its own implementation ("reflective towers").
- Also in this cluster, for completeness: SUIF 2 / OSUIF, Zephyr/ASDL, FLINT (Shao, DSL '97) — a common typed IR shared by many language front ends and passes, which is the direct ancestor of "one representation for programs and compiler procedures".

Coverage: **strong partial (b) and (c)** via meta-level overriding and dynamically linked compiler modules; **(a) absent** — none derives a live definition *with its private helper closure* into a separate compiler module under one verifier, and none has the same-IR subject/compiler duality over one typed function model.

### 1.5 Open compilers, tuning, and plugin systems

**OpenTuner** — Ansel et al., PACT 2014 ([DOI 10.1145/2628071.2628092](https://dl.acm.org/doi/10.1145/2628071.2628092)); **CompilerGym** — Cummins et al. ([arXiv 2109.08267](https://arxiv.org/abs/2109.08267)); **MLGO** — LLVM's ML-guided optimization, which replaces inlining/regalloc heuristics with swappable learned models ([LLVM MLGO docs](http://releases-origin.llvm.org/22.1.0/docs/MLGO.html); canonical path is `llvm.org/docs/MLGO.html` — verify before citing); **Milepost GCC** — Fursin et al., IJPP 2011 (**DOI unverified**: my searches returned two conflicting DOIs, `10.1007/s10766-010-0137-2` and `10.1007/s10766-010-0161-2`); **LLVM pass plugins** ([LLVM dev meeting 2025](https://llvm.org/devmtg/2025-04/slides/technical_talk/graenitz_pass_plugins.pdf)) and [MLIR out-of-tree dialects/passes](https://mlir.llvm.org/docs/DefiningDialects/).
Coverage: these replace a *parameter, model, or pass selection*, not a body; (a) no, (b) no (heuristic replacement, not body revision), (c) no. They matter because they are the reviewer's "you can already change a compiler decision without touching the algorithm" ammunition.

### 1.6 Deriving/cloning code while keeping the original live — the systems precedent

- **Procedure cloning** — Cooper, Hall, Kennedy, PLDI 1992; journal version "A methodology for procedure cloning", *Computer Languages* 19(2), 1993 ([DOI 10.1016/0096-0551(93)90005-L](https://dl.acm.org/doi/abs/10.1016/0096-0551%2893%2990005-L)). The compiler clones a procedure, specializes the clone, and keeps both callable. Modern instances: GCC `cgraph_node::create_version_clone` (IPA-CP function versioning) and LLVM function specialization/outlining. **This is (a) implemented inside production compilers for three decades.**
- **Truffle/Graal** — Würthinger et al., "Practical Partial Evaluation for High-Performance Dynamic Language Runtimes", PLDI 2017 ([DOI 10.1145/3140587.3062381](https://dl.acm.org/doi/10.1145/3140587.3062381)). AST interpreter nodes are cloned into specialized variants by partial evaluation while the generic node remains; the compiler is itself written in the language it compiles and is specialized at run time. This is the most uncomfortable analogy for Joggle: an automatic, finer-grained version of (a)+(c).
- **Erlang code replacement** — [Erlang/OTP "Compilation and Code Loading"](https://www.erlang.org/doc/system/code_loading.html) (fetched 2026-09-17): module code exists as *current* and *old*, "both old and current code are valid, and can be evaluated concurrently"; a failing `on_load` leaves the current code in place. **That is (c) including rollback, as a 40-year-old runtime primitive.**
- **Dynamic software updating** — Hicks & Nettles, TOPLAS 27(6) 2005 ([DOI 10.1145/1108970.1108971](https://dl.acm.org/doi/10.1145/1108970.1108971)); **Kitsune**, OOPSLA 2012 ([DOI 10.1145/2384616.2384635](https://dl.acm.org/doi/10.1145/2384616.2384635)).
- **Classboxes** — Bergel, Ducasse, Nierstrasz, *Computer Languages, Systems & Structures* 31(3–4), 2005 ([DOI 10.1016/j.cl.2004.11.002](https://dl.acm.org/doi/abs/10.1016/j.cl.2004.11.002)): a scoped, *private* extension of a class's behavior that does not affect other clients — very close to "edit one decision without changing the baseline used by other experiments".
- **Adapton** — Hammer et al., PLDI 2014 ([DOI 10.1145/2594291.2594324](https://dl.acm.org/doi/abs/10.1145/2594291.2594324)); **Build Systems à la Carte** — Mokhov, Mitchell, Peyton Jones ([Hackage](https://hackage-content-origin.haskell.org/package/build-0.0.1.1), [zbMATH](https://zbmath.org/pdf/07203399.pdf)). These are the right citations for the paper's own admitted limitation that recomputation is only module-granular.

### 1.7 Agent-assisted / LLM-assisted compiler development, 2024–2026

The paper cites only Autocomp and Argus. The real field:

- **Magellan** — Chen et al., C4ML@CGO'26 ([arXiv 2601.21096](https://arxiv.org/abs/2601.21096)). An LLM coding agent plus evolutionary search **synthesizes executable C++ decision logic and integrates it into LLVM** (function inlining, register allocation) and XLA, in a closed loop of generation, macro-benchmark evaluation, and refinement; it beats decades of hand-tuned inlining heuristics. **This is Joggle's §5.1 storage-planner example, done automatically, in the compiler, today.**
- **llvm-harness / LLVM-Bench** — Zheng et al. ([arXiv 2603.20075](https://arxiv.org/abs/2603.20075)), 334 reproducible LLVM middle-end bugs plus a mini repair agent; agents already edit compiler internals.
- **LLVM-Bench** — Tian et al. ([arXiv 2607.00700](https://arxiv.org/abs/2607.00700)), 423 validated LLVM issue-resolution tasks.
- **COMPASS** — an agent for MLIR pass-pipeline generation, TASE 2025 ([DOI 10.1007/978-3-031-98208-8_13](https://dlnext.acm.org/doi/10.1007/978-3-031-98208-8_13)).
- **Agentic Auto-Scheduling** — PACT 2025 ([DOI 10.1109/PACT65351.2025.00027](https://dl.acm.org/doi/abs/10.1109/PACT65351.2025.00027), [arXiv 2511.00592](https://ar5iv.labs.arxiv.org/html/2511.00592)): LLM-guided loop optimization.
- **Agentic Code Optimization via Compiler-LLM Cooperation** ([arXiv 2604.04238](https://arxiv.org/abs/2604.04238)): multi-agent system with compiler constituents as tools.
- **LLM Compiler** — Cummins et al. ([arXiv 2407.02524](https://arxiv.org/abs/2407.02524)); **"Large Language Models for Compiler Optimization"** — Cummins et al. ([arXiv 2309.07062](https://arxiv.org/abs/2309.07062)); **"Finding Missed Code Size Optimizations in Compilers using LLMs"** — Italiano & Cummins ([arXiv 2501.00655](https://arxiv.org/abs/2501.00655)).

Coverage: (a) no, (b) **yes — by source edit + rebuild**, (c) no (the revised decision replaces the installed one and the compiler is rebuilt).

---

## 2. Verdict

**The claim is defensible only in a narrowed form, and Section 6 as written does not defend it.**

Individually, each of (a), (b), (c) has prior art measured in decades:

| Capability | Closest prior art |
|---|---|
| (a) derive a copy of a function, keep the original | Procedure cloning (PLDI'92); GCC `create_version_clone`; Truffle/Graal node specialization (PLDI'17) |
| (b) revise one internal decision | Open implementation (ICSE'97); compile-time MOPs, Open C++ (OOPSLA'95); Engler's dynamically linked compiler modules (TSE'99); Exo 2 versioned cursors; MLGO/Magellan heuristic replacement; MLIR PDL for pattern rewrites |
| (c) run the copy while the original stays live | Erlang two-version code replacement; dynamic software updating (TOPLAS'05, Kitsune OOSLA'12); Classboxes; e-graph non-destructive rewriting (eqsat/Tamagoyaki) |

What is **not** in that literature, and is the real residue: the three as **one uniform operation over the same typed IR that also holds the subject program**, with (i) **private-closure capture**, (ii) **binding-level replacement checking** of already-resolved dependents (the `ρ_E'(c) = ρ_E(c)` check in §3.3, including the additive-overload redirect case in Figure 4), (iii) **separate, verified rollback boundaries** for compiler-code edits and subject edits, and (iv) application across **all four compiler roles** (analysis, selector, transformation, generator) **with no native toolchain step at all**.

That conjunction is a genuine systems contribution. "Malleable compilation" as a *new capability* is not.

**Is it a repackaging?** As currently presented, largely yes — it reads as *open implementation + procedure cloning + hot code loading*, unified. The authors clearly know this (`plan.md`: "The novelty boundary is intentionally narrow"), but §6 does not concede the 1990s open-compiler lineage and therefore cannot draw the delta, and it omits the two most dangerous neighbours (Truffle/Graal specialization; Erlang-style code replacement).

### Strongest reviewer counter-argument (verbatim-ready)

> "Every ingredient here is long-established. Opening a compiler's implementation decisions to clients is Kiczales' open implementation and compile-time MOP programme (IMSA'92, ICSE'97, Open C++, the CLOS MOP), which already let a user override an internal decision while the default stayed callable. Deriving a specialized copy of a function while the original remains callable is procedure cloning and function versioning, standard in GCC and LLVM for thirty years, and performed automatically at far finer granularity by Truffle/Graal's partial evaluation of interpreter node bodies. Running a revised definition beside the original, with rollback if the new one fails to load, is Erlang's two-version code replacement and the dynamic-software-updating literature. MLIR's PDL already lets rewrite rules live as IR and be added without a rebuild. And automated agents already write the exact decisions this paper targets — Magellan synthesizes LLVM inlining and register-allocation heuristics in C++ and beats hand-tuning. What remains is a packaging choice: put the pass in the same IR as the program and write the pass in that IR's language. The evaluation supports this reading: the authors' own source-patch control produces the *same plan and the same artifact*, and the only measured difference is that the recipe survives four hand-constructed renamings. This is a careful, well-bounded prototype of an old idea, and the paper does not measure the cost that would justify the design."

### Evidence and framing that defeats it

1. **A capability matrix**, not prose. Columns: *body editable in-IR · copy-with-private-closure · original stays callable · runs on a separate subject · revision-time verification · rollback · dependent-binding check · native toolchain step required*. Rows: MLIR Transform, MLIR PDL, Exo 2, TVM Relax/BYOC, egg/eqsat, Truffle/Graal, Open C++/Engler-lcc, Erlang code replacement, Magellan, **and the paper's own source-patch control**. §6 currently *promises* this ("which compiler implementation body each system represents, whether a user can derive an independently callable revision of that body, and what semantic and rebuild obligations remain") and never discharges it. The source-patch row is the one that must be defeated by a measurement, not a count.
2. **Turn the four constructed refactorings into a distribution.** Script N (say 40–100) independent refactorings — rename, inline, reorder, extract-helper, insert-logging — applied mechanically at multiple sites, and report the fraction of edits that apply-and-validate for (i) a textual one-hunk patch, (ii) an MLIR/PDL pattern, (iii) a Joggle derivation. One number ("derivation survives x/N where the patch survives y/N") converts an anecdote into a mechanism result. Four hand-built cases is exactly the sample size a reviewer discounts.
3. **Present the rebuild result as a revision curve, not two anecdotes.** §5.1 already has the data points (derive 0.04 s vs 8.0 s TU+relink vs 514 s clean source build; 42.1 s / 28.3 s compile times). Plot "validated artifact" latency against number of successive revisions for Joggle vs the TVM source route, and name the regime in which the design wins: **many revisions per decision, few decisions per artifact**. Without the regime statement, "no rebuild" looks like a 9-minute constant that amortizes away.
4. **Fix the abstract's agent claim, then make it the contribution.** "…yet these compiler decisions remain inaccessible to them" is **false as written** — Magellan and llvm-harness are direct counterexamples. Replace with the true and stronger claim: agents can already *write* the decision; what they lack is a cheap, isolated, checked way to *run a candidate beside the installed decision* without rebuilding the compiler and without a partially rewritten subject. That reframing makes derivation's verification and rollback boundaries the agent-relevant feature rather than a side note.
5. **Report the negative results as the mechanism's brief.** §5.1 already reports that the derived `c.prepare` leaves latency unchanged (42.9 vs 43.1 ms) and that the same ranking rule *raises* TVM's planned storage (+1.06 %). These sit awkwardly under a capability headline. State the thesis the data supports: the mechanism's product is a *cheap, checkable experiment*, not a better policy — and then the paper owes a *cost* result (decision-change → validated artifact, and peak RSS) rather than only a capability result. `plan.md` lists this as still missing; it is the single highest-value addition.

---

## 3. Concrete changes to Related Work (Section 6)

**Must be cited (absent today):**

1. Open implementation and compile-time MOPs — Kiczales et al., ICSE 1997; Lamping/Kiczales/Rodriguez/Ruf, IMSA 1992; Chiba, OOPSLA 1995; Kiczales/des Rivières/Bobrow, AMOP 1991; Smith, LFP 1984. → new subsection or a paragraph inside §6.2. **This is the highest-priority addition.**
2. Engler, "Interface Compilation", IEEE TSE 25(3) 1999 (user modules dynamically linked into the compiler). → §6.2.
3. Procedure cloning — Cooper/Hall/Kennedy, PLDI 1992 / *Computer Languages* 19(2) 1993. → §6.1, immediately before the Exo 2 paragraph.
4. MLIR PDL/PDLL. → §6.1, next to the Transform paragraph, with the explicit statement that PDL is the closest *no-rebuild* facility and why a pattern language does not reach a decision procedure.
5. Truffle/Graal partial evaluation (PLDI 2017). → §6.1, beside AnyDSL/LMS/Weval.
6. Erlang code replacement + Hicks & Nettles DSU (TOPLAS 2005) + Kitsune (OOPSLA 2012) + Classboxes (2005). → either §6.2 or the §3.4 discussion of "original stays callable and verified".
7. Equality saturation as a *non-destructive multi-version* representation: egg (POPL 2021), SPORES (VLDB 2020), eqsat ([arXiv 2505.09363](https://arxiv.org/abs/2505.09363)), Tamagoyaki ([arXiv 2602.16707](https://arxiv.org/abs/2602.16707)). → §6.4. The paper cites Tensat but never the mechanism that most resembles "keep the original live".
8. Incremental recomputation: Adapton (PLDI 2014), Build Systems à la Carte. → one sentence in §3.4 / §6, so the substrate is not mistaken for a novelty claim.
9. Expanded agent subsection: Magellan, llvm-harness/LLVM-Bench, LLVM-Bench (2607.00700), COMPASS, Agentic Auto-Scheduling, compiler-LLM cooperation, LLM Compiler, LLMs for compiler optimization, missed-optimization LLM testing. → §6.5, which currently has two citations and no engagement with the fact that agents already edit compiler internals.
10. OpenTuner, CompilerGym, MLGO, Milepost GCC. → §6.4, as the "replace a parameter/model, not a body" contrast.

**Comparisons that must be made explicit:**

- The **capability matrix** described above. Replace the repeating "X does A but the body stays given" prose pattern with one table plus one paragraph per row that needs nuance.
- **Engler/Open C++ vs Joggle**: state that "user-supplied compiler extension without rebuilding the compiler" is *not* the claim (that is 1997 precedent); the claim is derivation *from a live typed definition, inside the same IR, with closure capture and checked bindings*.
- **Procedure cloning / Truffle vs Joggle**: state that those copies are *automatic and input-directed* and transient, whereas Joggle's copy is *user-directed, structurally addressed, and a first-class module definition that the same verifier accepts*.
- **Erlang/DSU vs Joggle**: state that replacing a live definition is *not* the claim; the claim is that the original and the derived copy are two definitions in one verified IR, with separate rollback boundaries, not one module slot being swapped.
- **§6.1's best sentence** — "The difference lies in which body is editable, not in stronger transformation guarantees" — should be promoted to the *topic sentence* of the subsection instead of buried in the Exo 2 paragraph.

**Claims that should be narrowed or corrected:**

- Abstract: "Automated agents now write much of this code, yet these compiler decisions remain inaccessible to them." → **delete/replace** (Magellan, llvm-harness). Replace with: agents can write such decisions, but integrating one means editing the compiler and rebuilding, and there is no way to run a candidate beside the installed decision under the same checker.
- Abstract: "It also survives source refactorings that break an equivalent patch" → narrow to what was measured: a *structurally addressed* derivation reproduces the same plan across four constructed refactorings where a one-hunk patch cannot be applied.
- Abstract: "so a derived procedure remains reproducible" → "reproducible" is carrying more than the evidence supports; say "so a revision is rejected before it is published rather than producing a partially rewritten subject."
- §1: "The open question is what remains reusable when a researcher must revise an algorithm inside such a system" → concede that the question is old (open implementation, 1992/1997) and that what is new is posing it over **one typed IR that also holds the subject**, with the checker and rollback boundaries an automated proposal needs.
- §1 contribution 1: add "inside one typed IR that also represents the subject, with no native toolchain step" so it is not read as procedure cloning.
- §6.2: "These precedents motivate reuse of compiler definitions across roles, beyond packaging an individual pass." → add the concession sentence that open implementation and compile-time MOPs already made implementation decisions client-controllable, and that Joggle's delta is granularity (a body, not a strategy hook) plus checking.
- Terminological note: **"malleable compilation" appears unused in the literature I searched** (only cryptographic "non-malleability" and MPI "process malleability" surface). But **"malleable software" is an established term** in the end-user-programming / local-first community ([Ink & Switch](https://www.inkandswitch.com/malleable-software/), [essay](https://www.inkandswitch.com/essay/malleable-software/)). Either cite that lineage in one clause or expect a reviewer to ask why the word was chosen.

---

## 4. Verification notes

- Fetched directly and quoted: MLIR PDL docs, Erlang code-loading docs, Magellan abstract, llvm-harness abstract, Ink & Switch malleable-software page, Stodghill's open-compiler survey.
- Everything else is cited from a search result with a resolvable URL (DOI or arXiv). I did **not** fetch every paper in full.
- **Unverified / do not cite without checking:** Milepost GCC's DOI (two conflicting DOIs surfaced: `10.1007/s10766-010-0137-2` and `10.1007/s10766-010-0161-2`); the exact venue and pages of Sarkar/Waddell/Dybvig's "Staged compilation" (existence indicated by [zbMATH](https://zbmath.org/pdf/02213556.pdf), venue unconfirmed — I did not cite it above); the canonical `llvm.org/docs/MLGO.html` path (I only confirmed a `releases-origin.llvm.org` mirror).
- I found **no** prior work that proposes exactly Joggle's conjunction, and **no** prior use of the phrase "malleable compilation". That is a bounded-search result across arXiv and web search, not an exhaustive novelty claim.
