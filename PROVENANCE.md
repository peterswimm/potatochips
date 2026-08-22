# Provenance

## Scope

This repository is a public, maintained derivative of
[Kautenja/PotatoChips](https://github.com/Kautenja/PotatoChips). It preserves
the upstream Git history and records the source, author, license, and review
status of material changes. A fork is not a transfer of upstream ownership.

## Lineage

| Layer | Source | License | Stewardship |
| --- | --- | --- | --- |
| Core Rack source and DSP | Kautenja/PotatoChips | GPL-3.0-or-later | Upstream authors retain copyright; this fork maintains derivative changes. |
| Blargg-derived DSP portions | Identified by upstream source notices | LGPL-2.1 as identified in `LICENSE.md` | Preserve notices and compatible distribution terms. |
| disting NT API | `dep/distingNT_API` submodule | MIT | Expert Sleepers Ltd.; pinned by Git submodule. |
| Catch2 test framework | `dep/Catch2` submodule | Boost Software License 1.0 | Catch2 contributors; test-only dependency. |
| Panel/manual/logo artwork | `res/`, `manual/` | CC BY-NC-ND 4.0 | Upstream controlled; outside the FOSS claim. |
| Spelwork governance records | `.spelwork/`, root provenance files, GitHub workflows | GPL-3.0-or-later unless noted | Peter Swimm. |

## Material fork work

| Change | Evidence | Status |
| --- | --- | --- |
| Native disting NT algorithms, build layer, and host harness | `distingNT/`; commits `bd468f76` through `45ce1319` | Maintained by this fork. |
| Continuous integration for DSP, Rack, and disting NT builds | `.github/workflows/build.yml`; commit `45ce1319` | Maintained by this fork. |
| Spelwork public-provenance layer | `.spelwork/project.yaml`, `NOTICE.md`, `AI-NUTRITION.md`, `sbom.spdx.json` | Maintained by this fork. |
| BLIP impulse-table safety corrections | `src/dsp/blip_buffer.hpp`; see `AI-NUTRITION.md` | Pending human maintainer release approval. |

## FOSS boundary

The source code and new Spelwork governance materials are published as free
software/documentation under their stated licenses. The repository also carries
upstream non-free visual assets. Therefore the accurate public statement is:

> This is a GPL-licensed codebase with inherited CC BY-NC-ND artwork. The code
> and newly labeled Spelwork materials are FOSS; the complete asset bundle is
> not wholly FOSS.

Replacing the artwork with newly commissioned or independently created
permissively licensed assets is required before describing the complete project
as FOSS.

## Release evidence

Each release must include the commit SHA, Disting NT object checksums, this
component inventory, an AI nutrition label revision, and a GitHub build
attestation. See `RELEASE.md`.
