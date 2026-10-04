# Releasing OpenArc

This is the release contract, not evidence that a release pipeline exists. The
initial scaffold has no qualified browser binary, signing/notarization pipeline,
or trusted automatic-update channel. Publishing source does not qualify a browser
for daily use or authorize a binary-release claim.

## Record the evidence

Every release candidate must identify the OpenArc commit/tag, exact Chromium and
depot_tools revisions, dependency resolution, patch-series hashes, GN arguments,
compiler/Xcode/SDK versions, target architecture, and build artifact hashes.
Preserve build/test logs with secrets and personal paths removed. Compare the
locked engine with current upstream stable/security releases when preparing each
candidate; a previously verified lock can become outdated.

Use the gates in [BROWSER-PLAN.md](BROWSER-PLAN.md). Record each required check as
passed, failed, or unverified, linked to evidence from that candidate. An unresolved
required check blocks qualification; do not relabel it as unsupported to claim
completion.

## Source qualification

- A clean checkout builds with documented public dependencies and no maintainer
  credentials. Validate the instructions in [BUILDING.md](BUILDING.md).
- The lock, patches, original modules, tests, and build configuration correspond
  to the tagged source. Preserve upstream notices and retain provenance for new
  code and assets.
- Inventory the exact shipped dependencies/resources and generate the applicable
  Chromium credits/notices using tools from the locked revision. Check them
  against [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES); that initial file alone is
  not a complete binary notice bundle. Include required license/source materials.
- Confirm that source, history, logs, and generated artifacts contain no release
  credentials, private profile data, or copied proprietary branding.

Passing these checks establishes source-build evidence only.

## Browser and integration qualification

Run the plan's application, extension, recovery, and UI acceptance checks in the
actual candidate. Keep upstream sandboxing, site isolation, permission prompts,
and certificate/security behavior intact. Cover Space changes and real tab APIs,
saved-entry close/unpin/restore, multiple windows, private browsing, imports,
crash recovery, and versioned metadata migration.

Test the full extension platform and the owner's installed extension inventory,
including store installation, permissions, extension updates, and password-manager
native integration. A finite suite cannot prove universal compatibility; report
the observed scope and any unresolved gaps. Repeat identity-dependent tests in
the signed application below.

Record configured security services, ordinary web workflows, media/codecs, and
any required DRM behavior. Build flags or an available API do not establish
service access, distribution rights, or successful runtime behavior.

## Signed macOS distribution

Choose and record the application bundle identity, profile directory, Keychain
identity, release channel, and original icon. Keep them stable across updates.
Developer builds and other browsers must not share production profile storage.

Provide the project's own signing identities and entitlements. Sign and verify
the application and all required nested helpers, package it, and complete the
notarization and distribution checks appropriate to the selected macOS channel.
Verify the final downloaded artifact on a clean installation, including TCC
prompts, Keychain behavior, extensions, and native password-manager trust. Never
disable platform or vendor trust checks to force acceptance.

Chromium's macOS signing guide explains the distinction between development and
distribution identities; Apple's documentation describes notarization. These are
reference procedures, not commands already automated by this repository.
[Chromium signing](https://chromium.googlesource.com/chromium/src/+/main/chrome/installer/mac/signing/README.md),
[Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)

## Update and recovery qualification

Before enabling a release update channel:

- Authenticate update metadata and verify downloaded artifacts using the chosen
  mechanism; reject tampered or unauthorized updates.
- Exercise an actual update across an upstream Chromium change using the completed
  OpenArc feature set and a representative synthetic profile.
- Verify interrupted download/install recovery and preservation of Spaces, saved
  entries, sessions, and extension state.
- Back up before incompatible schema migration. A binary rollback must not open
  newer profile data unless supported; restore a compatible backup when required.
- Show the installed engine/build version and update failures to the user.

Do not advertise automatic updates before this pipeline is implemented and tested.

## Publishing and maintenance

Only trusted release jobs may access signing, notarization, update-signing, or
publishing credentials. Keep those credentials out of source and out of untrusted
pull-request jobs. Verify private security reporting is enabled and the
[security policy](../SECURITY.md) reflects actual maintainers and supported versions.

Publish the reviewed source tag, artifact hashes, required notices, platform and
build details, release notes, and compatibility/recovery evidence together. Label
development artifacts explicitly. Record personal daily-use acceptance and public
release qualification separately.

Assign a maintainer to upstream security updates and rehearse the next update.
If maintenance stops, remove supported-release claims and say so in the security
policy and download surface. Do not continue distributing a frozen browser as a
current supported release.
