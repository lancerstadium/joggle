# EuroSys submission status

This directory is a reproducible view of the evidence manuscript, not a claim
that the paper is submission-ready. The checklist keeps formatting progress
separate from scientific progress.

## Closed

- [x] Anonymous two-column review layout.
- [x] Mechanical Markdown-to-LaTeX rendering with unsupported-input failure.
- [x] Eight-page build: seven technical pages and one reference page.
- [x] All four current tables fit without clipping or unreadable text.
- [x] Bibliography resolves without undefined citations.

## Submission blockers

- [ ] Replace shared-runner timings with controlled, repeatable Linux results.
- [ ] Report external compiler baselines on matching models and hardware.
- [ ] Add an architecture figure that makes the progressive function IR and
      extension boundary concrete.
- [ ] Freeze the title, abstract, research questions, and three contributions
      after the evidence tables stop changing.
- [ ] Expand and audit related work; every comparison claim needs a primary
      source and a verified bibliography entry.
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
