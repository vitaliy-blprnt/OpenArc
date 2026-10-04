# OpenArc implementation status

Updated 4 October 2026. This file records evidence, not inferred completion.

## Current checkpoint

M0 is in progress. The pinned, unmodified Chromium baseline built successfully
and was launched in its isolated synthetic-data profile. Its native version UI
and example.com rendering were observed; fixture 0.3.0 passed all 22 automated
checks, the synthetic native exchange, and the six manual extension surfaces.
The four OpenArc patches are now applied and the actual patched build is running
with `build --reuse-baseline --jobs 12`. OpenArc identity, UI, extension behavior,
and no-AI defaults remain unverified until that build succeeds and is tested.

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
| Identity, vertical-tab, and AI-default patches against exact upstream source files | Current four-patch series passed apply/reverse, XML and source checks across 23 files at pinned 154 and candidate 155; full patched build pending |
| Tooling test suite | 77 tests passed locally (51 workflow, 14 native-host, 12 upstream-checker), including build provenance, baseline output reuse, interruption-safe patch reversal, dependency inventory/bootstrap, isolated native-host safeguards, and read-only upstream checks |
| Extension fixture scope guards | 31 Node tests passed; includes timer receiver binding, pin/group checks, interrupted group cleanup, and bounded move/activation event handling |
| Native model preparation | 24 C++ tests passed for association reconciliation and the bounded tab-session codec after standalone compilation with the pinned Chromium Clang, headers, libbase, and libc++; the documented recipe was executed. GN/browser integration has not been run |
| Automated password-change product gate | Feature, service and unit-test translation units compiled separately with pinned Chromium 154 Clang; five disabled-path tests prepared, not linked or executed |
| Public GitHub repository | Published: [vitaliy-blprnt/OpenArc](https://github.com/vitaliy-blprnt/OpenArc) |
| Hosted source checks | Passed on Linux/macOS for checkpoint `5b79adf`, including 73 Python and 31 Node tests ([run](https://github.com/vitaliy-blprnt/OpenArc/actions/runs/37182665108)); the newer 77-Python-test checkpoint is pending hosted validation. Native helper compilation is separate local evidence |
| Chromium baseline build | Succeeded: 46,695 build steps completed with the local SDK; native `chrome://version` verified 154.0.8037.98 ARM64 and the isolated baseline executable/profile; example.com rendered |
| OpenArc patched build | Four patches applied; actual `build --reuse-baseline --jobs 12` compilation in progress. No patched-browser launch result yet |
| Upstream patch rehearsal | All four patches apply and reverse cleanly against candidate 155.0.8059.26; SDK and vertical-tab seams inspected. [Evidence and limits](research/upstream-rehearsal.md); full upgrade/rebuild/runtime checks pending |
| Visible OpenArc browser UI | Not tested |
| Chromium baseline fixture | Fixture 0.3.0: all 22 automated checks passed, including pin/group and move/activation events; synthetic native messaging returned the exact nonce/pong protocol 1 reply. All six manual extension surfaces observed. Isolated profile with mock Keychain |
| Reference-browser fixture | Earlier fixture 0.1.0: 19 API checks and synthetic native exchange passed in installed Chrome 154.0.8037.59 ARM64. This remains separate from the locally built Chromium baseline |
| OpenArc Chrome Web Store / extension runtime / native integration | Not tested; neither the unmodified Chromium baseline nor reference-browser fixture results qualify patched OpenArc, Store installation/update, vendor trust, or credentials |
| Arc-style saved tabs and Spaces | Browser features not implemented; pure identifier reconciliation and tab-session codec prepared under `src/openarc/workspace` |
| Signed release and updater | Not implemented |

The actual `build --reuse-baseline` transition is underway. It keeps the
completed baseline's output path for incremental compilation, preserves its
receipt as historical evidence, and invalidates baseline launch qualification
before rebuilding. The running build log is ignored at
`.build/logs/openarc-build.log`; completion and a new OpenArc launch receipt are
still pending.

The read-only upstream check observed early stable `155.0.8059.26` at a `0.005`
rollout fraction on 4 October 2026 UTC, above the still-active pinned version.
This is an upgrade candidate, not a new source pin or a qualified update.

The AI-default patch covers ten existing feature definitions and the Autofill AI
opt-in UI. A separate default-off product gate now guards automated password
change availability, offers, direct starts, and model-quality reporting. Its
source compilation and review do not qualify browser behavior or test execution;
visible UI and model-download checks remain pending. See
[the source audit](CHROMIUM-INTEGRATION.md#built-in-ai-defaults).

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

## Milestones

| Milestone | State |
| --- | --- |
| M0 — Open-source foundation and macOS baseline | In progress |
| M1 — Extension foundation | Chromium baseline fixture passed; patched OpenArc, Store and vendor checks pending |
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
