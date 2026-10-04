# OpenArc implementation status

Updated 4 October 2026. This file records evidence, not inferred completion.

## Current checkpoint

The last accepted browser is the seven-patch OpenArc development build, which
compiled and ran without feature override flags. Native UI verified 154.0.8037.98 ARM64, its executable,
isolated development profile, bundle identity, and vertical tabs; Example Domain
rendered. Fixture 0.3.0 passed all 22 automated checks, the separate synthetic
native exchange, and all six manual extension surfaces.

uBlock Origin Lite retained its version, enabled state and Chrome Web Store
source after restart into this build; its popup and options were observed.
Real updates, filtering, password-manager trust, and broader extension acceptance
remain untested. M1 is incomplete. Bounded M3 model work can proceed against this
working baseline while full M1/M2 acceptance and broad custom-shell work remain
gated. M0's public-source, build, identity, profile, sandbox and reproducible-patch
gate is complete for this local development configuration.

The current checkout now has nine patches and a verified 28-file owned source
overlay for native workspace integration. GN generation and header dependency
checks passed for the workspace targets. All four focused native executables
built and ran successfully: 106 OpenArc tests and 317 upstream bookmark tests,
including the 14 added primary-write acknowledgement cases.
This newer source has not yet produced a qualified browser build. The old build
receipt is preserved as historical evidence and has been invalidated for launch.
The separate packaging compile was intentionally interrupted after 21,115
completed steps with zero compilation failures, retaining cached objects. It has
no successful build receipt and will resume as a new invocation after integration.

## Pinned baseline

- Chromium: `154.0.8037.98`, commit `b859317bf11f6be47f9b7799ec690a0a42a1fb33`.
- depot_tools: `8a5434051036b32412a2ecb10c213a72e3f3ccb9`.
- Target: Apple Silicon macOS, local component development build.
- Lock source: Google's macOS ARM64 stable Version History API and the official
  Chromium release tag, resolved on 3 October 2026.
- Host preflight: ARM64, 64 GiB RAM, APFS, Xcode 26.3, macOS SDK 26.2.
- The pin's minimum SDK is 15; its official reference SDK is 26.5. The local SDK
  meets the configured minimum, and the complete Chromium baseline build passed
  with it. This confirms this local development configuration, not the official
  reference toolchain or a signed release.

The exact inputs are in [upstream.lock](../upstream.lock). The upstream choice
does not promise this revision remains current after this checkpoint.

## Evidence ledger

