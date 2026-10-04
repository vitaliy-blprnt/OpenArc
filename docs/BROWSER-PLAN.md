# OpenArc: research and implementation plan

Prepared 3 October 2026. Implementation authorized. This document is the product and milestone contract; current completion evidence is in [STATUS.md](STATUS.md).

## Direction

Build OpenArc as an open-source, macOS-first browser and a small, maintained fork of Chromium's browser application. Start with the owner's personal workflow while making the project buildable and maintainable by others. Keep Chromium's rendering, navigation, extension, profile, permission, and security machinery. Change the browser interface and add a durable model for Spaces and saved tabs.

The intended experience is a left sidebar with permanent saved tabs above a divider and ordinary open tabs below it. Saved tabs open in place. Spaces provide separate working contexts. Browser AI is outside the product scope; complete no-AI acceptance has not yet been established.

### Confirmed requirements

- Personal browser, based on Chromium.
- Project name: OpenArc.
- Open-source project, with public source and build instructions as intended deliverables.
- macOS first.
- Arc is the reference for tabs, Spaces, and the left sidebar.
- Bookmarks behave like Arc's saved tabs: selecting one opens or focuses it in its permanent position, rather than adding a duplicate below.
- Full Chrome extension support is a requirement, including password managers. A small supported-extension list does not satisfy this requirement.
- AI is outside the product scope.

### Proposed defaults

