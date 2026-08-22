# Release Process

1. Confirm the working tree is intentional and `make publication-check` passes.
2. Run the CI-equivalent DSP, Rack, and disting NT checks. Before publication,
   the required GitHub Actions checks must be green; the Rack plug-in link check
   runs on Linux and is authoritative when it cannot be reproduced locally.
3. Run the Disting NT algorithms on hardware and record the tested firmware,
   algorithms, audio behavior, and control/CV behavior in the release notes.
4. Update `PROVENANCE.md`, `AI-NUTRITION.md`, `ai-nutrition.json`,
   `THIRD_PARTY.md`, and `sbom.spdx.json` for the release.
5. Create a signed tag and GitHub Release. The release workflow uploads the
   Disting NT objects, their SHA-256 checksums, and a GitHub build attestation.
6. The human steward approves publication. Automation records evidence; it does
   not authorize a release.

The first release that includes the currently pending AI-assisted changes must
state the human review decision in its release notes.
