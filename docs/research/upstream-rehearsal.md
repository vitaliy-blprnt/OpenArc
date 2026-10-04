# Chromium 155 patch rehearsal

Checked 4 October 2026 UTC. **Patch feasibility only; M2 is not complete.** The active checkout and `upstream.lock` remain on Chromium 154.0.8037.98, commit `b859317bf11f6be47f9b7799ec690a0a42a1fb33`. No upgrade, GN generation, compilation, browser launch, or extension test was performed against the candidate.

The official Git tag **155.0.8059.26** resolves to **`16c3e55476d3564bea713314b2fff638749ce3e6`**. A narrowly scoped `git ls-remote` and the official Gitiles tag response returned the same commit. This identifies the tested candidate; it does not establish rollout coverage or approve changing the project's pin. [Official tag](https://chromium.googlesource.com/chromium/src/+/refs/tags/155.0.8059.26), [exact commit](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6).

## Patch results

After extending the no-AI patch on 4 October, downloaded all seventeen files affected by the current patch series afresh from the exact candidate revision into a new isolated, ignored snapshot repository. All three patches passed sequential `git apply --check --index` and application:

| Patch | Result |
| --- | --- |
| `0001-openarc-identity.patch` | Applied without conflict |
| `0002-openarc-vertical-tabs.patch` | Applied without conflict |
| `0003-openarc-no-ai-defaults.patch` | Applied without conflict |

The resulting indexed diff passed `git diff --cached --check`. Reversing the patches in reverse order restored every downloaded file's original SHA-256 and left the snapshot's Git status empty. Hashes of `upstream.lock` and the seventeen corresponding files in the active checkout remained unchanged.

Current ordered apply/reverse evidence is `.build/no-ai-audit-validation/evidence.json`, with exact source URLs, source/patched hashes, commands and separate pinned-154/candidate-155 snapshots. The current no-AI patch SHA-256 is `b90f0d6e8536cd4ba6f3a10df1cec757caf712d043f922d3ae3942ab0d620466`. Checks also covered ten disabled feature definitions, the Autofill AI Settings producer/consumer binding, ordinary Autofill card markers, attribution, and plist/GRIT parsing. These are static source checks, not TypeScript or C++ compilation.

The earlier `.build/upstream-rehearsal-155.0.8059.26-7iwxbrfk/evidence.json` remains the source for tag resolution and the SDK/vertical-tab comparisons below; its eleven-file patch result covers an older patch hash and is superseded by the seventeen-file validation. All directories are ignored local evidence, not published build artifacts.

## Source-seam comparison

The SDK configuration is byte-identical to the current pin: official SDK **26.5 / 25F70**, minimum supported SDK **15**, and deployment/minimum launch OS **13.0**. The host's SDK 26.2 remains below the official reference; an actual candidate build must determine compatibility. The SDK minimum is distinct from the reference SDK and deployment target. [SDK configuration](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/build/config/mac/mac_sdk.gni), [minimum SDK](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/build/config/mac/mac_sdk_overrides.gni).

`tab_strip_prefs.cc` is byte-identical, retaining `RegisterBooleanPref(prefs::kVerticalTabsEnabled, false)`, so the default-on patch seam survives. The vertical controller keeps its filename and unchanged header/interface. Its implementation change adds keyboard-shortcut text to collapse/expand tooltips; the preference behavior is unchanged in this comparison. [Preference registration](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/tab_strip_prefs.cc), [controller](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/vertical_tab_strip_state_controller.cc), [interface](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h).

An actual M2 rehearsal still requires an explicitly updated pin, dependency sync, complete rebuild, launch, and the applicable baseline/extension checks with recorded results. Successful textual patch application cannot establish those outcomes or the continued runtime behavior of the patched feature gates.
