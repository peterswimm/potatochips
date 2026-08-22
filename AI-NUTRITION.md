# AI Nutrition Label

## Purpose

This label records known AI involvement in repository changes. It is an
attribution and review record, not a claim that AI systems own copyright or a
claim about their training data. Unrecorded historical AI involvement is marked
unknown rather than inferred.

## Current label

| Field | Value |
| --- | --- |
| Label version | 1.0.0 |
| Last updated | 2026-08-22 |
| Human steward | Peter Swimm |
| Disclosure unit | Revision and path scope |
| Release rule | Human maintainer approval is required for every release. |
| Machine-readable record | `ai-nutrition.json` |

## Known assisted work

| Scope | System | Role | Human accountability | Evidence | Review state |
| --- | --- | --- | --- | --- | --- |
| disting NT port, host harness, and CI | Claude Opus 5 | Implementation and documentation assistance | Peter Swimm | Commits `bd468f76` through `45ce1319` include co-author and session trailers. | Merged; build evidence is recorded in Git history and CI. |
| BLIP impulse-table safety corrections and strict sanitizer gate | Codex (GPT-5) | Diagnosis, implementation, and test execution assistance | Peter Swimm | Working-tree changes in `src/dsp/blip_buffer.hpp` and `distingNT/test/Makefile`; test commands are recorded in the change discussion. | Automated validation passed; pending maintainer release approval. |
| Spelwork provenance and publication metadata | Codex (GPT-5) | Documentation and workflow implementation assistance | Peter Swimm | Working-tree provenance files and workflow changes. | Pending maintainer release approval. |

## Inputs and data handling

- The original source repository and its public Git history are the primary
  inputs for the recorded work.
- This label does not assert what training data any model used.
- Do not submit secrets, private patches, proprietary audio, personal data, or
  unpublished vulnerability details to an AI system without explicit consent.
- Historical Claude sessions are referenced only where their commit trailers
  already record them. The label does not expose additional private prompts.

## Quality and limitations

AI-assisted changes can contain errors, omissions, or license misunderstandings.
Every proposed change requires human maintainer review, license-boundary review,
and the relevant automated checks. The Disting NT host harness tests memory and
numeric safety, but hardware listening and control validation remain a separate
release requirement.
