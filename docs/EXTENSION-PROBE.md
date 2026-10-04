# Extension platform probe

The [unpacked MV3 fixture](../tests/extensions/platform-probe/manifest.json) runs
a bounded set of real extension API checks against a browser you have built.
No browser execution is implied by its presence in the repository. A static
check or a passing fixture run does not establish Web Store compatibility,
native password-manager integration, or support for every Chrome extension.

## Use an isolated profile

Use only an isolated development browser profile, such as the profile created by
the repository launcher. Do not load this diagnostic in your daily browsing
profile. It needs the `tabs`, `bookmarks`, `sessions`, `storage`, `scripting`,
`contextMenus`, `sidePanel`, and `nativeMessaging` permissions. The only web host permission is
`https://example.com/*`; there is no blanket host access, cookies permission,
remote script, or evaluation of downloaded code.

The launcher's unmodified baseline mode uses a mock Keychain with synthetic data.
Results in that mode cannot qualify credential storage, signed-app identity, or
native password-manager trust. The normal OpenArc mode and final signed app need
their own integration checks.

Nothing runs on installation except registering a harmless, example.com-scoped
context-menu action that opens the dashboard. Checks require a dashboard click
and confirmation that the profile is isolated.

## Load and run

1. Build and launch the actual Chromium/OpenArc development application using
   [BUILDING.md](BUILDING.md). Record its OpenArc commit, locked Chromium revision,
   architecture, and build configuration.
2. Open `chrome://extensions`, enable Developer mode, choose **Load unpacked**,
   and select `tests/extensions/platform-probe` from this repository. Record
   unpacked loading separately from store installation.
3. Open the extension's toolbar action and select **Open dashboard**. The
   extension's **Options** entry opens the same page. If the action is hidden,
   use the browser's extensions menu to show it. The loaded extension's ID also
   permits direct navigation to `chrome-extension://<id>/dashboard.html`.
4. Confirm the isolated-profile checkbox and press **Run checks**. Leave the
   synthetic window and bookmarks alone until it finishes. example.com access
   requires a working network connection; a network failure is a failed check.
5. Read every result. **Blocked**, **not run**, and **fail** are incomplete
   evidence, never success. Download the report if needed; it contains generated
   fixture information, API results, timestamps, and the browser's user-agent
   string. Record the exact build revision alongside it.

Use the dashboard's separate manual checklist for the actual toolbar popup,
Options entry, visible context menu, and side-panel rendering. The side-panel
configuration check only checks API configuration. Use **Open side panel** in
the popup to exercise its required user gesture; observe the native surface.
Those observations are not automatically marked as passed by the report.

## Optional synthetic native messaging

This separate check exercises the project's own minimal native host. It does not
test any password manager, trusted-vendor integration, biometrics, secrets, or
credential storage. It is excluded from the default **Run checks** suite and does
not install a host automatically.

From the same repository root, install the host for the isolated browser mode
you are actually using. Copy the fixture's current 32-character extension ID from
`chrome://extensions` and replace the placeholder below:

```sh
python3 scripts/native_probe.py install --baseline --extension-id YOUR_EXTENSION_ID
```

For the normal development mode, substitute `--development` for `--baseline`.
Those modes target `.build/profiles/baseline/NativeMessagingHosts` and
`.build/profiles/development/NativeMessagingHosts` respectively. The optional
`--reference` mode targets only
`.build/profiles/fixture-reference/NativeMessagingHosts`. No mode registers a host
in system or daily-use browser locations. Host discovery in the actual built
browser still needs verification.

To validate the fixture against a separately installed Chromium-based reference
browser, launch it with this repository's absolute
`.build/profiles/fixture-reference` path as its dedicated `--user-data-dir` and
`--use-mock-keychain` for synthetic data. Verify the command line and profile
path in `chrome://version` before loading the fixture. Then use `install --reference` and
`uninstall --reference` with the fixture ID from that profile. Record the actual
reference-browser version separately. A passing reference-browser exchange
validates only the fixture's synthetic transport in that browser; it does **not**
qualify OpenArc, its signed identity, or password-manager vendor integration.

On the dashboard, click **Check synthetic native host**. A pass requires exactly
`{type: "pong", nonce: <the request nonce>, protocol: 1}` from
`org.openarc.platform_probe`. A missing host is **blocked**; malformed replies,
API failures, and timeouts do not pass. The fixture opens one native port, sends
one challenge, accepts only the first reply, and disconnects on every outcome,
including the 10-second timeout. The result is stored separately in the
downloadable report. Run this after the default suite if you want both in one
report; a new default run starts a new report.