These are recommended product defaults: local data with no browser account; optional profile-wide Favorites; light/dark appearance following macOS; automatic tab archiving off; no cloud sync in the first release. Implementation begins on the verified Apple Silicon host, and BSD-3-Clause has been adopted for original OpenArc code. The canonical repository is [vitaliy-blprnt/OpenArc](https://github.com/vitaliy-blprnt/OpenArc). The icon, exact styling, Intel support, minimum macOS version, and binary distribution method remain to be selected during implementation.

## What the Arc research tells us

The factual source inventory and platform caveats are in [Arc interaction research](research/arc-interactions.md). The engineering sources and alternatives are in [Chromium foundations](research/chromium-foundations.md).

Arc organizes its sidebar around how long something should stay useful: profile-wide Favorites at the top, saved tabs and folders for the current Space in the middle, and temporary tabs below a divider. Its saved tabs preserve an original URL even after navigation. Spaces organize activity; Profiles separate browsing identity and state. These are distinct responsibilities. [Favorites](https://resources.arc.net/hc/en-us/articles/19230755904151-Favorites-Top-Tabs-Across-Every-Space), [Pinned tabs](https://resources.arc.net/hc/en-us/articles/19231060187159-Pinned-Tabs-Tabs-you-want-to-stick-around).

Visual inspection of Arc's official screenshots shows a narrow sidebar, small navigation controls, full text labels for tab rows, a strong selected row, restrained folder indentation, a bottom Space switcher, and a large content pane framed by the Space's color. Color gives context; the page remains the dominant surface. These are observations of published screenshots, not measurements of a running Arc installation. [Spaces screenshot](https://arc.net/_next/image?q=100&url=%2Fspace-swiping.png&w=3840), [theme and split screenshot](https://arc.net/_next/image?q=100&url=%2Ftheme-picker.png&w=3840).

Adopt the clear separation of saved and temporary tabs, persistent ordering, Space switching, a keyboard command bar, and a collapsible sidebar. Treat split view and link previews as later enhancements. Do not reproduce Arc's AI, Easels, Boosts, cloud sharing, or automatic integrations.

Arc's homepage currently says it receives Chromium updates only. Describe it as having limited ongoing maintenance rather than assuming all updates have stopped. The custom browser must own an explicit update process. [Arc's current product page](https://arc.net/).

## Proposed interface

This is an information layout, not a finished visual design:

```text
┌──────────────────────────┬──────────────────────────────────────┐
│ window / back / forward  │                                      │
│ address + site controls  │                                      │
│ optional Favorites       │                                      │
│                          │                                      │
│ Work · Personal profile  │                                      │
│ ▾ Saved                  │                                      │
│   GitHub                 │              Web page                │
│   ▾ Project              │                                      │
│     Documentation        │                                      │
│     Issue tracker        │                                      │
│ ──────────────────────── │                                      │
│ + New tab                │                                      │
│   Article                │                                      │
│   Search results         │                                      │
│                          │                                      │
│ Spaces / + / utilities   │                                      │
└──────────────────────────┴──────────────────────────────────────┘
```

Use a resizable sidebar, initially around 260 logical pixels as a prototype starting point. Keep the divider and new-tab action reachable with long saved lists: the saved section can collapse and has a bounded scroll area, while the open-tab area retains usable height. Keep Space switching anchored at the bottom. Extension actions, downloads, site identity, permissions, and the main menu must remain discoverable even when the sidebar is hidden.

Tabs need selected, loading, unloaded, audio, muted, crashed, and attention states. Full URL and security information must be readily available; a compact sidebar must not obscure the current origin. Use readable system typography, full labels, keyboard focus rings, accessible names for icon controls, reduced-motion support, and both light and dark themes. A Space's name/icon accompanies its color.

### Interaction contract

| Action | Proposed behavior |
| --- | --- |
| Open a new URL or search | Create an ordinary tab in the current Space. |
| Click a saved entry | Focus its existing tab in this window, or lazily create it at its saved URL. Keep its row above the divider. |
| Navigate inside a saved tab | Change its current page/history; preserve the saved URL and custom label. |
| Return to saved page | Explicit action restores the saved URL. Updating the saved URL is a separate action. |
| Close a saved tab | Close the live page through normal Chromium close handling; retain the saved entry. Next activation opens its saved URL. |
| Remove a saved entry | Remove the durable pin with Undo. If its page is open, keep it as an ordinary tab to avoid losing work. |
| Drag an ordinary tab above the divider | Save its current URL and move the existing tab into the saved section without reloading. |
| Drag a saved entry below the divider | Unpin it and keep or open its page as an ordinary tab. |
| Close an ordinary tab | Remove it from the active list; support Chromium's recently closed/reopen flow. |
| Switch Space | Show that Space's saved entries and open tabs; restore its last selected page. Existing pages continue their normal lifecycle. |
| Move a tab to a Space in the same Profile | Move the live tab without reloading or changing cookies. |
| Move to a different Profile | Open the URL in that Profile's own session. Do not transfer cookies or a live renderer; preserve the original until explicitly closed. |
| Delete a Space | Offer to move its contents to another Space, with a recoverable path; do not silently discard pages or saved items. |
| Open a popup / extension-created tab | Retain Chromium's real tab/window semantics. Use the opener's Space when an opener exists in the same Profile; otherwise use the target window's active Space. Preserve dedicated popup/OAuth windows. |

Recommended shortcuts: Cmd+T opens the command bar for new navigation, tab search, and commands; Cmd+L edits the current URL; Cmd+W closes the current live page; Cmd+Shift+T reopens a closed page; Cmd+D toggles saving; Cmd+S retains Save Page; Cmd+Comma opens Settings. Add discoverable Space and sidebar shortcuts after checking macOS and extension shortcut conflicts.

Automatic archiving is optional and off by default. If added, it is recoverable and must exclude active media, capture, downloads, and pages with pending work. Unloading a renderer to save memory is different from closing or archiving a tab.

## Foundation and boundaries

### Open-source project structure

Recommend a public OpenArc source repository containing original browser modules, a small ordered Chromium patch series, build scripts, a pinned upstream/dependency manifest, tests, and documentation. Fetch upstream Chromium through documented tooling rather than committing generated builds or a copied source snapshot without history. This is still a Chromium browser fork; the repository layout keeps OpenArc's changes easy to inspect and update.

Proposed layout:

```text
README.md / LICENSE / THIRD_PARTY_NOTICES
CONTRIBUTING.md / SECURITY.md
upstream.lock                 # exact Chromium revision and build inputs
src/openarc/                  # original browser-layer modules
patches/chromium/              # small ordered integration patches
scripts/                      # fetch, apply, build, package, update checks
tests/                        # model and browser integration coverage
docs/                         # UX contract, architecture, build and release guides
.github/workflows/            # public checks and protected release jobs
```

BSD-3-Clause is adopted for original OpenArc code in [LICENSE](../LICENSE). Preserve Chromium's and all bundled dependencies' own licenses and notices. Arc is a behavior/design reference, not a source-code or asset dependency; create OpenArc's own icon and visual assets.

Anyone should be able to build the development application from public source and documented dependencies without maintainer secrets. Official signed macOS releases are a separate pipeline; keep signing, notarization, update-signing credentials, and release service tokens outside the repository. Untrusted pull requests must not run with release secrets. Document native-integration differences for unsigned local builds rather than bypassing vendor trust checks.

Include contribution instructions, focused checks, issue templates, an upstream-update guide, a private security-reporting route, and a maintainer/release policy. Record source revisions and build inputs with every release. Community contributions should preserve the no-AI browser scope, extension platform, data compatibility, and small upstream patch boundary. General distribution introduces additional packaging, updater, and support checks; a personal installation is not proof of a qualified public release.

Repository publication and release credentials are tracked as separate execution gates in [STATUS.md](STATUS.md).

### Recommendation: a small Chromium fork

Use Chromium's existing C++ browser layer and Views interface, with Objective-C++ at macOS integration seams where needed. Reuse available vertical-tab components after verifying them at the chosen stable revision. Research found vertical-tab code on current Chromium HEAD; that does not establish availability or maturity on the future pinned release. [Chromium foundations](research/chromium-foundations.md).

Avoid a separate SwiftUI browser shell in the first implementation. It introduces an additional bridge for tab ownership, focus, accessibility, extension popups, permission prompts, fullscreen, and window behavior. A Views-based interface can still be designed around the macOS experience.

| Foundation | Fit for this request |
| --- | --- |
| Chromium browser fork | Recommended. Preserves the upstream extension/browser platform and allows integration with the real tab and session models. Requires regular upstream updates and a controlled patch set. |
| CEF with Chrome runtime | Credible alternative, not simply an extension-free renderer. Would need proof that its embedding interfaces support the intended custom sidebar while preserving required browser UI and extension behavior. Less direct access to the browser model than a fork. |
| Electron shell | Fails the full-extension requirement as documented: Electron supports a subset and explicitly does not target arbitrary Chrome Web Store extensions or full Chrome parity. [Electron extension support](https://www.electronjs.org/docs/latest/api/extensions). |

Keep Blink, V8, the sandbox, site isolation, networking, certificates, and extension security behavior upstream. Browser UI customization must not weaken these boundaries. Retain upstream site-permission prompts, extension permission prompts, and security interstitials.

Proposed modules:

- **Workspace service:** profile-scoped Spaces, saved entries, folders, ordering, and versioned metadata. It is the authority for organization.
- **Tab adapter:** attaches stable workspace IDs to real Chromium tabs and observes creation, navigation, close, transfer, restore, and extension-driven events. Do not create a second browsing engine or fictitious tab registry.
- **Sidebar and commands:** Views presentation over those services; command execution reuses browser commands where possible.
- **Persistence and recovery adapter:** integrates workspace IDs and ordering with Chromium session restore; owns schema migration, backup, and reconciliation.
- **Release tooling:** pinned upstream revision, deterministic patches, build metadata, signing, packaging, update verification, and upgrade tests.

Prefer Chromium's bookmark model for saved URL/title/folder data, extending it with Space association where practical. First prove ordering and restore semantics. If extra workspace metadata needs its own store, use a profile-scoped versioned store and stable IDs. Do not maintain unrelated authoritative copies of bookmarks or tabs. Chromium remains the owner of cookies, history, password storage, extension storage, and actual navigation sessions.

### Data and window semantics

| Record | Responsibility |
| --- | --- |
| Profile | Real Chromium isolation boundary for cookies, extensions, storage, and identity. |
| Space | Stable ID, owning Profile, label/icon/theme, and saved organization. |
| Saved entry | Stable ID, Space or profile-wide Favorite scope, saved URL, optional custom title, folder, and position. Independent of whether a live tab exists. |
| Open tab association | Real Chromium tab/session identity plus Space and optional saved-entry ID. |
| Window session | One Profile, current Space, current selected tab per Space, and window-owned live tabs. |

Recommended multiwindow policy: saved organization is shared within its owning Profile/Space; live tabs belong to a window. A saved entry may have one live instance per window. Opening it in a second window creates that window's instance, while metadata changes update both sidebars. This avoids pretending one WebContents can be displayed in two windows. Moving a live tab between windows transfers ownership and preserves its Space when both windows share a Profile. If the destination already has a live instance of that saved entry, retain the destination's association and make the transferred page an ordinary tab without reloading; preserve both pages.

Hidden Spaces are an organizational filter, not an extension security boundary. Extensions with the corresponding permissions must still see the real tabs in their browser window. The UI's hidden state must not produce fake closes or invalid tab IDs. A saved entry that is not open must not appear as a phantom `chrome.tabs` tab. Tab activation by an extension must reveal the tab's Space.

Extension tab IDs are session-scoped, so they are not durable workspace keys. Define the adapter's handling of extension-driven create, move, activate, close, and pin/unpin before implementing drag and drop. Preserve actual indices, window IDs, events, pinned-tab ordering constraints, and active-tab permissions. Do not implement Spaces as invisible browser windows or repurpose `chrome.tabGroups` as Spaces/folders. Specify how upstream groups appear in the sidebar while preserving their API behavior. [Chrome tabs API](https://developer.chrome.com/docs/extensions/reference/api/tabs).

Private windows use Chromium's off-the-record profile behavior and ephemeral workspace metadata. They must not persist private URLs through restore records, exports, or the ordinary workspace service. Following Chromium's permission model for extensions in private windows is mandatory.

### Extension requirement and limits of proof

Full support means keeping the upstream Chrome extension platform, its permission model, and normal extension UI, not implementing selected APIs. Verify Web Store installation and update behavior in the actual branded build rather than assuming source compatibility proves delivery compatibility.

The compatibility gate covers content scripts, background service workers, storage, toolbar actions and popups, side panels, context menus, shortcuts, downloads, tab/window APIs, bookmarks, sessions, DevTools, network filtering under the selected upstream version, native messaging, and identity flows. Include multiple extension classes and all extensions the owner actually installs; the sample suite is a regression tool, not a product allowlist. Extension-driven bookmark create/rename/move/delete and session restore must reconcile saved entries immediately, without stale rows or duplicate live associations.

Literal proof that every extension works is not possible from documentation or a finite test suite. Vendor rules, deprecated APIs, Google-only services, and signed native-app trust can affect individual extensions even when the upstream platform is intact. A gap stays visible and blocks the full-support acceptance claim; it must not be silently reclassified as out of scope. Password-manager native unlock and passkeys need separate checks from installing the extension. [Primary-source details](research/chromium-foundations.md).

Google account sync, Google API entitlements, Safe Browsing service configuration, DRM/Widevine, proprietary codecs, Chrome branding, and update infrastructure are separate from having Chromium source. Inventory and verify the required capabilities at the selected build. No Chrome sync promise and no claim of DRM playback until tested. Keep license notices and use an original browser identity. [Primary-source details](research/chromium-foundations.md).

## Dependency-ordered implementation milestones

Each milestone produces a reviewable checkpoint and evidence. Start gates permit
bounded work; acceptance gates determine when a milestone is complete. Record
the pinned upstream revision, exact build, test outputs, unresolved defects, and
decision changes in [STATUS.md](STATUS.md). M0 is complete for the documented
local development build. The seven-patch build and extension fixture provide a
working basis for bounded M3 model work, while M1 and M2 acceptance remain open.
Starting that work does not qualify signing, vendor trust, update delivery, or a
large custom shell.

### M0 — Open-source foundation and macOS baseline

**Requires:** this plan. **Read:** both research notes and the official Chromium macOS build instructions linked there.

**Change:** select the repository owner, establish the OpenArc source layout, adopt a license for original code with upstream notices, and publish the source repository after checking it for credentials and private data. Choose the stable upstream ref, inspect its vertical-tab implementation, record hardware/toolchain/disk needs, create fetch/build/patch scripts, and establish an OpenArc app identity and development data directory. Build unmodified Chromium first. Never point development builds at an existing Chrome or Arc profile.

**Done when:** the public source repository has license/attribution files and a clear pre-release status; a clean checkout produces a launching browser with the normal sandbox and profile behavior using documented public build inputs and no maintainer secrets; its exact revision/configuration is recorded; patches can be applied and reversed reproducibly; reusable vertical-tab seams are documented from that ref.

### M1 — Prove the extension foundation

**Requires:** M0. **Read:** the extension/native-messaging sources in the foundation note and the selected revision's extension implementation.

**Change:** establish the intended app bundle/signing identity; validate Store installation, updates, prompts, restart persistence, extension UI, and the API classes listed above. Exercise native messaging and supported password-manager trust configuration without masquerading as Google Chrome. Record service-dependent failures explicitly.

**Done when:** the actual build passes the compatibility matrix, all currently available owner-required extensions are exercised, and every untested external dependency is named. Do not begin a large custom-shell investment if this gate exposes an unsatisfied core requirement. An unpacked-extension demonstration alone does not pass.

### M2 — Prove an upstream update can be delivered

**Start gate:** M0; source rehearsals and isolated candidate builds can proceed
alongside M1. **Acceptance dependency:** M1. **Read:** pinned build configuration
and release-tooling sources.

**Change:** add one minimal browser-UI patch, advance to another appropriate stable upstream revision, reapply it, rebuild, and rerun the extension baseline. Define how security releases are detected, reviewed, packaged, signed, and delivered. Add contribution/build guidance, a private security-reporting route, and CI checks with release credentials isolated from pull-request jobs.

**Done when:** an upstream upgrade is reproduced with recorded patch/rebuild results and the baseline still passes. The plan identifies who operates updates and how failed upgrades are recovered. Feature development must not make security updates impractical.

### M3 — Durable Space and saved-tab model

**Start gate:** M0 and an actual development-build extension fixture baseline.
The current seven-patch build satisfies this start gate. Bounded native model,
bookmark-observer, codec, reconciliation and isolated-test work can proceed
without waiting for signed vendor integration or upgrade delivery.
**Acceptance dependencies:** full M1 and M2 acceptance remain required before
declaring M3 complete or expanding into a large custom shell. **Read:**
interaction/data contracts above, Chromium bookmark, profile, tab, and session
code at the pinned ref.

**Change:** implement the workspace service, stable IDs, ownership rules, schema versioning, migration, and tab adapter. Define ordering across window sessions, extension-visible indices/pin/group behavior, and reconciliation after an interrupted save. Use transactional or journaled metadata changes for operations that span stores.

**Done when:** M1 and M2 are accepted and meaningful model/integration tests prove
saved URL/current URL separation, close versus remove, duplicate URLs,
reordering, multiple windows, saved-instance transfer collisions, extension
bookmark edits, restore reconciliation, failed migration recovery, and
private-session non-persistence without losing the previous recoverable state.
Standalone helper tests are evidence for their named modules, not this full gate.

### M4 — Working sidebar and saved tabs

**Start gate:** the relevant default-Space model invariants and core extension
regression baseline pass on the actual development build. This permits one
bounded Saved/open-section development slice using the existing vertical region,
toolbar and extension surfaces; it need not wait for external vendor credentials
or release delivery. **Acceptance dependencies:** broad custom-shell work and
full M4 acceptance still require accepted M1–M3. **Read:** verified vertical-tab
components and Arc visual/interaction research.

**Change:** show real tabs in the left sidebar; implement saved/open sections, saved-tab activation, closing/unpinning, folders, drag and drop, context menus, loading/audio/crash states, resize, collapse, and reachable extension actions.

**Done when:** real websites remain interactive while tabs are pinned, navigated, reset, closed, reopened, and reordered; saving a page produces no duplicate below the divider; normal unload/close behavior is preserved; long lists keep navigation and new-tab controls reachable.

### M5 — Spaces, Profiles, and multiple windows

**Requires:** M4. **Read:** ownership contract and upstream profile/window behavior.

**Change:** implement Space creation, rename, reorder, move, recoverable removal, active-state restoration, optional profile-wide Favorites, Profile assignment, and the defined window policy. Keep each window bound to one Profile; changing Profile opens/focuses the appropriate window.

**Done when:** three Spaces retain separate saved/open tab organization across restart; same-Profile Spaces share sign-in state; two Profiles do not share cookies or extensions; window transfer and extension-created/activated tabs behave correctly; private browsing leaves no workspace records.

### M6 — Keyboard navigation and browser essentials

**Requires:** M5. **Read:** Chromium omnibox/command infrastructure and macOS integration sources.

**Change:** add the command palette, tab search across Spaces, location editing, shortcut help, native menus, Settings access, downloads, history, find, zoom, PDF, printing, file upload, fullscreen, DevTools, media controls, external-link handling, and default-browser registration. Reuse upstream flows and error handling. Review the pinned revision's browser-owned AI surfaces and disable unwanted assistant UI/services/model downloads; this does not restrict the websites or extensions the owner chooses to use.

**Done when:** real browsing flows and extension surfaces work with keyboard and mouse in visible/collapsed-sidebar states; certificate and permission prompts identify the correct site; commands do not interfere with text fields or extension shortcuts. Registration support is tested without automatically taking over the user's current default browser.

### M7 — Migration, recovery, and data portability

**Requires:** M5–M6. **Read:** source-browser export formats and upstream session restore behavior.

**Change:** import standard bookmarks into saved folders/Spaces, export standard bookmarks plus a versioned workspace backup, add recently closed recovery, and implement lazy session restore and migration backups. For Arc, support its documented copy-links path first; any direct sidebar-file importer must be a separate version-checked, read-only adapter over an owner-provided copy.

**Done when:** interrupted writes and process termination recover coherent Spaces, order, selected tabs, and saved entries; valid fixtures round-trip; malformed imports fail without damaging existing data; original browser files remain unchanged. Do not import encrypted cookies/password databases by raw file copying.

### M8 — Visual and interaction qualification

**Requires:** M6–M7. **Read:** the approved sidebar contract and real-app screenshots from M4–M7.

**Change:** refine spacing, type, theme colors, content framing, hover/selection/focus, full-screen behavior, touchpad gestures, drag feedback, VoiceOver, reduced motion, and long-label/large-sidebar behavior. Profile startup, switching, scrolling, and memory against the unmodified baseline with the same fixture.

**Done when:** representative empty/large/error/private/multiwindow states pass visible macOS review; keyboard and VoiceOver can reach every control; status does not depend on color alone; performance traces identify no unresolved interaction regression. A web mock does not substitute for testing the browser application.

### M9 — Public release and maintenance acceptance

**Requires:** M1–M8. **Read:** release process, compatibility matrix, migration/restore evidence.

**Change:** tag the release source and verify its license/notices, build instructions, contribution guide, and security policy. Package/sign the macOS app, notarize for the chosen public macOS distribution channel, wire a signed/verifiable update mechanism, preserve update/migration backups, and expose installed engine/build/update status. Test updating OpenArc across another upstream change with the finished feature set. Verify that no credentials or private browser data appear in source, CI logs, or release artifacts.

**Done when:** the public source can be built from its documented inputs; its license/notices and security/contribution routes are available; installation and update preserve data; tampered updates are rejected; interrupted updates recover; schema-incompatible binary rollback cannot open newer data without a compatible backup; the extension suite passes in the signed installation; ordinary web workflows, media/DRM requirements, crash restore, private sessions, and security service configuration are documented as passed or unresolved. Daily-use acceptance and public-release qualification are recorded separately. Neither is claimed until its required gaps are resolved.

## Later additions

After the core browser and maintenance process pass: split view, explicit Peek previews, per-domain Space routing, optional recoverable auto-archive, richer appearance controls, and possibly device sync. Each adds state and recovery cases, so none belongs in the first foundation spike. Windows/Linux follow only when their own platform integration and extension gates can be run.

The first implementation deliverable should be a reproducible Chromium build with verified extensions and an upstream update rehearsal. The custom sidebar then grows on that proven foundation.
