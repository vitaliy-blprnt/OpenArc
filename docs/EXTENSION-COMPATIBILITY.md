# Extension compatibility gate

OpenArc must preserve the full upstream Chromium extension platform. This matrix
is a validation plan, not an extension allowlist and not a compatibility claim.

Every recorded result must identify the OpenArc commit, Chromium revision,
macOS version, app identity/signature, extension version, test action, and actual
result. Keep personal extension inventories, browsing URLs, credentials, and test
profiles out of the public repository.

## Required coverage

| Surface | Required behavior | Current evidence |
| --- | --- | --- |
| Chrome Web Store | Normal install, permission prompt, extension ID/signature retained | Not tested |
| Update lifecycle | Observe a real extension update; preserve storage/settings and enabled state | Not tested |
| Content scripts | Injection and granted/revoked site access behave like upstream | Not tested |
| Background workers | Start, suspend, wake, message, and restart without lost state | Not tested |
| Storage | Local/session storage and supported managed/sync behavior; service dependencies documented | Not tested |
| Browser surfaces | Toolbar action, popup, options, side panel, context menu, commands | Not tested |
| Tabs/windows | Create, query, activate, move, close, pin, popup, and multiple-window events | Not tested |
| Bookmarks | Create, rename, move, delete, and query reconcile with saved sidebar entries | Not tested |
| Sessions | Closed tabs/windows restore without duplicate workspace associations | Not tested |
| Tab groups | Upstream group API behavior remains distinct from Spaces and folders | Not tested |
| Downloads | Extension-triggered downloads retain prompts and history | Not tested |
| Network rules | The pinned upstream version's request/filtering APIs and permission model | Not tested |
| DevTools | Panels, inspected-window integration, and developer workflows | Not tested |
| Identity | Website OAuth and extension identity callbacks, including popup windows | Not tested |
| Native messaging | Host discovery, extension allowlists, process lifecycle, and signed-app trust | Not tested |
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