Remove the fixture host when finished, using the same mode and extension ID:

```sh
python3 scripts/native_probe.py uninstall --baseline --extension-id YOUR_EXTENSION_ID
```

Chrome documents [native messaging](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging)
as a browser-to-native-process protocol with allowed extension origins. Observing
this synthetic exchange proves only that transport path in the tested build.

## What the automated run exercises

| Check | Fixture boundary |
| --- | --- |
| Worker messaging | Dashboard challenge and response from the MV3 service worker |
| Local/session storage | Unique extension-owned key round trip, then key removal |
| Context menus | Create/update/remove one uniquely named item; visible behavior remains manual |
| Side panel | Read the packaged panel's configured path and enabled state |
| Tabs | Create a separate fixture window and nonce-labeled pages; query that label, move, navigate, mute, and remove recorded fixture IDs |
| Bookmarks | Create a uniquely labeled tree; create/move a bookmark and folder, rename a folder, remove the bookmark and empty fixture folders |
| Content injection | Inject a packaged function into only the nonce-bearing example.com tab, verify a temporary DOM marker, remove that marker |
| Sessions | Close the fixture's example.com tab and restore only its verified session ID |
| Cleanup | Remove recorded, unchanged fixture resources; preserve unexpected content |

The sessions API cannot filter recently closed entries by extension ownership.
The probe asks for only the newest entry, compares its tab URL to this run's exact
unique fixture URL, and restores only on a match. Concurrent tab closing or an
unexpected window entry makes the result **blocked**; nothing else is restored.
Unrelated entry fields are not logged, stored, or displayed. This is another
reason to use an isolated profile.

## Cleanup and interrupted runs

An extension-local journal records creation intents before API calls, then
resource IDs and ownership markers. A lock shared by the extension's pages
prevents concurrent dashboards from overwriting that journal. Cleanup
checks the marker before modifying a tab and checks ID, parent, title, and URL
before removing a bookmark. It removes folders only when empty, never using
recursive deletion. It never closes an entire window, so a user-added page is
not removed along with the fixture. Do not navigate fixture tabs or edit its
bookmark tree while tests run.

If interrupted, reopen the dashboard and select **Clean up unfinished fixture**.
Changed resources are preserved and reported; resolve those manually in the
isolated profile. An invalid journal disables automatic cleanup. If interrupted
between API creation and recording the returned ID, recovery queries only that
intent's unique fixture title or URL and verifies ownership before recording it.
An ambiguous or unconfirmed result retains the journal and leaves cleanup
incomplete. Inspect the nonce-labeled fixture manually; remove and reload the
extension only after resolving unfinished resources. Recovery never enumerates
general tabs or a full bookmark tree. Exact roundtrip storage keys are also
retried during cleanup.

Browser API errors can contain an unrelated URL if a fixture page was navigated
during a test. The report replaces raw API exceptions with fixed failure text;
only controlled fixture assertions provide detailed messages.

The latest report and unfinished journal remain in the extension's own local
storage. Synthetic example.com history and recently closed entries can remain;
the probe does not clear history or unrelated sessions. Remove the unpacked
extension after testing and dispose of the dedicated test profile when done.

## Checks that do not launch a browser

The scope guards have dependency-free Node tests:

```sh
node --test tests/extensions/platform-probe/scope.test.mjs
```

They verify rejection of foreign/changed resources and cleanup ordering. They
do not execute Chrome APIs. JavaScript parsing and manifest validation are also
static checks; only an actual dashboard run supplies browser API evidence.

## Official API references

The fixture follows Chrome's MV3 APIs for [tabs](https://developer.chrome.com/docs/extensions/reference/api/tabs),
[bookmarks](https://developer.chrome.com/docs/extensions/reference/api/bookmarks),
[sessions](https://developer.chrome.com/docs/extensions/reference/api/sessions),
[storage](https://developer.chrome.com/docs/extensions/reference/api/storage),
[worker messaging](https://developer.chrome.com/docs/extensions/develop/concepts/messaging),
[script injection](https://developer.chrome.com/docs/extensions/reference/api/scripting),
[context menus](https://developer.chrome.com/docs/extensions/reference/api/contextMenus),
and [side panels](https://developer.chrome.com/docs/extensions/reference/api/sidePanel).
The declared minimum version is 123 because the fixture uses the documented
Promise variants of context-menu methods. Test against the project's locked
Chromium build; API documentation alone is not a compatibility result.
