# Third-Party Components

This table is the human-readable counterpart to `sbom.spdx.json`. It is not a
substitute for the license texts and notices carried with each component.

| Component | Location | License | Source and notes |
| --- | --- | --- | --- |
| Potato Chips upstream source | `src/`, tests, build files | GPL-3.0-or-later | Christian Kauten and contributors; forked from `Kautenja/PotatoChips`. |
| Blargg audio-library material | Identified within `src/dsp/` | LGPL-2.1 as identified by upstream | Preserve file notices and the upstream attribution in `LICENSE.md`. |
| disting NT API | `dep/distingNT_API` | MIT | Expert Sleepers Ltd.; Git submodule pinned in `.gitmodules`. |
| Catch2 | `dep/Catch2` | Boost Software License 1.0 | Test-only Git submodule. |
| Panel/manual/logo assets | `res/`, `manual/` | CC BY-NC-ND 4.0 | Christian Kauten; not included in the project's FOSS claim. |

When adding a dependency, update this table, `sbom.spdx.json`, and the relevant
license notice before release.
