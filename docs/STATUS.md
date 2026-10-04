# OpenArc implementation status

Updated 3 October 2026. This file records evidence, not inferred completion.

## Current checkpoint

M0 is in progress. Repository tooling, community files, license, and the initial
source patch series have been created. The pinned Chromium source and dependencies
have been fetched and their setup hooks completed. Dependency-inventory and
depot_tools bootstrap setup issues were corrected; GN generation passed and the
unmodified Chromium baseline is compiling. No browser binary has been run yet.

## Pinned baseline

- Chromium: `154.0.8037.98`, commit `b859317bf11f6be47f9b7799ec690a0a42a1fb33`.
- depot_tools: `8a5434051036b32412a2ecb10c213a72e3f3ccb9`.
- Target: Apple Silicon macOS, local component development build.
- Lock source: Google's macOS ARM64 stable Version History API and the official
  Chromium release tag, resolved on 3 October 2026.
- Host preflight: ARM64, 64 GiB RAM, APFS, Xcode 26.3, macOS SDK 26.2.
- The pin's minimum SDK is 15; its official reference SDK is 26.5. The local SDK
  meets the configured minimum, but compilation remains the compatibility check.

The exact inputs are in [upstream.lock](../upstream.lock). The upstream choice
does not promise this revision remains current after this checkpoint.

## Evidence ledger

| Check | Result |
| --- | --- |
| Official release tag and exact commit resolution | Verified |
| Host/toolchain preflight | Passed for attempting a development build |
| Original-code license and community guidance | Written; private vulnerability reporting verified enabled on the canonical repository |
| Identity and vertical-tab patches against exact upstream source files | Apply/reverse and XML checks passed; compilation pending |
| Tooling test suite | 46 tests passed locally, including build provenance, dependency inventory/bootstrap, and synthetic native-host safeguards |
| Extension fixture scope guards | 12 Node tests passed; no Chrome APIs executed |
| Public GitHub repository | Published: [vitaliy-blprnt/OpenArc](https://github.com/vitaliy-blprnt/OpenArc) |
| Hosted source checks | Passed on Linux/macOS for checkpoint `9ff6d0b` ([run](https://github.com/vitaliy-blprnt/OpenArc/actions/runs/37175254022)) |
| Chromium baseline build | GN generation passed; compilation in progress |
| OpenArc patched build | Not run |
| Visible browser UI | Not tested |
| Chrome Web Store / extension runtime / native integration | Not tested |
| Arc-style saved tabs and Spaces | Not implemented |
| Signed release and updater | Not implemented |

The existing extension inventory is local-only under ignored build artifacts.
It is not part of public source or public evidence.

## Milestones

| Milestone | State |
| --- | --- |
| M0 — Open-source foundation and macOS baseline | In progress |
| M1 — Extension foundation | Pending baseline |
| M2 — Upstream update rehearsal | Pending extension baseline |
| M3 — Durable Space/saved-tab model | Pending foundation gates |
| M4 — Sidebar and saved tabs | Pending model |
| M5 — Spaces, Profiles, windows | Pending sidebar |
| M6 — Navigation and essentials | Pending Spaces |
| M7 — Migration and recovery | Pending integration |
| M8 — Visual/interaction qualification | Pending working browser |
| M9 — Public release acceptance | Pending required gates |

Sources: [Google Version History](https://versionhistory.googleapis.com/v1/chrome/platforms/mac_arm64/channels/stable/versions/all/releases?filter=endtime=none),
[pinned Chromium commit](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33),
[SDK minimum](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/build/config/mac/mac_sdk_overrides.gni).
