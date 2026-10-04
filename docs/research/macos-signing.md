# macOS Developer ID signing prerequisites

Research date: 2026-10-04, America/Cayman. Chromium source pin:
`154.0.8037.98` / `b859317bf11f6be47f9b7799ec690a0a42a1fb33`.
This is a source-based execution plan, not signing, notarization, Gatekeeper,
or password-manager qualification. No signing modules, credential lookups,
builds, or browser launches were executed for this research.

## Build decision

The unbranded, non-component candidate can be built without changing the empty
`MAC_TEAM_ID` in Chromium branding solely to support Developer ID signing.
The browser signing driver accepts `--identity`; its generated configuration
contains the product, version, bundle ID, updater and ANGLE settings, but no
team-ID parameter. It passes the selected identity to `codesign --sign`.
The Developer ID certificate carries the signature's Team ID; it is not supplied
by Chromium's branding variable. [Driver](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/driver.py#101),
[generated configuration](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/build_props_config.py.in#9),
[signer](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/signing.py#25),
[Apple code-signing requirements](https://developer.apple.com/documentation/technotes/tn3127-inside-code-signing-requirements).

`MAC_TEAM_ID` does compile into `MAC_TEAM_IDENTIFIER_STRING`, and it participates
in generated Google-branded entitlements. Those entitlement templates are
excluded when `is_chrome_branded=false`. Changing the signing identity after the
build does not change compiled Team-prefixed Keychain access groups, including
the Touch ID/WebAuthn group. Those features require their own identity and
entitlement assessment; basic signature trust does not qualify them.
[Version header](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/common/chrome_version.h.in#26),
[entitlement selection](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/BUILD.gn#751),
[WebAuthn access group](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/browser/webauthn/chrome_web_authentication_delegate.cc#389).

The standard upstream signing workflow requires `is_debug=false` and
`is_component_build=false`. The separate OpenArc packaging mode additionally
retains `is_official_build=false`; it generates the matching `OpenArc Packaging`
directory through the `chrome/installer/mac` target. No Google signing assets or
Google-branded entitlement settings should be introduced for this candidate.
[Signing requirements](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/README.md#10),
[packaging inputs](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/BUILD.gn#127).

## Preserve the build and defer only the early assessment

Use a new staging directory containing copies of `OpenArc.app` and its matching
`OpenArc Packaging` directory. Retain the original candidate, build receipt and
hashes. Choose a distinct output directory. The upstream pipeline itself copies
the input app into temporary work before customization and signing, but explicit
staging also isolates any signing-configuration edits from generated build files.
[Pipeline copy](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/pipeline.py#80).

For the staged signing configuration, the narrow seam is
`ChromiumCodeSignConfig.run_spctl_assess`: override it to return `False` in the
staged copy, or select an equivalent subclass in a separate wrapper. Keep the
remaining nondevelopment configuration unchanged. Do not use `--development`:
that mode injects `get-task-allow` and changes other validation requirements.
The normal configuration does not inject that entitlement.
[Chromium config](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/chromium_config.py#8),
[normal defaults](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/config.py#189),
[development overrides](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/driver.py#40).

This defers only an automated post-sign assessment invoked by the Python
pipeline. It does **not** disable macOS Gatekeeper, change system policy, remove
quarantine, or authorize ignoring a failed final assessment. Keep Gatekeeper
enabled and assess the completed artifact separately. The reason to defer the
pipeline's assessment is ordering: it runs `spctl --assess` immediately after
signing and aborts on failure, before notarization is submitted. An unnotarized
Developer ID app can therefore stop the pipeline before its notary stage.
[Assessment and failure handling](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/signing.py#156),
[notarization occurs later](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/pipeline.py#832).

The remaining pipeline still signs nested products first, then the framework,
then the outer app, and performs deep/strict signature verification. Preserve
its per-process Hardened Runtime flags and renderer/GPU JIT entitlements instead
of applying one blanket entitlement file or a blanket deep re-sign.
[Product flags](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/parts.py#36),
[signing order](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/parts.py#260).

## Local signed candidate

After explicitly selecting the user's Developer ID **Application** identity,
the staged driver's app-only invocation is:

```sh
python3 "<staging>/OpenArc Packaging/sign_chrome.py" \
  --input "<staging>" --output "<separate-signed-output>" \
  --identity "<user-selected Developer ID Application identity or certificate hash>" \
  --disable-packaging --notarize none
```

No Team ID flag or installer identity is needed for this app-only invocation.
The browser pipeline accepts the identity's name or certificate hash, while the
distinct installer identity is relevant to PKGs. This command is a plan only;
the staged assessment override above is required if the early assessment would
reject the unnotarized candidate. Record signature identity, Hardened Runtime,
entitlements and strict verification separately from vendor behavior. An
unnotarized candidate is not release-qualified.
[Identity parameters](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/config.py#49),
[driver modes](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/driver.py#128).

## Notarized distribution flow

Unbranded configuration supports notarization: it uses the standard invoker and
the common pipeline, without a Google-branding gate around notarization. It does
not require a Chromium provisioning profile for the ordinary unbranded
entitlements. The default distribution creates a DMG. With the staged assessment
override retained, the source-supported distribution invocation is:

```sh
python3 "<staging>/OpenArc Packaging/sign_chrome.py" \
  --input "<staging>" --output "<separate-notarized-output>" \
  --identity "<user-selected Developer ID Application identity or certificate hash>" \
  --notarize staple \
  --notary-arg=--keychain-profile \
  --notary-arg="<explicitly provided user-owned notary profile>"
```

Do not add `--disable-packaging` to this invocation. The pipeline signs with
secure timestamps when notarization is enabled, submits the app, staples its
ticket, creates/signs the DMG, then submits and staples the DMG. No PKG is requested,
so this path does not require a Developer ID Installer identity. Preserve a
separate final assessment step for both the app inside the delivered package and
the distribution artifact; pipeline success alone does not perform that final
Gatekeeper assessment once the early check is deferred.
[Unbranded invoker selection](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/config_factory.py#27),
[default DMG distribution](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/model.py#280),
[secure timestamp](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/signing.py#25),
[app and packaging sequence](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/pipeline.py#747),
[DMG notarization and staple](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/pipeline.py#921).

Only use a notary profile supplied by its owner. Do not enumerate Keychain
credentials or create an authentication profile on their behalf. If none is
provided, notarization remains pending. A profile supplies notarization
authentication separately from Chromium's compiled `MAC_TEAM_ID` and the chosen
code-signing certificate. The upstream `--notary-arg` interface passes profile
arguments through to `notarytool`.
[Notary argument forwarding](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/notarize.py#30),
[Apple profile usage](https://developer.apple.com/documentation/technotes/tn3147-migrating-to-the-latest-notarization-tool).

### Pinned app-only notarization output issue

Source inspection identifies a separate artifact-retention issue with
`--disable-packaging --notarize staple`: notarization mode selects the temporary
notary work directory for the app, `sign_all` staples there and skips packaging,
and its work-directory context then deletes that directory. No app-copy-to-output
step is present in that branch. This is a source-derived finding, not an executed
reproduction. Use the DMG flow above, or separately review an app-only workflow
that preserves the timestamped signed app through notarization and stapling.
[Destination selection](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/pipeline.py#813),
[end of signing pipeline](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/pipeline.py#761),
[temporary cleanup](https://chromium.googlesource.com/chromium/src/+/b859317bf11f6be47f9b7799ec690a0a42a1fb33/chrome/installer/mac/signing/commands.py#220).

## Final evidence gates

Record the final artifact's strict nested signature verification, expected
Developer ID identity, per-process Hardened Runtime and entitlements, secure
timestamps, accepted notary results, validated stapled tickets, and successful
Gatekeeper assessment. Perform the actual extension/native password-manager
checks against that exact artifact with an isolated profile. Keep identity values
and credential details out of published logs. Signing changes binary hashes, so
the original compilation receipt does not itself authorize a signed-copy launch.
Apple requires valid Developer ID signatures, Hardened Runtime and secure
timestamps for notarization; notarization and vendor compatibility are distinct
gates. [Apple notarization requirements](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).
