# Chromium foundation research

Research date: 2026-10-03, America/Cayman. Scope: personal macOS browser, Arc-like Spaces, saved tabs that open in place, and a left sidebar, with full Chrome extension support including the user's existing password manager. The user explicitly requested all extensions, not a curated subset. This is source research and an implementation recommendation; no browser binary or extension compatibility has been tested.

## Recommendation

Build a deliberately small fork of the **Chromium browser application**, retaining its browser services and native Views UI framework. Change the tab organization and presentation, not Blink, V8, the network stack, sandbox, or extension runtime. Keep the custom work in a small patch series and new browser-layer modules. “Small fork” describes the intended modification boundary, not the size of the build or an assurance that rebasing is effortless.

This recommendation follows from the hard extension requirement. A browser fork gives direct access to the same tab and extension models that existing browser UI uses. It still requires end-to-end proof of the actual Chrome Web Store installation/update flow and the actual password manager. Neither compiling Chromium nor opening an extension popup proves those integrations.

## Options compared

| Foundation | What it supplies | Main issue for this browser | Decision |
| --- | --- | --- | --- |
| Chromium browser fork | The complete open-source browser layer, including the existing browser window and tab model; directly editable native browser UI. | Large source/build environment and an ongoing upstream merge/release responsibility. New Space semantics must coexist with extension-visible tabs. | Recommended. Reuse services and minimize changed seams. |
| Electron shell with `WebContentsView` | Chromium-backed page views and a convenient application framework. | Electron explicitly does not support arbitrary Chrome Web Store extensions; it supports only a subset of extension APIs and only unpacked extensions. Chrome compatibility is a stated non-goal. | Fails the stated extension requirement as the production foundation. A UI-only prototype would prove presentation only. |
| CEF with modern Chrome runtime | An embedding API, binary distributions, and a Chrome browser runtime with extension/autofill/browser UI functionality. | Must prove how much of the Arc-like tab/window model can be controlled through CEF while preserving native extension behavior. Falling through to CEF/Chromium patches reduces the benefit of the abstraction. | Credible alternative for a constrained feasibility spike, not the default recommendation. |