| Check | Result |
| --- | --- |
| Official release tag and exact commit resolution | Verified |
| Host/toolchain preflight | Passed for attempting a development build |
| Original-code license and community guidance | Written; private vulnerability reporting verified enabled on the canonical repository |
| Identity, vertical-tab, AI-default and workspace patches against exact upstream source files | Seven patches built and launched; nine patches passed isolated ordered apply/reverse and source checks across 35 files at pinned 154 and candidate 155. Patch 0006 separately passed pinned GRIT generation for en, en-GB and fr with stable resource IDs; changed non-English labels fall back to English |
| Tooling test suite | 124 Python tests passed locally, covering workflow/provenance, source-overlay ownership/recovery, isolated workspace launch, native-host safeguards and upstream checking; hosted checks passed for `0e077ac` |
| Extension fixture scope guards | 31 Node tests passed locally; includes timer receiver binding, pin/group checks, interrupted group cleanup, and bounded move/activation event handling. Hosted checks passed for `0e077ac` |
| Native workspace tests | GN generation, header checks, compilation, linking and execution passed: 84 model/catalog/service tests, seven tab-state tests, 15 real-tab-strip controller tests, and 317 upstream bookmark tests including all 14 new primary-write acknowledgement cases. Frozen source/patch inputs were verified before and after execution. These tests do not qualify a browser build, sessions or visible sidebar behavior |
| Automated password-change product gate | Feature, service and unit-test translation units compiled separately with pinned Chromium 154 Clang; five disabled-path tests prepared, not linked or executed |
| Search migration startup repair | Patch 0007 preserves the previously enabled one-way search-engine migration. Two protected-copy reproductions failed with the six-patch default and passed with the seven-patch build without feature overrides. Feature and regression-test translation units compiled; the C++ test was not linked or executed |
| Public GitHub repository | Published: [vitaliy-blprnt/OpenArc](https://github.com/vitaliy-blprnt/OpenArc) |
| Hosted source checks | Passed on Linux/macOS for workspace-launch tooling checkpoint `0e077ac`, including 124 Python and 31 Node tests ([run](https://github.com/vitaliy-blprnt/OpenArc/actions/runs/37189787577)). Native compilation/execution is separate local evidence; these jobs do not build Chromium |
| Chromium baseline build | Succeeded: 46,695 build steps completed with the local SDK; native `chrome://version` verified 154.0.8037.98 ARM64 and the isolated baseline executable/profile; example.com rendered |
| OpenArc patched build | Seven-patch build succeeded after 29 final incremental steps and launched without feature overrides. Earlier four-patch build completed 984 incremental steps |
| Upstream patch rehearsal | All nine patches apply and reverse cleanly across 35 files at candidate 155.0.8059.26, recorded in ignored `.build/workspace-nine-patch-validation/evidence.json`. [Rehearsal details](research/upstream-rehearsal.md); candidate overlay GN/build and full upgrade/runtime checks pending |
| Visible OpenArc browser UI | Seven-patch native UI verified 154.0.8037.98 ARM64, bundle `org.openarc.browser`, exact development executable/profile, and native vertical tabs. Example Domain rendered. No Gemini/AI Mode controls on the observed new-tab page, no AI rows in the Settings menu, and no Autofill AI opt-in card; search by image remains visible |
| macOS sandbox status | `sandbox_check` on the exact isolated seven-patch browser returned the normal unsandboxed browser-process state; all six observed GPU/utility/renderer children were sandboxed. Ignored `.build/reports/openarc-sandbox-status.json`; this is process-state evidence, not a sandbox-escape/security audit |
| Chromium baseline fixture | Fixture 0.3.0: all 22 automated checks passed, including pin/group and move/activation events; synthetic native messaging returned the exact nonce/pong protocol 1 reply. All six manual extension surfaces observed. Isolated profile with mock Keychain |
| Reference-browser fixture | Earlier fixture 0.1.0: 19 API checks and synthetic native exchange passed in installed Chrome 154.0.8037.59 ARM64. This remains separate from the locally built Chromium baseline |
| OpenArc extension fixture | Seven-patch fixture 0.3.0 passed all 22 automated checks and exact nonce/pong protocol 1 native exchange. All six manual extension surfaces observed in this build |
| OpenArc Chrome Web Store | uBlock Origin Lite `2026.930.1227`, ID `ddkjiahejlhfcafbddmgiahcphecmpfh`, installed through the native prompt in the four-patch build; the same version, enabled state and Store source persisted after restart into seven patches. Popup/options observed; update delivery, filtering, vendor trust and credentials untested |
| Arc-style saved tabs and Spaces | Not yet visible in the browser. Native preparation includes catalog, ID/codecs, atomic workspace record, journaled default-Space/save service, discard-aware tab state and a default-Space window controller. Browser ownership, sessions and Views integration remain pending |
| Signed release and updater | Not qualified; the separate packaging build was intentionally interrupted for workspace integration, with completed objects retained and no success receipt. Signing and updater acceptance remain pending |

The completed `build --reuse-baseline` transition reused the baseline output
path for incremental compilation, preserved its receipt as historical evidence,
and invalidated baseline launch qualification before rebuilding. The seven-patch
build now launches with the development executable at
`.build/chromium/src/out/Baseline/OpenArc.app/Contents/MacOS/OpenArc`.
The launch manifest is ignored at `.build/reports/openarc-seven-patch-launch.json`;
native version/profile inspection and rendering were separately observed.

The read-only upstream check observed early stable `155.0.8059.26` at a `0.005`
rollout fraction on 4 October 2026 UTC, above the still-active pinned version.
This is an upgrade candidate, not a new source pin or a qualified update.

The AI-default patch covers ten existing feature definitions and the Autofill AI
opt-in UI. A separate default-off product gate now guards automated password
change availability, offers, direct starts, and model-quality reporting. Its
source compilation and review do not qualify all browser behavior or test
execution. Patch 0005 now builds with `FIELDTRIAL_TESTING_ENABLED=0`, removing
the automatic bundled `GlicDogfood` and `Skills` overrides. The seven-patch
observations cover the new-tab controls, Settings menu rows, and Autofill opt-in
card named above. Search by image remains visible, and model-download behavior
has not been qualified. Explicit feature overrides and stored variations seeds
remain separate mechanisms. This is not complete no-AI acceptance. See
[the source audit](CHROMIUM-INTEGRATION.md#built-in-ai-defaults).

Some product labels remain Chromium-branded, including Settings' About entry
and the main menu's Your Chromium label. Upstream attribution/logo also remains;
the observed bundle identity does not imply every visible label was renamed.

The existing extension inventory is local-only under ignored build artifacts.
It is not part of public source or public evidence.

Local baseline evidence is preserved in ignored
`.build/reports/baseline-extension-probe.json` and
`.build/reports/baseline-ui-observations.json`. The observed executable was
`.build/chromium/src/out/Baseline/Chromium.app` and the profile was
`.build/profiles/baseline/Default`. `chrome://version` showed a zero revision,
which is intentional for this non-official build's `use_dummy_lastchange`;
build receipts bind the actual locked source SHA. The reduced user-agent string
in the extension report is not the source for the exact version or architecture.

Manual baseline observations covered popup rendering, its dashboard action,
the Options entry, user-gesture side-panel rendering, the side-panel dashboard
action from a webpage, and the visible page context-menu dashboard action.
These observations are separate from the fixture's automatic report. The
synthetic host was unregistered and the owned baseline browser exited before
the patched build. Mock Keychain and synthetic transport do not qualify real credential
storage, a password-manager vendor's trusted-browser checks, or signed identity.
The earlier reference run also exposed and verified the fix for a browser-only
timer receiver bug in the probe.

The final fixture report is ignored at
`.build/reports/openarc-extension-probe-seven-patch.json`, run
`e3e63c90-1b03-431a-9e33-45d7ddbe37dd`. Native UI confirmed
`.build/profiles/development/Default`. Manual observations separately covered
popup rendering and dashboard action, the browser's Options entry, user-gesture
side-panel rendering and dashboard action from a webpage, and the visible page
context-menu dashboard action. These and the final Store/no-AI observations are
recorded in `.build/reports/openarc-ui-observations-seven-patch.json`. The
synthetic host was unregistered and the owned browser exited afterward.

Historical four-patch reports remain at
`.build/reports/openarc-extension-probe-four-patch.json` and
`.build/reports/openarc-ui-observations-four-patch.json`. Store installation,
restart retention, and extension popup/options are additional manual observations,
not fixture assertions or proof of updates, filtering, signed identity,
password-manager integration, or full compatibility.

## Milestones

| Milestone | State |
| --- | --- |
| M0 — Open-source foundation and macOS baseline | Complete for the documented local development configuration: public source, build/launch, isolated identity/profile, sandbox process state, patch reproducibility and vertical-tab seams verified |
| M1 — Extension foundation | In progress: final fixture/manual checks passed; one Store install and restart retention observed. Broader matrix, real updates and vendor checks pending |
| M2 — Upstream update rehearsal | Nine-patch source rehearsal passed; candidate overlay GN/build/runtime, update delivery and full acceptance pending |
| M3 — Durable Space/saved-tab model | 106 OpenArc native tests and 317 bookmark tests passed through GN, including tab/window controllers and the primary-write acknowledgement. Browser/session integration and full M3 acceptance pending |
| M4 — Sidebar and saved tabs | Bounded default-Space Views development is in source preparation; native integration tests and visible UI are pending. Broad shell and full acceptance still require accepted M1–M3 |
| M5 — Spaces, Profiles, windows | Pending sidebar |
| M6 — Navigation and essentials | Pending Spaces |
| M7 — Migration and recovery | Pending integration |
| M8 — Visual/interaction qualification | Pending working browser |
| M9 — Public release acceptance | Pending required gates |

Sources: [Google Version History](https://versionhistory.googleapis.com/v1/chrome/platforms/mac_arm64/channels/stable/versions/all/releases?filter=endtime=none),
[pinned Chromium commit](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33),
[SDK minimum](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/build/config/mac/mac_sdk_overrides.gni).
