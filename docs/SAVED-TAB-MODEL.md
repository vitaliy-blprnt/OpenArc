# Proposed saved-tab and Space model

**Status: implementation contract only.** This work belongs to M3 and the first
M4 slice. It is pending the M0–M2 build, extension, and upstream-update gates in
[BROWSER-PLAN.md](BROWSER-PLAN.md). No module below is implemented or qualified by
this document. A reference Chrome test is useful fixture evidence, not an
OpenArc acceptance result.

This contract follows the plan's profile/window ownership rules and the pinned
source seams in [CHROMIUM-INTEGRATION.md](CHROMIUM-INTEGRATION.md#workspace-integration-seams).
The implementation changes browser organization and Views presentation. Chromium
continues to own real tabs, navigation, sessions, bookmarks, and extension APIs.

## Authority and identity

The following durable authorities apply to regular profiles.

| State | Authority and representation |
| --- | --- |
| Spaces | One versioned, profile-local workspace record: ordered `SpaceId`, display label, icon, theme, and saved-root reference. Its label is independent of a bookmark folder's title. |
| Saved destination and label | A real `BookmarkModel` URL node: URL is the saved destination; title is the saved label. Navigation never changes either automatically. |
| Saved folders and ordering | Real bookmark parent/child relationships and sibling order. Do not persist a second ordering array for these nodes. |
| Entry identity | Random UUID `EntryId` in namespaced bookmark metadata; never URL, title, bookmark integer ID, or extension tab ID. Two entries may have the same URL. |
| Live association | Per-real-tab `SpaceId` and optional `EntryId`; the owning window indexes these associations. No cached copy of tab navigation history. |
| Window session | Active `SpaceId`, selection per Space, and identifier-only tab associations through Chromium session extra data. |

Use typed UUID wrappers for `SpaceId` and `EntryId`. Bookmark lookup must include
storage type plus bookmark UUID: UUIDs are not globally unique across local and
account storage. Resolve pointers only while handling an operation on the UI
sequence; observer notifications can invalidate them.

The first regular-profile write creates an ordinary local `OpenArc` bookmark
folder under Other Bookmarks and one backing folder per Space. These remain
visible and editable through Chromium's bookmark manager and extension APIs.
Names are presentation, never identity. Metadata identifies the managed root,
Space root, and saved entries; only descendants of a recognized Space root are
saved entries. Existing bookmarks outside these roots remain ordinary bookmarks.
Creating a bookmark inside a recognized root adopts it as a saved entry.

Space records own their display labels and order. A backing folder initially
gets a useful title, but editing that ordinary folder title does not rename the
Space. This keeps each field authoritative in one store. Themes, operation
journals, window state, and private state never enter bookmark metadata, which
can be copied or synced. Favorites are reserved as a future profile-wide scope;
do not ship a second implementation of saving for them.

## Four native modules

Place original code under `src/openarc/workspace/`; integration patches attach
it at the existing Chromium seams. These names and signatures describe the
proposed interface, not declarations verified to compile.

| Module | Small interface | Responsibility |
| --- | --- | --- |
| `WorkspaceService` | `GetSnapshot()`, `Execute(WorkspaceCommand, completion)`, `ObserveChanges(callback)` | Profile-scoped Space definitions, bookmark indexing/reconciliation, operation sequencing, recovery state, and immutable saved-tree snapshots. Factory depends on `BookmarkModelFactory`; readiness follows bookmark loading. |
| `WorkspaceWindowController` | `GetSnapshot()`, `Execute(WindowCommand, completion)`, `ObserveChanges(callback)` | One real browser window's associations, activation, close/save/unpin commands, last selection, and sidebar projection. Receives the service and real browser/tab-strip dependencies. |
| `WorkspaceTabState` | `GetAssociation()`, internal `SetAssociation(...)` | Small tab-owned state deriving from `ContentsObservingTabFeature`; follows a real `TabInterface` across WebContents discard. Mutation belongs to the window controller. |
| `WorkspaceSessionAdapter` | `EncodeTab/Window`, `StageRestoredTab/Window`, `ReconcileReadyWindow` | Bounded identifier payloads, all session write/restore paths, unresolved-ID handling, and explicit clearing. Codec helpers are internal test seams. |

`WorkspaceCommand` is a typed variant of the commands added at each checkpoint,
starting with create/rename/reorder Space and saved-node edit/move/remove. It is
not a string dispatch framework. `WindowCommand` begins with activate saved,
save existing tab, close live instance, return to saved destination, and remove
saved entry. Add switching/moving only with their model tests. A view submits a
command and observes the resulting snapshot; it cannot edit bookmark metadata,
session files, or tab state directly.

All public operations run on the UI sequence. Persistence runs on a sequenced
writer; callbacks are canceled safely during `Shutdown()`. Snapshots carry a
generation and stable IDs, not owning tab/bookmark pointers. Commands resolve
IDs again before acting and return controlled outcomes such as not ready,
missing entry, unavailable tab, canceled close, invalid destination, or recovery
required. Reject duplicate pending activation for the same window/entry, so two
clicks cannot create two instances. Emit one coherent notification after a batch,
while removing invalid references immediately during bookmark removals.

Do not introduce abstract wrappers for every Chromium class. Use the existing
test bookmark model, test profile, and real browser-test tab strip. Separate the
pure reconciliation/codec logic only where it supplies an actual test seam.

## Required transitions

| Operation | Required result |
| --- | --- |
| Activate an unloaded saved entry | Resolve its current bookmark URL, create one real tab through normal Chromium navigation, and bind it in this window. Reserve the binding while creation is pending. |
| Activate a loaded entry | Focus its existing real tab without navigating. Other windows keep their own instances. |
| Navigate, reload, or discard | Preserve `EntryId` and saved URL/title. The saved row reflects real loading/audio/crash state while its label remains the bookmark title. |
| Return to saved destination | Navigate the bound tab through normal navigation, including beforeunload. Do not rewrite history or update the bookmark. |
| Save an ordinary tab | Create a bookmark from its current committed, bookmarkable URL and user-facing title, then bind that same tab. No reload, renderer transfer, or native pin-bit change. |
| Close saved live page | Request normal Chromium close. Keep the binding until confirmed removal; canceled/deferred beforeunload leaves it bound. The bookmark remains. |
| Remove saved entry | Remove through bookmark mutation/undo handling; unbind live instances in every window and keep those pages as ordinary tabs in their Space. Offer Undo. |
| Update saved URL/title | Explicit bookmark mutation; update all saved rows, but do not navigate already-open instances. |
| Transfer to another same-Profile window | Preserve the real tab and Space. If the destination has that entry bound, its binding wins and the arriving page becomes ordinary. Preserve both pages. |
| Reopen recently closed | Restore normal navigation/history first, then rebind only if the entry exists and that window has no bound instance. Otherwise keep an ordinary tab. |

Saving never infers identity from matching URLs. To save the same address twice,
create two entries. An existing saved tab cannot be bound to two entries in the
same window. A duplicate-bookmark command creates a second unloaded entry.

An extension bookmark rename, URL edit, move, reorder, or delete has the same
effect as the equivalent bookmark-manager operation. Moving an entry into
another recognized Space root changes its scope and its bound tabs' Space;
active bound tabs reveal that new Space. Moving it outside managed roots unbinds
its live instances while preserving the ordinary bookmark. Deleting a backing
root empties the Space and unbinds descendants; it does not silently recreate the
deleted subtree or close tabs. Recreate a missing backing root only on the next
explicit save into that Space, after reconciliation confirms there is no surviving
root with its ID. Treat root deletion as one change that clears the scoped root
reference; this prevents duplicate roots after an interrupted creation.

Copied/imported metadata must not duplicate `EntryId` bindings. Preserve the
existing canonical node; give a newly observed copy a fresh ID. On startup,
prefer a valid stored locator, then use a documented deterministic storage/UUID
ordering to repair ambiguous copies. Never choose by URL. Account-storage moves
update the scoped locator; they do not create a second authoritative saved tree.

## Space and extension invariants

1. A real normal window belongs to one Profile. Its tabs belong to exactly one
   Space within that Profile. New normal tabs inherit their same-Profile opener's
   Space, otherwise the target window's active Space. Dedicated popup/OAuth
   windows retain upstream behavior and do not acquire a custom sidebar.
2. Space switching changes the projection and selected real tab. It never closes,
   hides a browser window, or reparents a renderer. Select that Space's last
   surviving tab; if empty, create one normal new-tab page when entering it.
3. A saved entry has at most one live instance per window. The same entry may be
   live in multiple windows. Workspace snapshots do not own those tabs.
4. Every live tab remains in the real `TabStripModel`, including other Spaces.
   Extensions observe truthful IDs, window IDs, indices, pin bits, groups, and
   events. An unloaded saved row is not a `chrome.tabs` tab. A sidebar row index
   is never passed as a native tab index.
5. Any activation of a tab, including extension-driven activation, reveals its
   Space before presenting the selection. An organization-only change emits no
   fake close/create/move events. Preserve active-tab permission semantics.
6. Native pinning is separate from saving. Extension/native pin and unpin retain
   Chromium ordering and event behavior; neither creates nor deletes bookmarks.
   Saved rows can show a native-pin indicator. An ordinary pinned tab stays in
   the open section's upstream pinned presentation.
7. Native tab groups stay real groups. The model assigns an entire group to one
   Space. Grouping into an existing group adopts its Space; a new group adopts
   the active member's Space, or its first native-index member's Space if none
   is active. Membership changes preserve tabs and upstream group operations.
   If this changes a saved member's Space away from its bookmark's scope, unbind
   that live instance and retain its bookmark unloaded; do not move shared
   bookmarks just because an extension regrouped tabs. Moving a saved bookmark
   across Spaces moves its live group as a whole, applying the same rule to any
   other saved members whose scopes differ.
   Saved members keep their saved rows and a group indicator; the open section
   shows remaining ordinary members under the native group header. Group actions
   operate on the full real group, including saved members. Projection must not
   manufacture membership or duplicate tab rows.
8. Profile changes open/focus another Profile's window and navigate there;
   they never transfer live renderers or cookies. Private instances use a
   memory-only workspace and no ordinary-profile writer, session extension, or
   recovery export. Do not inherit BookmarkModelFactory's OTR redirection for
   workspace writes. Private saved entries live only in that instance's memory;
   it does not acquire the original profile's BookmarkModel for mutation. This
   is an explicit ephemeral storage adapter behind the same snapshot/command
   interface, not a second durable bookmark store.

Space deletion requires an explicit destination, moves live associations and
saved contents recoverably, and cannot delete the last Space. Model and journal
support precede the UI command. Drag-and-drop follows these same commands; it
does not introduce another mutation path.

## Persistence and reconciliation

The workspace metadata record contains a schema version, Space definitions,
scoped root locators, and bounded pending operations. Use an atomic profile-local
writer with a previous compatible snapshot. Unknown future schemas open in
recovery/read-only mode; never overwrite them with defaults. No full tab URL list
belongs in this record. BookmarkModel remains the saved-URL authority and
Chromium session files remain navigation authority.

Journal operations spanning bookmark/workspace/session stores before mutation.
Record an operation ID, affected stable IDs, intended transition, and the minimum
undo/recovery data; a temporary removed-bookmark snapshot is recovery data, not
a competing saved catalogue. Persist the intent before exposing a multi-store
operation as completed. Bookmark writes and session writes complete separately:
do not claim an atomic commit across them. Keep an operation recoverable until
its stores can be reconciled, and expose pending/recovery state honestly.

On restore, wait for BookmarkModel loading, validate schema and IDs, reconcile
copied metadata, then attach live tabs. A missing saved node becomes an ordinary
tab with its navigation intact. A journal may offer explicit recovery of an
interrupted user operation; it must not automatically resurrect missing or
externally deleted bookmarks. An unknown Space falls back to the default Space
and clears the invalid reference. One live-instance collision preserves both
pages and unbinds the later restored instance deterministically.

Wire incremental tab/window extra data, full session rebuild, recently-closed
capture, and both tab/window restoration. Preserve association through historical
tab capture and canceled closes; clear live membership on confirmed `kRemoved`.
Defer insertion writes until Chromium has assigned the destination window's
session identity. Clearing writes an explicit versioned unbound payload because
extra-data updates overwrite keys rather than delete them. Store identifiers and
session references only; no private URLs or parallel navigation stacks.

Undo must not undo somebody else's subsequent bookmark edit. Use Chromium's
bookmark undo integration and its grouping semantics; an OpenArc removal Undo
is valid only while its matching operation is still recoverable. A restored entry
may rebind a still-open ordinary page only if its original tab identity is valid,
the page remains unbound in the same window/Space, and no instance has claimed
the entry. Otherwise restore the bookmark unloaded.

## Implementation checkpoints and evidence

After M0–M2 pass, implement model/reconciliation first without sidebar changes.
The first UI slice is one default Space with a bounded Saved tree above a divider
in the existing vertical region and real ordinary tabs below. Implement Save,
activate, close live page, remove with Undo, and Return to saved destination.
Keep the existing toolbar, omnibox, extension actions, permission UI, resize,
collapse, and browser menu. Do not relocate them in this slice.

The shared collection projection must suppress only the duplicate visual row for
a bound saved tab, never the tab itself. Preserve selection, keyboard focus,
accessibility, native pin ordering, and groups. If grouped saved-row projection
is not complete, keep the slice behind a development flag and mark it incomplete;
do not change extension behavior to make the UI easier. A subsequent slice adds
the bottom Space selector and new/rename/move commands using the already-tested
model. Favorites, broad drag-and-drop, and cross-Profile controls remain later.

Required interface and browser-test scenarios:

- Save → navigate → close canceled → close confirmed → activate: same saved
  destination, no duplicate live instance, navigation retained until real close.
- Two same-URL entries and two windows: independent identities, one instance per
  entry/window, and transfer collision preserving both pages.
- Bookmark extension edits: rename/URL/move/reorder, parent subtree deletion,
  move outside scope, metadata copy, cross-storage UUID collision, and Undo after
  another edit. No stale descendants or unwanted bookmark resurrection.
- Restart and recently-closed restore: incremental writes and full rebuild both
  retain valid associations; missing entries, stale Spaces, duplicate restored
  bindings, and interrupted operations preserve recoverable navigation/data.
- Discard/reload/crash retain association. Shutdown during a pending operation,
  malformed/newer schema, and failed migration leave the prior snapshot intact.
- Extension create/activate/move/close/pin/group operations preserve native IDs,
  indices/events and permissions; hidden-Space activation reveals the correct
  page. Unloaded saved entries never appear in tab queries.
- Private-window save/navigation/close/restart leaves no workspace records,
  journals, bookmark mutations, or session extra data in the regular profile.
- Visible first-slice run: save a real page, navigate within it, return, close and
  reopen in place; exercise an extension popup and side panel with sidebar shown
  and collapsed. Long saved lists leave New Tab and open tabs reachable.

Passing these scenarios proves the named slice only. It does not pass the full
M4/M5 UI contract, extension matrix, maintenance gate, or signed-release gate.