Sources: [Chromium browser window](https://www.chromium.org/developers/design-documents/browser-window/), [TabStripModel source](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/chrome/browser/ui/tabs/tab_strip_model.h), [Electron extension support](https://www.electronjs.org/docs/latest/api/extensions), [Electron WebContentsView](https://www.electronjs.org/docs/latest/api/web-contents-view), [CEF architecture](https://chromiumembedded.github.io/cef/architecture.html), [CEF distributions and usage](https://chromiumembedded.github.io/cef/general_usage.html).

CEF nuance: old claims that “CEF cannot run Chrome extensions” are too broad. Its Chrome layer has extension support, and the old Alloy bootstrap was removed in M128. Chrome and Alloy *styles* remain distinct. The current CEF tutorial describes Chrome style as full Chrome UI. That does not establish compatibility of a new custom shell with every extension, store flow, or native password-manager bridge. [CEF tutorial](https://chromiumembedded.github.io/cef/tutorial.html)

## Reuse existing vertical tabs before inventing a new tab strip

Current Chromium source already contains native vertical-tab infrastructure. The source was inspected online; these are discovery pointers on `main`, not a statement that a particular stable build exposes all behavior:

- [`VerticalTabStripRegionView`](https://github.com/chromium/chromium/blob/main/chrome/browser/ui/views/frame/vertical_tab_strip_region_view.cc) contains pinned/unpinned containers, layout, collapse/hover behavior, and tab interaction integration.
- [`VerticalTabStripStateController`](https://github.com/chromium/chromium/blob/main/chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h) controls whether vertical tabs are enabled and their collapsed state.
- [`TabStripCollectionController`](https://github.com/chromium/chromium/blob/main/chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h) connects views to the existing tab model.
- [`BrowserViewTabbedLayoutImpl`](https://github.com/chromium/chromium/blob/main/chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc) handles vertical strip sizing and browser layout.
- [macOS immersive UI tests](https://chromium.googlesource.com/chromium/src.git/+/refs/heads/main/chrome/browser/ui/views/frame/immersive_mode_controller_mac_interactive_uitest.mm) exercise vertical-tab/fullscreen interactions. This is a useful regression surface, not evidence that our future modifications work.

First implementation step: select and record the current appropriate stable Chromium release tag/commit, its dependencies and toolchain; build that unmodified baseline; then inspect which of these seams exist in that exact ref. Do not follow `main` as a daily-use release branch or assume that pinned tabs already implement Arc's persistent saved-item semantics.

Retain Views/C++ for the browser shell and use existing Objective-C++/AppKit integration where necessary. A separate SwiftUI outer shell is technically an option, but would create a second integration layer for tab ownership, focus, accessibility, menus, drag/drop, full screen, permission bubbles and extension popup anchoring. The lower-risk design judgment is to style and extend the native browser implementation first. Chromium's design guidance favors browser-layer controllers with precise ownership rather than placing feature logic inside Views. [Browser feature design principles](https://chromium.googlesource.com/chromium/src.git/+/refs/heads/main/docs/chrome_browser_design_principles.md)

## Full extension support has several acceptance gates

1. **Runtime:** the current extensions the user needs must load, run their background/service-worker logic, inject content scripts, access granted sites, and display actions/options correctly. Preserve upstream extension services and permission UI.
2. **Store:** install through the normal Chrome Web Store flow, preserve the extension ID/signature, retain enabled state on restart, and observe an actual extension update. Chromium has a [WebstoreInstaller implementation](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/extensions/browser/webstore_installer.cc); code presence is not a guarantee that a newly branded build's end-to-end store flow works. Chromium extensions also have a distinct [update lifecycle](https://developer.chrome.com/docs/extensions/develop/concepts/extensions-update-lifecycle).
3. **Tab semantics:** verify extension tab queries, active-tab permissions, new-tab creation, tab moves, window enumeration and tab switching across Spaces. Do not maintain an unrelated “fake tab list” visible only to the sidebar.
4. **Password-manager extension:** test login, unlock, save, autofill, generation, passkeys where used, locking and restart with the user's actual manager.
5. **Native integration:** separately test desktop-app unlock and biometrics where used. Native messaging uses browser-specific host lookup locations and an extension-ID allowlist. A new browser identity/profile directory can break discovery even when extension JavaScript works. [Chrome native messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging)
6. **Signed application:** run the same native integration tests against the final application identity and signing configuration. For example, 1Password currently supports adding an additional trusted macOS browser but requires a code-signed app in Applications and the installable browser extension. This is vendor-specific evidence, not a promise for an unidentified manager. [1Password additional browsers](https://support.1password.com/additional-browsers/)

Preserve the full upstream extension platform as a product requirement. Capture the user's complete installed extension inventory before the first integration spike and test all of it, alongside upstream extension regression coverage; this inventory is evidence, not a narrowed product scope. Do not promise every historical extension or a proprietary Google-only service. Do not bypass a password manager's browser identity checks or re-sign third-party extensions to make a test appear successful.

## Chromium is not the whole Google Chrome product

| Area | Verified constraint and planning consequence |
| --- | --- |
| Google account browser sync | Google restricts private Chrome APIs for third-party browsers. Plan local storage/import/export first; do not promise Chrome Sync. Website login with a Google account is a separate flow. [Google announcement](https://blog.chromium.org/2021/01/limiting-private-api-availability-in.html) |
| Google API-dependent features | Some Chromium features need separately acquired credentials, access and quotas; the API-key guide warns about restricted sign-in and limited quotas. Inventory outbound service dependencies and test the selected configuration. Do not borrow Google's keys. [Chromium API keys](https://www.chromium.org/developers/how-tos/api-keys/) |
| Safe Browsing | Protection requires a working service/configuration, not merely compiling the code or displaying a setting. Resolve credentials, privacy behavior and applicable terms; test a safe vendor test page and confirm warning behavior. Public Safe Browsing APIs distinguish non-commercial usage from commercial Web Risk, and the v4 documentation now marks v4 deprecated. Choose a current supported integration when implementing. [Safe Browsing](https://developers.google.com/safe-browsing), [v4 status and usage boundary](https://developers.google.com/safe-browsing/v4) |
| Media codecs | Chromium documents build-dependent codec support through `ffmpeg_branding` and `proprietary_codecs`. A flag changes support/configuration; it does not establish rights to distribute codecs. Test the user's actual media sites on the final build. [Chromium audio/video](https://www.chromium.org/audio-video/) |
| DRM streaming | Widevine integration requires its own license agreement. Its platform list includes Chromium/CEF/Electron, but that does not mean a custom binary automatically includes an authorized CDM or receives service acceptance. Treat protected streaming as an explicit feasibility gate if needed. [Widevine overview](https://developers.google.com/widevine/drm/overview) |
| Branding and licenses | Use an original app name/icon and retain Chromium and bundled third-party notices. Chromium's root license allows modified source/binary redistribution subject to conditions; Google Chrome branding assets are not provided under that open-source license. [Chromium license](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/LICENSE), [Chrome branding](https://github.com/chromium/chromium/blob/main/docs/google_chrome_branded_builds.md), [third-party policy](https://chromium.googlesource.com/chromium/src.git/+/HEAD/docs/adding_to_third_party.md) |
| App updates | Building a browser does not create a hosted, trusted update channel. The fork owns its release package, signing, update manifest/verification, installation and recovery process. This is a project requirement rather than a claim of automatic Chromium functionality. |

## macOS build and delivery requirements

Official instructions support Intel or Arm Macs, require Xcode/macOS SDK and an APFS volume, and use `depot_tools`, GN and Ninja to build the `chrome` target. Select the SDK required by the pinned source ref rather than hard-coding today's tool versions into the plan. Check available disk and memory before fetching/building; no large checkout was downloaded for this research. [Chromium macOS build instructions](https://github.com/chromium/chromium/blob/main/docs/mac_build_instructions.md)

Create an independent bundle ID, app data directory and Keychain identity. Never point a developer build at the active Arc/Chrome profile. Import into a new profile with a recoverable backup. Keep application data compatible across routine updates; record schema versions for custom Space metadata and test crash-safe migrations.

Signing and notarization are separate from compilation. Chromium's signing tooling requires the developer's own signing identity; a self-signed identity is incompatible with the library-validation option used by Chrome. Validate helpers, entitlements, Keychain access and TCC permission prompts in the final bundle. [Chromium macOS signing](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/chrome/installer/mac/signing/README.md), [Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)

## Trust boundaries and maintenance plan

Keep arbitrary pages in Chromium's renderer processes with the sandbox and Site Isolation intact. The browser process owns trusted UI, profile state, permissions and privileged actions. Spaces are organizational metadata; they are not a credential boundary unless explicitly assigned separate Chromium Profiles. Preserve native permission prompts and the upstream checks between processes. [Chromium process architecture](https://www.chromium.org/developers/design-documents/multi-process-architecture/), [Site Isolation](https://www.chromium.org/Home/chromium-security/site-isolation/)

Recommended release discipline:

1. Track the upstream stable release/security feed and pin each build to an identifiable upstream commit. Never describe a build as current merely because it compiles. [Official Chrome release feed](https://chromereleases.googleblog.com/)
2. Keep custom browser modules separate and the patch series small. Rebase and build the next upstream version before adding unrelated features.
3. Run focused upstream/browser tests plus custom Space persistence, extension, native-messaging and permission tests. Verify the package on a clean test profile.
4. Produce a signed release with a versioned manifest, integrity verification, and an update path that preserves profiles. Exercise an actual update and failure recovery before daily-use approval.
5. Make engine version and update failures visible. If security maintenance is abandoned, do not continue treating the resulting frozen build as a supported daily browser.

If Electron were revisited after changing the extension requirement, its shell would additionally need strict privileged-IPC boundaries, remote Node integration disabled, context isolation and sandboxing enabled, explicit permission handling, and current framework updates. Those duties are documented by [Electron security guidance](https://www.electronjs.org/docs/latest/tutorial/security); `WebContentsView` alone is not a secure general browser product.

## Binary feasibility gates before committing to the full UI

- Unmodified pinned Chromium builds and launches on the target Mac with independent app data.
- The selected stable source's existing vertical-tab UI is mapped and its relevant macOS behavior verified.
- The actual required extension inventory installs from the store, persists and updates under the custom identity.
- The actual password manager works, including native unlock/biometrics if required, in a signed build.
- Representative websites, OAuth, WebAuthn/passkeys, camera/microphone, downloads, PDF/print and required media work.
- A second upstream version can be merged, built, signed and installed while preserving the test profile and custom data.

These gates determine whether this is a viable personal daily browser. A static mock, engine demo or successful source build cannot substitute for them.
