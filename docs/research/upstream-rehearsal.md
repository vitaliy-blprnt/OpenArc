# Chromium 155 patch rehearsal

Checked 4 October 2026 UTC. **Patch feasibility only; M2 is not complete.** The active checkout and `upstream.lock` remain on Chromium 154.0.8037.98, commit `b859317bf11f6be47f9b7799ec690a0a42a1fb33`. No upgrade, GN generation, compilation, browser launch, or extension test was performed against the candidate.

The official Git tag **155.0.8059.26** resolves to **`16c3e55476d3564bea713314b2fff638749ce3e6`**. A narrowly scoped `git ls-remote` and the official Gitiles tag response returned the same commit. This identifies the tested candidate; it does not establish rollout coverage or approve changing the project's pin. [Official tag](https://chromium.googlesource.com/chromium/src/+/refs/tags/155.0.8059.26), [exact commit](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6).

## Patch results

After adding workspace tab ownership and bookmark write acknowledgement on
4 October, downloaded all 35 files affected by the nine-patch series from the
exact candidate revision into a new isolated, ignored snapshot repository.
All nine patches passed sequential `git apply --check --index` and application
at both the pinned revision and the candidate:

| Patch | Result |
| --- | --- |
| `0001-openarc-identity.patch` | Applied without conflict |
| `0002-openarc-vertical-tabs.patch` | Applied without conflict |
| `0003-openarc-no-ai-defaults.patch` | Applied without conflict |
| `0004-openarc-disable-automated-password-change.patch` | Applied without conflict |
| `0005-openarc-disable-bundled-fieldtrial-tests.patch` | Applied without conflict |
| `0006-openarc-visible-product-strings.patch` | Applied without conflict |
| `0007-openarc-preserve-search-engine-migration.patch` | Applied without conflict |
| `0008-openarc-workspace-tab-state.patch` | Applied without conflict |
| `0009-openarc-bookmark-primary-write-ack.patch` | Applied without conflict |

The resulting indexed diff passed `git diff --cached --check`. Reversing the
patches in reverse order restored every downloaded file's original SHA-256 and
left the snapshot's Git status empty. Hashes of `upstream.lock` and all 35
corresponding files in the active checkout remained unchanged.

Current evidence is `.build/workspace-nine-patch-validation/evidence.json`,
with exact source URLs, patch/source/patched hashes, commands and separate
pinned-154/candidate-155 snapshots. Source checks also covered eleven disabled
feature definitions, the Autofill AI Settings producer/consumer binding, ordinary
Autofill card markers, bundled field-trial configuration, the search-migration
default, unchanged resource identities/placeholders, attribution and plist/GRIT
parsing. UMA value 15 preserves obsolete value 5. The workspace patches pass
textual apply/reverse here; their C++ interfaces and behavior have not been
compiled or tested against the candidate. The original-source overlay was not
GN-generated, compiled or executed against candidate 155.

The earlier `.build/upstream-rehearsal-155.0.8059.26-7iwxbrfk/evidence.json`
remains the source for tag resolution and the SDK/vertical-tab comparisons below.
Intermediate evidence under `.build/no-ai-audit-validation/`,
`.build/password-change-gate/validation/` and
`.build/search-migration-default/validation/` covers earlier patch sets only.
All directories are ignored local evidence, not published build artifacts.

## Source-seam comparison

The SDK configuration is byte-identical to the current pin: official SDK **26.5 / 25F70**, minimum supported SDK **15**, and deployment/minimum launch OS **13.0**. The host's SDK 26.2 remains below the official reference; an actual candidate build must determine compatibility. The SDK minimum is distinct from the reference SDK and deployment target. [SDK configuration](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/build/config/mac/mac_sdk.gni), [minimum SDK](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/build/config/mac/mac_sdk_overrides.gni).

`tab_strip_prefs.cc` is byte-identical, retaining `RegisterBooleanPref(prefs::kVerticalTabsEnabled, false)`, so the default-on patch seam survives. The vertical controller keeps its filename and unchanged header/interface. Its implementation change adds keyboard-shortcut text to collapse/expand tooltips; the preference behavior is unchanged in this comparison. [Preference registration](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/tab_strip_prefs.cc), [controller](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/vertical_tab_strip_state_controller.cc), [interface](https://chromium.googlesource.com/chromium/src/+/16c3e55476d3564bea713314b2fff638749ce3e6/chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h).

An actual M2 rehearsal still requires an explicitly updated pin, dependency sync, complete rebuild, launch, and the applicable baseline/extension checks with recorded results. Successful textual patch application cannot establish those outcomes or the continued runtime behavior of the patched feature gates.
