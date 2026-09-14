# EuroSys submission status

This directory is a reproducible view of the evidence manuscript, not a claim
that the paper is submission-ready. The checklist keeps formatting progress
separate from scientific progress.

## Closed

- [x] Anonymous two-column review layout.
- [x] Mechanical Markdown-to-LaTeX rendering with unsupported-input failure.
- [x] Fresh seven-page argument draft builds within the 12-page technical-content
      limit; evidence tables and figures have not yet been inserted.
- [ ] Rebuild the citation surface from the verified literature ledger; the
      clean-slate manuscript deliberately removed the old citation catalogue.
- [x] Literature map states the strongest competing explanations from MLIR,
      Relax, Exo, ACT/ATLAAS, Ladder, Mirage, and Axon and derives falsifiable
      experiments from them.
- [x] Deterministic operator corpus covers 441 cases: nine pointwise/
      normalization rows and eight contraction/Transformer rows.
- [x] XCiT-Tiny infers all 1,333 initially unknown results; its seven-call
      conversion frontier is preserved rather than reported as end-to-end
      support.

## Submission blockers

- [ ] Replace shared-runner timings with controlled, repeatable Linux results.
- [ ] Complete the pre-registered evolution-continuity experiment against the
      documented native paths in TVM/Relax and ONNX-MLIR/MLIR.
- [ ] Demonstrate chooser substitution and one generic target policy across
      several functions and at least two complete models.
- [ ] Report external compiler baselines on matching models and hardware.
- [ ] Add an architecture figure that makes the progressive function IR and
      extension boundary concrete.
- [ ] Freeze the title, abstract, research questions, and three contributions
      after the evidence tables stop changing.
- [ ] Use at least 50 relevant verified works and run
      an independent claim-to-source audit; every comparison claim needs a
      primary source and a verified bibliography entry.
- [ ] Add artifact, AI-use, conflicts, ethics, and reproducibility disclosures
      required by the venue.
- [ ] Run a strict systems-paper review, revise, then repeat the review once.
- [ ] Build and test the anonymized artifact from a clean machine or image.

## Engineering work allowed before submission

Only refactors that unblock measurement, correctness, or artifact review are
on the critical path. Broad parser/printer/verifier decomposition is deferred
until the submission evidence is frozen; targeted extraction is appropriate
when a touched subsystem cannot be safely tested or reviewed in its current
form.
