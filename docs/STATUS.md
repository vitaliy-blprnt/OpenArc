# OpenArc implementation status

Updated 4 October 2026. This file records evidence, not inferred completion.

## Current checkpoint

M0 is in progress. Repository tooling, community files, license, and the initial
source patch series have been created. The pinned Chromium source and dependencies
have been fetched and their setup hooks completed. Dependency-inventory and
depot_tools bootstrap setup issues were corrected; GN generation passed and the
unmodified Chromium baseline is compiling. No locally built browser binary has
been run yet. The diagnostic extension has been exercised separately in an
installed reference Chrome using an isolated synthetic-data profile.

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
| Identity, vertical-tab, and AI-default patches against exact upstream source files | Current three-patch series passed apply/reverse, XML and source checks across 17 files at pinned 154 and candidate 155; compilation pending |
| Tooling test suite | 73 tests passed locally, including build provenance, baseline output reuse, interruption-safe patch reversal, dependency inventory/bootstrap, isolated native-host safeguards, and read-only upstream checks |
| Extension fixture scope guards | 31 Node tests passed; includes timer receiver binding, pin/group checks, interrupted group cleanup, and bounded move/activation event handling |
| Native model preparation | 24 C++ tests passed for association reconciliation and the bounded tab-session codec after standalone compilation with the pinned Chromium Clang, headers, libbase, and libc++; the documented recipe was executed. GN/browser integration has not been run |
| Public GitHub repository | Published: [vitaliy-blprnt/OpenArc](https://github.com/vitaliy-blprnt/OpenArc) |
| Hosted source checks | Passed on Linux/macOS for checkpoint `7ac9ed8`, including 73 Python and 31 Node tests ([run](https://github.com/vitaliy-blprnt/OpenArc/actions/runs/37180249971)); native helper compilation is separate local evidence |
| Chromium baseline build | GN generation passed; compilation in progress |
| OpenArc patched build | Not run |
| Upstream patch rehearsal | All three patches apply and reverse cleanly against candidate 155.0.8059.26; SDK and vertical-tab seams inspected. [Evidence and limits](research/upstream-rehearsal.md); full upgrade/rebuild/runtime checks pending |
| Visible OpenArc browser UI | Not tested |
| Reference-browser fixture | Fixture 0.1.0: 19 API checks and synthetic native nonce/pong exchange passed in installed Chrome 154.0.8037.59 (ARM64), with mock Keychain and a dedicated profile; popup rendering, user-gesture side panel, side-panel dashboard action, and Options entry observed. Fixture 0.3.0 includes pin/group and event-delivery checks (22 total); its browser run is pending |
| OpenArc Chrome Web Store / extension runtime / native integration | Not tested; reference-browser fixture results do not qualify OpenArc |
| Arc-style saved tabs and Spaces | Browser features not implemented; pure identifier reconciliation and tab-session codec prepared under `src/openarc/workspace` |
| Signed release and updater | Not implemented |

The optional `build --reuse-baseline` transition has tooling coverage. It keeps
the completed baseline's output path for incremental compilation, preserves its
receipt as historical evidence, and invalidates baseline launch qualification
before rebuilding. Its actual use awaits the baseline build and runtime checks.

The read-only upstream check observed early stable `155.0.8059.26` at a `0.005`
rollout fraction on 4 October 2026 UTC, above the still-active pinned version.
This is an upgrade candidate, not a new source pin or a qualified update.

The AI-default patch now covers ten feature definitions and the Autofill AI
opt-in UI. The automated password-change service remains a known gap requiring
a focused product gate and tests; visible UI and model-download behavior remain
unqualified. See [the source audit](CHROMIUM-INTEGRATION.md#built-in-ai-defaults).

The existing extension inventory is local-only under ignored build artifacts.
It is not part of public source or public evidence.

The reference run exposed a browser-only timer receiver bug in the probe, fixed
and retested with an exact native reply. Its JSON report is local under ignored
build artifacts. The synthetic native host was unregistered after the run and
the dedicated test process stopped. No credential or vendor-trust tests were
performed. The popup's dashboard action and visible context-menu interaction
remain unverified; their API configuration checks do not replace those actions.

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
