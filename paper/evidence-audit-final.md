# Final evidence-discipline audit of `paper/submission/main.tex`

Auditor: delegated subagent. Method: extract every quantity from `main.tex`, classify as
measured vs design, then recompute each measured figure with `python3` directly from the
CSV/JSON records (never from the paper's render scripts). The MLIR probe was re-executed
against the built `mlir-opt`; the UltraFace convolution-nest count was recomputed from the
retained IR pair; the module/test/LOC counts were recounted from the tree.

Recomputation scripts were run inline; every number below was produced by reading the named
record with `python3 csv/json`.

---

## 1. Verdict table — measured claims

Verdicts: **MATCH** = record reproduces the stated figure at the stated precision;
**MISMATCH** = record gives a different specific figure; **NO RECORD** = no machine-readable
record located; **OVERSTATED** = directionally right but the phrasing claims more than the record.

| # | Claim (quoted, line) | Record file | Recomputed value | Verdict | Notes |
|---|---|---|---|---|---|
| 1 | "by `$1.36\times$` to `$3.24\times$`" (L31, abstract) | `data/locality-matrix.json` (`summary[*].speedup`) | min 1.3654, max 3.2378 | **MISMATCH** | 1.3654 rounds to **1.37**, not 1.36; body L904 says 1.37. Abstract and body disagree. |
| 2 | "extension tasks across three systems, sixteen pinned models of which eleven produce validated artifacts, a five-system runtime comparison under one job" (L125–127) | `data/extension-*-pilot.csv`; `data/model-coverage-pilot.csv`; `data/locality-matrix.csv` | 3 systems; 16 rows; 11 `execute=pass`; 5 variants | MATCH | — |
| 3 | "about 13{,}800 lines" (L575) | tree `src/` + `include/` + `tool/` | 13,837 | MATCH | No record file; recounted directly (12,197 + 520 + 1,120). |
| 4 | "Twenty modules … Five more under `extensions/` … default suite runs 68 tests" (L586–589) | `modules/`, `extensions/`, `build/CTestTestfile.cmake` | 20 / 5 / 68 | MATCH | `ctest -N` → "Total Tests: 68". |
| 5 | "across two calls per variant … the 8,840 scores and 17,680 box coordinates have a maximum absolute error of `$3.58\times10^{-7}$` … below the `$10^{-5}$` threshold" (L697–699) | `experiments/reuse/result.json` | `calls_per_variant=2`; counts 8840/17680; max err 3.5762786865234375e-07; `absolute_tolerance=1e-05` | MATCH | Exact. |
| 6 | "6,837,984 to 6,530,808 bytes (4.49\%), including a 24-byte increase in integer slots" (L699–701) | `experiments/reuse/result.json` | 6837984→6530808; 4.492201 %; int64 64→88 B | MATCH | — |
| 7 | "all five validate under both planners, the emitted C differs on one model by 20 bytes … peak RSS moves by at most two percent either way" (L704–707) | `data/planner-artifact-same-host.csv` | 5 models × 2 planners all `validated=True`; ultraface 218047→218067 (20 B); max |ΔRSS| 589824/30736384 = **1.919 %** | MATCH | — |
| 8 | "four constructed refactorings … only one of them separates the routes cleanly… Renaming nine local bindings together" (L716–728) | `experiments/reuse/control.json`, `experiments/reuse/compare.py` | 4 cases; `helper_rename` tie; `operand_rename` patch applies / check fails; `binding_rename` patch fails, derive+oracle ok; `guard_change` both fail; `compare.py` renames **9** names | MATCH | — |
| 9 | "Five decision mutations, including disjunction and altered capacity writeback, are rejected before a derived module is serialized" (L731–732) | `experiments/reuse/check.cmake` (`reject_change` × 5) | exactly 5: `<0`→`<=0`, `&&`→`\|\|`, strict→non-strict lifetime, `slot`→`slot+1`, `counts[item]`→`counts[item]+1` | MATCH | Record is a CTest **script**, not a captured run log. |
| 10 | "deriving the recipe costs 0.04 s and the derived planner 2.48 s … 8.0 s and 7.7 s on revert, after a 514 s clean build" (L745–747) | `experiments/reuse/external/summary.json`, `rebuild_cycle.log`, `build-initial.log` | 0.040887; 2.478623; 8.034092; 7.653783; 514 | MATCH | Also `compiled_units=1`, `relink=true`; TVM plan bytes 5634560→5694400 (raises) vs Joggle 6837984→6530808 (lowers). |
| 11 | "20 functions and 1,115 operations and reproduced output on four complete models" (L764) | `experiments/derive-prepare.md` (prose table) | 20 / 1,115 / 4 models | MATCH | **NO machine-readable record** — the runner's output was never captured as CSV/JSON. |
| 12 | "A 70-line recipe" (L770) | `experiments/guard/module.jog` | 70 non-comment, non-blank lines (89 total) | MATCH | — |
| 13 | "removes 18 provably true padding guards and simplifies five more … Median latency is unchanged (42.9 against 43.1 ms over 20 balanced processes)" (L773–776) | `experiments/guard/result.json` | if_count 106→88; reading "18 … and 5 simplified"; medians 42.9052085 / 43.089125; n=20; `matches_manual_probe=true` | MATCH | — |
| 14 | "sixteen models, fifteen from the ONNX Model Zoo and one TFLite model. All sixteen decode and convert, and the fifteen ONNX models also infer" (L826–828) | `data/model-coverage-pilot.csv` | 16 rows (15 ONNX + 1 TFLite); `decode=pass` ×16; `convert=pass` ×16; `infer=pass` ×15, TFLite `n/a` | MATCH | — |
| 15 | "every ONNX Zoo case reaches a zero unknown-result frontier with no source-format call left" (L829–831) | `data/model-frontier-pilot.csv` | 15/15 rows `unknown_after=0`, `source_calls_after=0`, all revision `27b37c75…` | MATCH | — |
| 16 | "the cases that earlier stopped at 280, 6790, and 1333 unknown results" (L831) | `data/model-frontier-pilot.csv` | tiny-yolov3-11 280; ssd-mobilenetv1-12 6790; xcit 1333 | MATCH | — |
| 17 | "Eleven also have executed artifacts that pass a stored reference" + the 11-model list (L831–834) | `data/model-coverage-pilot.csv` | exactly 11 `execute=pass` rows; list matches | MATCH | — |
| 18 | "the TFLite model, which agrees with LiteRT to `$1.0\times10^{-6}$`" (L834) | `data/model-coverage-pilot.csv` | 1.013279e-06 | MATCH | — |
| 19 | "Three models stop at conversion … XCiT-Tiny exceeds its recorded preparation timeout" (L835–836) | `data/model-coverage-pilot.csv` | 4 rows `expose=not_run`: tiny-yolov3, ssd-mobilenetv1, efficientnet-qdq, xcit | MATCH | — |
| 20 | "preparation completes in 1{,}036 s … `$4.46\times10^{-3}$` with 130 elements outside tolerance … Both variants report the same error" (L837–840) | `data/model-coverage-pilot.csv` (note + `max_abs_error`) | 4.461586e-03; note: "prepare 1036 s, both variants emit and compile"; "130 elements outside the 1e-4 tolerance"; "Both variants report the same error" | MATCH | — |
| 21 | "54 external Conv calls alongside 36 semantic ReLU calls … prints 102 loops" (L847–849) | `experiments/progressive-trace.md` (raw tool-output block) | `semantic_convs_before=54`, `external_convs_selected=54`, `external_convs_prepared=54`; `semantic_relus_before=36`; `loops_prepared=102`; `main_same_after_prepare=true` | MATCH | Record is a Markdown transcript with a fenced tool-output block, **not** CSV/JSON. |
| 22 | "Joggle's first-fit artifact is 0.84× to 1.00× of TVM's … on six models" (L868–869) | `data/locality-matrix.csv` | six-model Joggle/TVM ratio of medians spans 0.8442 → **0.9948** | **MISMATCH** | 0.9948 rounds to **0.99**; "1.00×" is rounded up. Everything else in the sentence is exact. |
| 23 | "2.7\% and 14\% slower on ShuffleNetV2 and GoogLeNet … MNIST, where TVM is 3.0× faster" (L869–871) | `data/locality-matrix.csv` | 1.0275 → +2.75 %; 1.1404 → +14.04 %; MNIST 3.0446 | MATCH | Uses ratio of medians (same estimator as the 0.84–0.99 span). |
| 24 | "all 52 convolution nests of that subject, costs 2.2 s" (L879) | `build-study/locality-matrix/ultraface-rfb-320/{prepared.jog,prepared-locality.jog,build.json}` | **52** seven-axis nests in both files; every one changed `n,m,oh,ow,q,r,s`→`n,m,oh,q,r,s,ow`; `locality.apply` = 2.2246 s | MATCH | No record under `paper/data/`. `experiments/locality/README.md` L95 says **18** nests — the paper is right, the study README is stale. |
| 25 | "1.49× faster than the ordinary one" (L884) | `data/locality-matrix.json` | ultraface `speedup` = 1.48982 | MATCH | ratio of medians. |
| 26 | "1.50× faster than TVM's unscheduled lowering" (L885) | `data/locality-matrix.json` / `locality-matrix-paired.json` | ratio of medians 46.9989375/31.383063 = **1.4976**; median of paired ratios = **1.4142** | **MISMATCH (internal)** | Same record, same model, same pair as L907's range floor. L907 states the floor as **1.41**, L885 as **1.50**. |
| 27 | "ONNX-MLIR … beats … on six of the nine models … by up to 3.83×, and loses on the other three by up to 1.27×" (L887–890) | `data/locality-matrix.json` | ONNX-MLIR faster on 6 (densenet 1.18, mnist 1.62, mobilenetv2 3.36, shufflenet 3.83, squeezenet1.1 1.15, ultraface 2.02); Joggle faster on 3 (googlenet 1.08, resnet18 1.27, tinyyolov2 1.16) | MATCH | — |
| 28 | "ONNX Runtime stays 7.7× to 85.9× ahead on every model" (L890) | `data/locality-matrix.json` | 7.676 (mnist) → 85.906 (tinyyolov2) | MATCH | — |
| 29 | "Median absolute deviations … 4.09, 2.63, 3.74, 2.61, and 0.40 ms for ordinary Joggle, policy Joggle, TVM, ONNX-MLIR, and ONNX Runtime" (L896–898) | `data/locality-matrix.json` | base 4.092958; locality 2.625396; tvm 3.744729; onnxmlir 2.606646; ort 0.402541 | MATCH (values) | These are the **ultraface-rfb-320 row only**; no other model reproduces any of the five. The sentence names no model, so a reader will read them as campaign-wide. |
| 30 | "ordinary run time spans 1.6 ms to 2.3 s" (L903) | `data/locality-matrix.csv` | base medians 1.647 ms (mnist) → 2312.418 ms (tinyyolov2) | MATCH | — |
| 31 | "shortens run time on every one of them, by `$1.37\times$` to `$3.24\times$` with a median of `$2.04\times$`" (L903–904) | `data/locality-matrix.json` `speedup`; vs `locality-matrix-paired.json` | ratio-of-medians 1.3654–3.2378, median **2.0420**; median-of-paired-ratios 1.4412–3.2240, median **2.1409** | MATCH to `locality-matrix.json`, **MIXED ESTIMATOR** | The same sentence's next clause cites the *paired* bootstrap interval. Paired-consistent wording would be "1.44× to 3.22×, median 2.14×". |
| 32 | "every model's paired bootstrap interval excludes one" (L905) | `data/locality-matrix-paired.json` | all 10 `ci95` lower bounds > 1 (min 1.2768) | MATCH | — |
| 33 | "measured on nine models, the same policy is faster on eight, by `$1.41\times$` to `$2.82\times$`" (L906–907) | `data/locality-matrix-paired.json` | TVM measured on 9; 8 "locality faster"; paired medians 1.4142 (ultraface) → 2.8154 (mobilenetv2) | MATCH | Uses the **paired** estimator — inconsistent with #25/#22/#23 which use the ratio of medians. |
| 34 | "MNIST is a tie at `$1.01\times$`, interval `$[0.94, 1.06]$`" (L907–908) | `data/locality-matrix-paired.json` | 1.010987, ci95 [0.941684, 1.056689] | MATCH | — |
| 35 | "stays inside the study's `$10^{-4}$` tolerance … largest observed error being `$1.87\times10^{-5}$`" (L908–910) | `data/locality-matrix.csv`, `locality-matrix.json` (`atol`) | 1.871586e-05 (tvm, mobilenetv2 trial 0) | MATCH | The one non-admitted variant (TVM on squeezenet1.0-13-qdq, 2.0659e-02, 30/1000 outside) is correctly dropped and documented in the JSON `failures`. |
| 36 | "rewrites 54 affine-proved MobileNetV2 bodies and reduces the generated-C median by about a quarter in two independent runner cohorts" (L929–930) | `data/mobilenetv2-policy/{variants.csv,plain.csv,canon.csv,replication/*}` | `loops_changed=54`; 139.679→102.638 ms (−26.52 %); replication 178.796→131.557 ms (−26.42 %) | MATCH | A third cohort (`artifact-5dac130/`) gives −26.58 %; "two" is the count of separately dispatched cohorts. |
| 37 | "reduces loops from 156 to 110 and C source from 192,209 to 188,244 bytes, yet the median rises from 356.360 to 384.927 ms. MADs are 1.009 and 0.531 ms, and the adjacent runtime controls are 10.359 and 10.447 ms" (L934–937) | `data/mobilenetv2-fusion-linux-variants-pilot.csv`, `…-unfused-pilot.csv/json`, `…-fused-pilot.csv/json` | loops 156→110; 192209→188244; joggle 356.360/384.927; MAD 1.009/0.531; ort 10.359/10.447 | MATCH | — |
| 38 | "takes 225.667 ms against LiteRT's 6.424 ms" (L937–938) | `data/tflite-linux/tflite-mobilenetv2.csv` | joggle 225.667 ms; litert 6.424 ms (n=20 each) | MATCH | — |
| 39 | "source-function calls fall from 2,939,981 to 1,272,463 while 10,008 edits and the 28.6-MB output remain unchanged; wall-time medians are 42.099 and 28.341 seconds" (L946–948) | `data/revision-memo-pilot.csv` | 2939981→1272463; `edits=10008`; 28,565,283 B = 28.565 MB; medians 42.098738 / 28.341025 | MATCH | — |
| 40 | "preserves all 597 edits, with one cleanup invocation falling from 244.26 to 11.28 seconds" (L948–950) | `data/cse-index-pilot.csv` | `edits=597`; wall 244.26 → 11.28 | MATCH | The record names the command `opt.basic`; the paper's paraphrase "cleanup invocation" is not the record's term. |
| 41 | "Joggle takes 101.5 s against 4.52 s for TVM and 4.51 s for ONNX-MLIR over five runs each, so its compile time is 22.5× theirs" (L962–964) | `data/compile-time-same-host.csv`, `.json` | medians 101.4626 / 4.5159 / 4.5098; ratios 22.468 and 22.498 | MATCH | `runs=5`, `threads=1`, one model. |
| 42 | "55.7 s is the interpreted `c.prepare` pass, 45.0 s the plan, place, emit, and compile chain, and 0.8 s reading and converting the model" (L965–966) | none | README prose: 0.07 + 0.69 + 55.68 + 45.0 = 101.44 s; retained per-stage JSON for this model sums to ≈71 s | **NO RECORD** | `measure_compile.py` writes only the span total; the 5 run directories were not retained. Requested explicitly by the auditor; the breakdown is unauditable. |
| 43 | "fourteen patterns compile to interpreter bytecode under `mlir-opt`" (L993–995) | `data/mlir-route-probe.json` | `patterns_compiled: 14`; **re-ran**: 14 `pdl.pattern` ops in `ops.mlir`, 14 `pdl_interp.create_operation` in output, exit 0 | MATCH | Re-executed against `/Users/lancer/Documents/Item/joggle-study/llvm-project/build/bin/mlir-opt`. |
| 44 | "exposes seventeen options" (L997) | `data/mlir-route-probe.json` | `option_count: 17`; list length 17; `Passes.td` `OneShotBufferizePass` `let options` = 17 | MATCH | — |
| 45 | "against LLVM 23.0.0git" (L993) | `data/mlir-route-probe.json` | `"version": "LLVM version 23.0.0git"`; binary prints the same | MATCH | — |
| 46 | "all booleans, enums, or scalars" (L997–998) | `data/mlir-route-probe.json`; `mlir/…/Passes.td` | 10 bool, 2 enum, 3 scalar, **2 `ListOption<std::string>`** (`--dialect-filter`, `--no-analysis-func-filter`) | **OVERSTATED** | Substantive point (no ranking procedure) holds; the type enumeration does not. |
| 47 | Table 1 (`tab:scope`) Joggle/TVM/ONNX-MLIR file and obligation counts (L814–820) | `data/extension-footprint-pilot.csv`, `data/extension-tvm-pilot.csv`, `data/extension-onnx-mlir-pilot.csv` | Joggle 1/dep3, 1/dep4, 1/dep4, 4+mod3+dep4; TVM 1, 1, 3 files, numeric-format unsupported; ONNX-MLIR 6+build3+reg1, 7+build3+reg1, external-kernel unsupported, numeric-format unsupported | MATCH | Every cell maps to a column. |

## 2. Table 2 (`tab:capability`) cell-by-cell

The table at L1028–1042 contains **no `\cite` at all** and the caption (L1019–1024) says only
"Entries come from the cited documentation of each system". No cell can be traced to a source
by a reader. Per-cell assessment:

| Row / column | Cell | Verdict | Basis |
|---|---|---|---|
| MLIR Transform or PDL — runs beside | `---` | **NOT PROBED** | `mlir-route-probe.json` answers only "which route changes a buffer-assignment ranking without rebuilding, and what can it express". It never tests running a revised pattern beside an installed one. The caption's "the MLIR row was probed on LLVM 23.0.0git here" over-claims. Needs `mlirTransformDocs`/`mlirPdl`. |
| MLIR Transform or PDL — checked at revision | `---` | **NOT PROBED / ambiguous** | Same; also MLIR runs a verifier after every pass (`--verify-each`, default on), so "no check at revision" is only true under an unstated definition ("check the *revision of the algorithm*"). |
| MLIR Transform or PDL — no rebuild | `partial` | SUPPORTED by probe | Probe: PDL route "with no C++ and no rebuild"; the bufferization route needs a C++ pass edit. "partial" is exactly that split. |
| Exo 2: schedule — runs beside | `partial` | NEEDS PAPER | Exo 2 abstract/paper ("Cursors") reports versioned scheduling procedures; "partial" is plausible but is the paper's reading, not the cited abstract's statement. |
| Exo 2: schedule — checked at revision | `---` | NEEDS PAPER, arguably wrong | Exo 2's stated selling point is *safe* user-defined scheduling operations; the compiler checks them. Under any reading other than "validates the revision of the algorithm", this cell is contestable. |
| Exo 2: schedule — no rebuild | `yes` | SUPPORTED | Abstract: users "define new scheduling operations externally to the compiler". |
| Truffle: node specialization — runs beside / checked / no rebuild | `yes / --- / yes` | NEEDS PAPER (checked cell) | Truffle's rewriting-with-fallback is standard, but Truffle *does* validate rewrites with assumptions/deoptimization; `---` needs the cited paper and a stated definition of the column. |
| Erlang: module version — runs beside | `yes` | **VERIFIED against the cited source** | Erlang docs: "The code of a module can exist in two variants … current and old … Both old and current code are valid, and can be evaluated concurrently." |
| Erlang — checked at revision / no rebuild | `--- / yes` | SUPPORTED | No revision-time binding check; no toolchain rebuild. |
| Open implementation — runs beside / checked / no rebuild | `yes / --- / partial` | NEEDS PAPERS | `lamping1992open`, `kiczales1997open`, `chiba1995mop`; the "no rebuild: partial" cell is a judgement about MOP recompilation that no cited source states in these terms. |
| Magellan: pass in C++ — no rebuild | `---` | **VERIFIED against the cited source** | Abstract: heuristics "integrate directly into existing compilers"; pass is synthesised as executable C++. |
| Magellan — runs beside / checked | `--- / ---` | NEEDS PAPER | Abstract reports generation, benchmark evaluation, refinement — consistent with `---`, but not stated as absence. |
| Source patch: planner — runs beside / checked / no rebuild | `--- / yes / yes` | SUPPORTED by `experiments/reuse/control.json` | `operand_rename`: patch applies (`patch_status=0`) but load-time check fails (`check_status=1`, "unknown name 'counts'"). |
| Joggle row | `yes / yes / yes` | SUPPORTED | `result.json`, `control.json`, `guard/result.json`. |

## 3. Problems, most serious first, with the exact edit

1. **`main.tex:885` contradicts `main.tex:907`.** "1.50× faster than TVM's unscheduled
   lowering" vs the range floor "1.41×" — the same model (UltraFace), the same two systems,
   the same record. Both are computable (`data/locality-matrix.json` ratio of medians =
   1.4976; `data/locality-matrix-paired.json` median of paired ratios = 1.4142) but the paper
   quotes each once. **Change L885** `1.50$\times$` → `1.41$\times$` **and L884** `1.49$\times$`
   → `1.47$\times$`, or else change L907 `1.41$\times$ to 2.82$\times$` → `1.50$\times$ to
   2.85$\times$` and L908's tie `1.01$\times$` → `1.06$\times$` with interval `[0.94, 1.06]`
   retained. Pick one estimator for the whole paper; the paired one is the one the sentence
   already invokes ("paired bootstrap interval").

2. **`main.tex:31` (abstract) says 1.36× where the body says 1.37× and the record says 1.37×.**
   `data/locality-matrix.json` `speedup` minimum is 1.3654. **Change L31** `$1.36\times$` →
   `$1.37\times$`.

3. **`main.tex:903–904` mixes two estimators inside one sentence.** The range and median
   (1.37–3.24, 2.04) are ratios of medians (`locality-matrix.json`); the next clause quotes
   *paired* bootstrap intervals (`locality-matrix-paired.json`), whose own range and median are
   1.44–3.22 and 2.14. **Change L904** to `by $1.44\times$ to $3.22\times$ with a median of
   $2.14\times$` if the paired estimator is chosen (consistent with #1); otherwise drop the
   paired clause from that sentence.

4. **`main.tex:965–966`: the compile-time breakdown 55.7 / 45.0 / 0.8 s has no record.**
   `data/compile-time-same-host.csv` stores only `seconds` per run; `measure_compile.py` never
   emits per-stage figures; the five run directories were not retained. The only per-stage
   records that survive for this model (`build-study/.../prepare.json`, `build.json`) total
   ≈71 s, not 101.5 s, so the breakdown cannot be reconciled from anything on disk. **Either**
   add `stage_seconds` to `compile-time-same-host.csv` and regenerate, **or** delete the
   breakdown at L965–966 and state only the span total.

5. **Table 2 (`main.tex:1016–1043`): no citations, and two MLIR cells are not probed.**
   **Change the caption at L1022–1023** from "the MLIR row was probed on LLVM 23.0.0git here"
   to "the MLIR *no-rebuild* cell was probed on LLVM 23.0.0git here", and attach a `\cite` to
   each row (Exo 2, Truffle, Erlang, open implementation, Magellan, MLIR). The Exo 2 row's
   `checked at revision: ---` is the cell most likely to be disputed and should be
   re-derived from `ikarashi2024exo2` or softened.

6. **`main.tex:896–898`: five MADs presented as campaign-wide are the UltraFace row only.**
   Every one of 4.09 / 2.63 / 3.74 / 2.61 / 0.40 ms is `locality-matrix.json` →
   `ultraface-rfb-320`. **Change L896** to "On UltraFace, median absolute deviations for these
   processes are …", or report a per-model spread.

7. **`main.tex:997–998`: "seventeen options, all booleans, enums, or scalars" is overstated.**
   Two of the seventeen (`--dialect-filter`, `--no-analysis-func-filter`) are string *lists*
   (`ListOption<std::string>`) in `Passes.mlir`/`Passes.td` and in the probe's own JSON list.
   **Change L997–998** to "all booleans, enums, scalars, or string filters".

8. **`main.tex:868–869`: "0.84× to 1.00×" — the six-model maximum is 0.9948.** **Change
   L869** `1.00$\times$` → `0.99$\times$` (or state "at parity on UltraFace, 0.99×").

9. **Claims resting on prose-only records** (acceptable today, but not machine-readable):
   L764 (20 functions / 1,115 ops / four models), L847–849 (54/36/102), L879 (52 nests),
   L731–732 (five mutations; the record is test source, not a run log). If the venue asks for
   artifact-backed numbers, capture these as CSV/JSON.

10. **Consistency hazard outside the paper.** `experiments/locality/README.md:95` says the
    UltraFace policy "moves the output-width axis innermost in **18** convolution nests"; the
    paper says 52 and the retained IR pair confirms 52 (52 seven-axis nests, all reordered).
    Correct the study README so a reviewer comparing the two does not conclude the paper
    overstated by 3×.

## 4. What was re-executed rather than read

- `mlir-opt -split-input-file --convert-pdl-to-pdl-interp …/PDL/ops.mlir` → exit 0,
  14 pattern ops in / 14 interpreter ops out; `mlir-opt --version` → LLVM 23.0.0git;
  `OneShotBufferizePass` option list recounted from `Passes.td` → 17.
- UltraFace nest count parsed from `prepared.jog` / `prepared-locality.jog` (52 / 52, all
  changed).
- `ctest -N` in `build/` → 68 tests; `find src include tool` → 13,837 lines; `modules/` → 20;
  `extensions/` → 5.
- All locality, compile-time, planner-artifact, coverage, fusion, memo, CSE, guard, memo and
  policy statistics recomputed from the CSVs/JSONs with `csv`/`json` + `statistics`.
