# Extension compatibility gate

OpenArc must preserve the full upstream Chromium extension platform. This matrix
is a validation plan with bounded observed results, not an extension allowlist
or a promise of universal compatibility.

Every recorded result must identify the OpenArc commit, Chromium revision,
macOS version, app identity/signature, extension version, test action, and actual
result. Keep personal extension inventories, browsing URLs, credentials, and test
profiles out of the public repository.

## Recorded checkpoint

On 4 October 2026, the seven-patch OpenArc component build compiled after 29 final
incremental steps and ran as Chromium **154.0.8037.98 ARM64**, locked source
`b859317bf11f6be47f9b7799ec690a0a42a1fb33`, with bundle `org.openarc.browser`
and isolated `.build/profiles/development/Default` profile. Native UI verified
its exact executable and vertical tabs; Example Domain rendered without feature
override flags. The MV3 fixture **0.3.0** passed all **22 automated checks** and
the exact nonce/pong protocol 1 synthetic native exchange. All six manual
surfaces passed: popup rendering/dashboard, the browser's Options entry,
user-gesture side-panel rendering/dashboard from a webpage, and the visible
page context-menu dashboard action.

uBlock Origin Lite **2026.930.1227**, ID
`ddkjiahejlhfcafbddmgiahcphecmpfh`, installed from the official Chrome Web Store
through its native permission prompt in the four-patch build. The same version,
enabled state and Chrome Web Store source persisted after restart into the
seven-patch build. Its popup and options were observed; update delivery and
filtering behavior remain untested. The synthetic native host was unregistered.

Ignored final fixture evidence:
`.build/reports/openarc-extension-probe-seven-patch.json`, run
`e3e63c90-1b03-431a-9e33-45d7ddbe37dd`. Manual observations are recorded
separately in `.build/reports/openarc-ui-observations-seven-patch.json`; the
owned browser exited after testing. M1 remains incomplete. A development
component build does not qualify signed release identity or password-manager
vendor trust. Bounded model preparation may proceed; full compatibility and
large custom-shell acceptance still require the gates in
[BROWSER-PLAN.md](BROWSER-PLAN.md).

## Required coverage

| Surface | Required behavior | Current evidence |
| --- | --- | --- |
| Chrome Web Store | Normal install, permission prompt, extension ID/signature retained | uBlock Origin Lite install/native prompt observed in four patches; expected ID, version, enabled state and Store source retained after seven-patch restart; no separate signature audit |
| Update lifecycle | Observe a real extension update; preserve storage/settings and enabled state | Restart retention observed for uBlock Origin Lite; real update and storage/settings preservation untested |
| Content scripts | Injection and granted/revoked site access behave like upstream | Seven-patch fixture: packaged injection on its owned example.com tab passed; permission changes pending |
| Background workers | Start, suspend, wake, message, and restart without lost state | Seven-patch fixture: worker challenge/response passed; suspend/wake and stateful restart lifecycle pending |
| Storage | Local/session storage and supported managed/sync behavior; service dependencies documented | Seven-patch fixture: local/session round trips passed; managed/sync and storage restart persistence pending |
| Browser surfaces | Toolbar action, popup, options, side panel, context menu, commands | Seven-patch fixture: all six manual surfaces observed; menu registration and panel configuration passed. uBlock Origin Lite popup/options also observed; commands pending |
| Tabs/windows | Create, query, activate, move, close, pin, popup, and multiple-window events | Seven-patch fixture: synthetic window/tab operations, pin order, and move/activation events passed; broader window/popup coverage pending |
| Bookmarks | Create, rename, move, delete, and query reconcile with saved sidebar entries | Seven-patch fixture: synthetic bookmark/folder CRUD and moves passed; saved-sidebar reconciliation not implemented |
| Sessions | Closed tabs/windows restore without duplicate workspace associations | Seven-patch fixture: its own closed tab restored; window restore and workspace integration pending |
| Tab groups | Upstream group API behavior remains distinct from Spaces and folders | Seven-patch fixture: synthetic pair metadata, membership and ungroup round trip passed; Space integration pending |
| Downloads | Extension-triggered downloads retain prompts and history | Not tested |
| Network rules | The pinned upstream version's request/filtering APIs and permission model | Not tested |
| DevTools | Panels, inspected-window integration, and developer workflows | Not tested |
| Identity | Website OAuth and extension identity callbacks, including popup windows | Not tested |
| Native messaging | Host discovery, extension allowlists, process lifecycle, and signed-app trust | Seven-patch fixture: registered synthetic host returned exact nonce/pong protocol 1; signed-app and vendor trust pending |
| Password managers | Unlock, save, fill, generate, lock, restart, desktop integration, relevant passkeys | Not tested |
| Private windows | Explicit extension permissions and no private workspace persistence | Not tested |
| Upstream upgrade | Repeat the matrix after changing the pinned Chromium version | Not tested |

## Workspace-specific assertions

- A live page is a real Chromium tab. A saved entry with no live page is not a tab.
- Extension tab IDs are session-scoped and cannot serve as durable workspace IDs.
- Switching Spaces changes sidebar presentation without inventing window IDs or
  synthesizing tab-close events.
- Activating a tab through an extension reveals its Space in its owning window.
- Tabs created with an opener inherit the opener's Space within the same Profile;
  otherwise they use the target window's active Space.
- Closing a saved page emits normal close events and retains its saved entry.
- An extension's allowed view of tabs is not restricted by sidebar visibility.
- Preserve upstream index, pin-order, tab-group, and active-tab permission rules.

## Acceptance

First validate the unmodified pinned baseline, then the OpenArc identity and
interface patches. Exercise representative extension classes and the complete
available owner-required inventory; never present the sample as the supported
product boundary. Unpacked fixtures can diagnose API behavior but do not pass
the Store installation/update gate. Installing a password-manager extension
does not pass its signed native-integration gate.

Vendor or Google-service restrictions remain explicit unresolved items. A finite
test matrix cannot prove every third-party extension, and no passing result
should be generalized beyond the build and behavior it actually exercised.

Sources: [Chrome extension documentation](https://developer.chrome.com/docs/extensions/),
[tabs API](https://developer.chrome.com/docs/extensions/reference/api/tabs),
[native messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging),
[1Password additional browsers](https://support.1password.com/additional-browsers/).
