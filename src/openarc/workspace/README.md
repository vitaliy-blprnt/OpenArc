# Native workspace foundation

`ReconcileAssociations` is a pure helper over identifier snapshots supplied by a
future browser adapter. Its header documents validation, caller priority order,
and the separate group-reconciliation responsibility. It has no URL data, I/O,
tab ownership, or persistence.

This is preparation for the saved-tab model, **not M3 completion**. The owned source overlay installs these modules at Chromium's
`openarc/workspace` path. GN generation, header dependency checks, compilation,
linking and execution passed for four focused native test executables.
Browser/profile factories, session integration, private-window behavior, and the
custom sidebar remain unverified in a running browser.

`EncodeTabSessionMetadata` and `DecodeTabSessionMetadata` are a tab-only JSON
codec. Version 1 uses exact arrays: `[1,"unbound"]`, `[1,"bound","space-uuid"]`,
or `[1,"bound","space-uuid","entry-uuid"]`. The fixed shape avoids duplicate
object-key ambiguity in the upstream JSON reader. It accepts RFC JSON whitespace,
requires lowercase valid UUIDs, rejects unknown versions/types/fields, limits
payloads to 256 UTF-8 bytes, and emits only version/state/identifiers.

Unbound clears the entire workspace association, including its Space. Removing
only the saved-entry association must encode a bound record that retains the
`SpaceId` and has no `EntryId`; saved-tab demotion must not clear Space affiliation.

Encoding requires an explicit profile context and refuses all off-the-record
values, including an explicit clear. Decoding only interprets caller-owned bytes;
it performs no persistence. An absent SessionService key is a caller decision;
an empty or malformed present value is an error. Retain rejected/future-schema
data through the caller's recovery mechanism and do not replace an error with
an encoded unbound/default value. The caller still must select the actual
profile context and prevent private writes. This helper does not prove private
non-persistence in a browser, window restore, or any session integration.

`EncodeWindowSessionMetadata` and `DecodeWindowSessionMetadata` add a separate
window payload: `[1,"unbound"]` or
`[1,"bound","active-space-uuid",[["space-uuid",source-tab-id],...]]`. A bound
record with no selections keeps the active Space. It limits input to 16 KiB and
256 selections, requires unique Spaces and positive int32 source tab IDs, rejects
one source tab selected in two Spaces, and refuses all private-context encodes.
Invalid input fails the whole operation; no truncation or partial state escapes.

`PersistedTabId` is a typed reference to a record in the source window snapshot,
not a live tab identity. Chromium creates fresh live IDs during restoration and
fresh historical IDs when capturing recently closed entries. The future adapter
must map live IDs to historical IDs at capture, account for historical IDs
regenerated on disk reload, and map old records to new live tabs at restore.
Only after current window/Space membership reconciliation may it resolve these
source references. An integer match in an unrelated live tab is never a fallback.
Unknown, skipped, or ambiguous references must not select unrelated tabs. The
codec does not build these mappings or activate tabs. Missing keys, rejected
payload recovery, and private-write prevention remain caller responsibilities.

## Read-only saved-entry catalog

`SavedEntryCatalog` observes a real Chromium `BookmarkModel`. Its owner supplies
the authoritative Space bindings and exact bookmark locators `(storage, UUID)`.
Version 1 root metadata uses `openarc.workspace.space_id.v1`; saved URL nodes use
`openarc.workspace.entry_id.v1`. Values are canonical lowercase UUID strings.
The root marker is the Space ID; a separate root-metadata UUID is unnecessary.
The persisted workspace record must retain the scoped bookmark locator as well
as the Space ID, so a copied marker cannot silently replace a missing root.

Snapshots distinguish model loading, extensive updates, readiness, and model
destruction. A ready snapshot still reports unavailable roots individually.
Each available Space exposes depth-first rows using current bookmark titles,
URLs, parent locators, and sibling order. Folder removal invalidates descendants
even when Chromium emits only one ancestor-removal notification. Missing or
malformed saved IDs and duplicate IDs retain their URL rows and bookmark
locators, but expose no bindable Entry ID. Nested configured roots are marked
unavailable instead of assigning descendants to an ambiguous Space. Local and
account storage are separate locator namespaces.

This catalog never creates, repairs, adopts, renames, removes, or persists a
bookmark. The future WorkspaceService owns those operations, including any
explicit recreation of a missing root. It must reconcile invalid live bindings
without closing pages, and must not treat loading/updating as authoritative
deletion. No observer callback retains a bookmark pointer. Consumers must copy
snapshots they retain and revalidate generation and locators before acting.
Synchronous bookmark mutation from a catalog callback is unsupported.
Observer teardown is supported: notification storage survives an owner being
destroyed during a callback, and later callbacks skip that destroyed owner.

The catalog currently rebuilds configured subtrees on relevant model callbacks
and publishes only changed snapshots. It withholds rows during extensive
changes, then publishes the rebuilt snapshot at the outer batch end. This
simple implementation is not a performance qualification for large libraries.
The caller must supply the correct regular-profile model; factory selection,
private storage isolation, metadata adoption, crash recovery, tab binding, and
browser UI integration are still outside this component.

## Tab-owned association state

`WorkspaceTabState` derives from Chromium's `ContentsObservingTabFeature` and
registers through the real tab's unowned-data host. Patch 0008 wires its
TabFeatures ownership behind the default-off OpenArcWorkspaces feature. Its identifier binding survives navigation and
WebContents replacement during discard; a detach signal does not clear it.
Only `WorkspaceWindowController` may change the binding in production.
Invalid updates preserve the previous value, repeated values do not notify,
and removing an Entry ID while retaining its Space remains distinct from
clearing the complete association.

All seven `RenderViewHostTestHarness` cases passed in the GN-built
`workspace_tab_state_tests` executable. They cover navigation/discard preservation,
canceled-close-safe state, demotion and notification timing, invalid-input
preservation, unowned-data/subscription lifetime, and destruction from a change
observer. Notification storage survives the callback while weak subscriptions
skip remaining callbacks after feature destruction. This establishes native
component behavior, not session restoration or browser UI acceptance.

## Default-Space window controller preparation

`WorkspaceWindowController` uses the real `TabStripModel` and a read-only saved
catalog. A navigation callback creates a normal browser tab; repeat activation
focuses the existing bound page without navigation and retains the caller's
activation gesture. A save completion binds the same page only after resolving
its current bookmark locator and entry ID. Neither operation alters Chromium's
pin bits, groups, native ordering, or real tab identity.

Close uses the stock tab-strip delegate and reports a request, not a confirmed
close. The association survives refusal/deferred close; confirmed strip removal
drops only the window's claim. Transfer preserves tab-owned state. An existing
destination-window claim wins a collision even when the incoming tab is inserted
earlier, leaving both pages alive. Bookmark deletion demotes a saved page to an
ordinary tab. Loading/extensive catalog updates preserve associations and keep
pages reachable until a saved projection can be published again.

The controller is deliberately limited to one default Space. It rejects private,
guest, and non-normal windows. Other-Space bindings remain visible and intact
for later integration; this is not Space switching or session restoration. It
does not acquire profiles, write files, mutate bookmarks, or implement sidebar
Views. Browser/profile ownership and a production navigation adapter are pending.

All 15 cases passed in the GN-built `workspace_window_controller_tests`
executable. They use real TabStripModel/BookmarkModel objects and cover activation,
saved URL separation, pin/group preservation, close-request retention, deletion,
batching, transfer collisions, stale completions, recursive activation, private
rejection, and controller/strip destruction during callbacks.

## GN native test checkpoint

With the owned overlay and nine ordered patches applied, use the pinned output
configuration documented in `docs/BUILDING.md`. Build these targets with the pinned
`autoninja`, then run each executable with `--test-launcher-jobs=1` and
`--test-launcher-retry-limit=0`:

| Target | Passed cases |
| --- | ---: |
| `workspace_model_tests` | 84 |
| `workspace_tab_state_tests` | 7 |
| `workspace_window_controller_tests` | 15 |
| `workspace_bookmark_write_tests` | 317 |

The first target contains 40 association/codec, 18 catalog and 26 store/service
cases. The bookmark target includes all 14 new primary-write acknowledgement
cases. Local compilation/execution receipts, source hashes and launcher summaries
are preserved under ignored `.build/native-workspace-gn`. All 28 overlay files
and nine patch inputs were unchanged across execution. This test run does not
produce a browser build receipt. The standalone recipes below record earlier,
narrower helper checks and are not additional independent test counts.

## Standalone native test recipe

Run from the OpenArc repository root on macOS Apple Silicon with Python/toolchain
setup already completed. This recipe requires the pinned Chromium checkout,
its pinned Clang and Google Test sources, generated headers in `out/Baseline/gen`,
and an already-linked component `libbase.dylib`, `libc++_chrome.dylib`, and their
transitive component libraries. They come from the documented baseline build;
this recipe does not fetch, configure, or build them. Use matching headers and
artifacts from the same pin/configuration. A missing library or generated header
is a prerequisite failure, not a passing test.

All new outputs go under ignored `.build/native-association-tests`. The command
does not run Ninja or change the Chromium source, generated build graph, or its
output artifacts. It compiles the pinned Google Test sources for this standalone
executable; it does not run Chromium's browser test launcher.

```sh
repo_root="$(pwd)"
chromium_root="$repo_root/.build/chromium/src"
component_output="$chromium_root/out/Baseline"
test_output="$repo_root/.build/native-association-tests"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
mkdir -p "$test_output"

"$chromium_root/third_party/llvm-build/Release+Asserts/bin/clang++" \
  -std=c++23 -fno-exceptions -fno-rtti -fno-modules -O0 -g0 \
  -Wall -Wextra -Werror -Wno-unused-parameter \
  -DCOMPONENT_BUILD -DDCHECK_ALWAYS_ON=1 -DNDEBUG \
  -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE \
  -D_LIBCPP_INSTRUMENTED_WITH_ASAN=0 \
  -nostdinc++ -nostdlib++ -isysroot "$sdk_path" -mmacos-version-min=13.0 \
  -fcrash-diagnostics-dir="$test_output/clang-crashes" \
  -isystem "$chromium_root/third_party/libc++/src/include" \
  -isystem "$chromium_root/third_party/libc++abi/src/include" \
  -I "$repo_root/src" -I "$chromium_root" -I "$component_output/gen" \
  -I "$chromium_root/buildtools/third_party/libc++" \
  -I "$chromium_root/third_party/abseil-cpp" \
  -I "$chromium_root/third_party/perfetto/include" \
  -I "$component_output/gen/third_party/perfetto/build_config" \
  -I "$component_output/gen/third_party/perfetto" \
  -I "$chromium_root/base/allocator/partition_allocator/src" \
  -I "$component_output/gen/base/allocator/partition_allocator/src" \
  -I "$chromium_root/third_party/googletest/src/googletest" \
  -I "$chromium_root/third_party/googletest/src/googletest/include" \
  "$repo_root/src/openarc/workspace/association_reconciler.cc" \
  "$repo_root/src/openarc/workspace/association_reconciler_unittest.cc" \
  "$repo_root/src/openarc/workspace/tab_session_codec.cc" \
  "$repo_root/src/openarc/workspace/tab_session_codec_unittest.cc" \
  "$repo_root/src/openarc/workspace/window_session_codec.cc" \
  "$repo_root/src/openarc/workspace/window_session_codec_unittest.cc" \
  "$chromium_root/third_party/googletest/src/googletest/src/gtest-all.cc" \
  "$chromium_root/third_party/googletest/src/googletest/src/gtest_main.cc" \
  -L "$component_output" -lbase -lc++_chrome \
  "-Wl,-rpath,$component_output" \
  -o "$test_output/association_reconciler_tests" && \
  "$test_output/association_reconciler_tests"
```

`-Wno-unused-parameter` matches Chromium's suppression for its headers. The
Chromium libc++ headers/configuration and runtime are required because its C++
ABI differs from the system libc++. Paths and compilation transcripts from a
local run belong in ignored output, not public test evidence.

The tests exercise current bookmark scope, default-Space fallback, missing-entry
demotion, caller-ordered collision priority, independent windows/entries,
invalid authority and real-tab identities, whole-result failure, immutability,
and idempotence. Codec cases additionally cover bound/unbound round trips,
invalid identifiers, schema/type confusion, unknown fields, size/UTF-8 limits,
strict JSON syntax, and private-context encode refusal. Passing this executable
establishes helper behavior only.

The helper executable contains 40 passing native tests: 12 association cases,
12 tab-codec cases, and 16 window-codec cases. Window cases additionally cover
multiple/empty selections, typed source-ID preservation, duplicate rejection,
positive int32 boundaries, whole-payload/count limits, strict schema parsing,
and private-context refusal. These tests do not prove live/historical ID mapping
in Chromium session restore.

## Real BookmarkModel standalone catalog tests

The following separate recipe uses the same pinned component build prerequisites
as above. In addition it reads the existing bookmark/static archives, two
production object files, component libraries, ICU data, and the generated English
resource pack from `out/Baseline`. The pinned LLD linker supports Chromium's thin
archives; the system Apple linker does not. The small standalone main supplies
ICU and resource initialization normally provided by Chromium's unit-test
launcher. `LoadEmptyForTest()` creates synthetic in-memory bookmarks without
loading a real profile or creating bookmark stores.

All new output stays in ignored `.build/native-catalog-tests`. Nothing invokes
GN/Ninja or rewrites the active checkout or its output. `saved_entry_catalog_test_main.cc`
is standalone-only and is intentionally absent from the proposed GN test target.

```sh
repo_root="$(pwd)"
chromium_root="$repo_root/.build/chromium/src"
component_output="$chromium_root/out/Baseline"
test_output="$repo_root/.build/native-catalog-tests"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
mkdir -p "$test_output"

"$chromium_root/third_party/llvm-build/Release+Asserts/bin/clang++" \
  -std=c++23 -fno-exceptions -fno-rtti -fno-modules -O0 -g0 \
  -Wall -Wextra -Werror -Wno-unused-parameter \
  -DCOMPONENT_BUILD -DDCHECK_ALWAYS_ON=1 -DNDEBUG \
  -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE \
  -D_LIBCPP_INSTRUMENTED_WITH_ASAN=0 \
  -nostdinc++ -nostdlib++ -isysroot "$sdk_path" -mmacos-version-min=13.0 \
  -fcrash-diagnostics-dir="$test_output/clang-crashes" \
  -isystem "$chromium_root/third_party/libc++/src/include" \
  -isystem "$chromium_root/third_party/libc++abi/src/include" \
  -I "$repo_root/src" -I "$chromium_root" -I "$component_output/gen" \
  -I "$chromium_root/buildtools/third_party/libc++" \
  -I "$chromium_root/third_party/abseil-cpp" \
  -I "$chromium_root/third_party/skia" \
  -I "$chromium_root/third_party/perfetto/include" \
  -I "$component_output/gen/third_party/perfetto/build_config" \
  -I "$component_output/gen/third_party/perfetto" \
  -I "$chromium_root/base/allocator/partition_allocator/src" \
  -I "$component_output/gen/base/allocator/partition_allocator/src" \
  -I "$chromium_root/third_party/googletest/src/googletest" \
  -I "$chromium_root/third_party/googletest/src/googletest/include" \
  "$repo_root/src/openarc/workspace/saved_entry_catalog.cc" \
  "$repo_root/src/openarc/workspace/saved_entry_catalog_unittest.cc" \
  "$repo_root/src/openarc/workspace/saved_entry_catalog_test_main.cc" \
  "$chromium_root/components/bookmarks/test/test_bookmark_client.cc" \
  "$chromium_root/components/os_crypt/async/browser/test_utils.cc" \
  "$chromium_root/components/os_crypt/async/common/test_encryptor.cc" \
  "$chromium_root/third_party/googletest/src/googletest/src/gtest-all.cc" \
  "$component_output/obj/components/bookmarks/browser/libbrowser.a" \
  "$component_output/obj/components/bookmarks/common/libcommon.a" \
  "$component_output/obj/components/favicon_base/libfavicon_base.a" \
  "$component_output/obj/components/url_formatter/liburl_formatter.a" \
  "$component_output/obj/components/url_formatter/libskeleton_generator.a" \
  "$component_output/obj/net/libpreload_decoder.a" \
  "$component_output/obj/components/query_parser/query_parser/query_parser.o" \
  "$component_output/obj/components/omnibox/common/common/string_cleaning.o" \
  -L "$component_output" -lbase -lbase_i18n -lc++_chrome -lurl \
  -lui_base -lui_gfx -lkeyed_service_core -lcomponents_os_crypt_async_common \
  -lcomponents_os_crypt_async_browser -lcomponents_prefs \
  -lcomponents_pref_registry -lcrcrypto -lthird_party_icu_icui18n \
  -licuuc -lsignin_switches -lnet -fuse-ld=lld -Wl,-dead_strip \
  "-Wl,-rpath,$component_output" \
  -o "$test_output/catalog_tests" && \
  "$test_output/catalog_tests" "$component_output/icudtl.dat" \
    "$component_output/gen/repack/locales/en.pak"
```

The 18 catalog tests exercise actual model notifications for loading, URL/title
authority, nested folder/order projection, absent and malformed IDs, duplicate
IDs within/across Spaces, moves between/outside roots, ancestor deletion, root
deletion without replacement-marker adoption, root-marker repair by the caller,
nested-root ambiguity, storage-scoped UUID lookup, extensive change batches,
remove-all/model destruction, atomic invalid configuration rejection, unrelated
edits, and rejection of permanent, managed, and URL roots. This validates the
catalog's model behavior, not the future browser/profile/tab integration.

## Bounded default-Space persistence service

`WorkspaceService` adds explicit creation of one default Space and saving one
bookmark at a time. Creation does not run automatically when a profile loads.
The service waits for both its record and the real BookmarkModel. Its snapshot
distinguishes loading, ready, writes in progress, recovery, and shutdown; catalog
root availability remains a separate property. It has no general rename/move/
delete API, tab navigation, or session persistence.

`WorkspaceRecordStore` writes the fixed regular-profile files
`OpenArcWorkspaces.json` and `OpenArcWorkspaces.previous.json` on a sequenced
worker. Schema 1 is an exact array containing version, monotonic revision,
default Space ID, label/icon/theme, optional local bookmark locators for the
container and root, and an optional creation intent. The intent holds an
operation ID, planned container/root/bookmark UUIDs, and an Entry ID. It never
holds saved URLs or page titles. Input is limited to 64 KiB; unknown versions,
malformed records, a missing primary with a previous copy, and changed files
require recovery. Neither file is replaced until its bytes match the known
load/write state. The previous copy is rotated from the known primary before
atomic primary replacement. A retry can recognize that intermediate rotation.
There is no automatic fallback to the previous copy or filesystem compare-and-
swap: a future factory must own exactly one store per exclusively owned profile,
and external changes during a commit are unsupported.

A save first commits its intent, then creates user bookmark nodes with those
known UUIDs and versioned `openarc.workspace.*.v1` metadata. The container marker
is `openarc.workspace.container.v1`; its value, like the root marker, is the
Space UUID. Native grouped bookmark actions remain visible to BookmarkUndoService.
The service asks for a fresh primary BookmarkStorage write acknowledgement,
revalidates the current bookmark identity, and only then clears the intent in
the record. Applied identity and durable completion are distinct. Later bookmark
edits retain normal BookmarkModel persistence semantics; an acknowledgement
describes the captured snapshot, not every subsequent edit or power-loss safety.

On restart, a surviving exact pending bookmark can be acknowledged and finalized
without changing its content. Missing nodes are never recreated automatically.
`RetryPendingSave` is an explicit new URL/title request for missing nodes; it
preserves existing nodes and their current content. `AbandonPendingSave` retires
only the intent and does not remove bookmarks. A write failure preserves recovery
state; unrelated bookmark notifications cannot silently retry it. Recovery from
an invalid record or a failed initial record write currently requires resolving
the file condition and reloading the service; no broad repair API is provided.
Root deletion preserves the Space, clears the missing root locator durably, and
requires a subsequent explicit save to create a new root.

Shutdown prevents later bookmark mutations, replies to pending commands with
shutdown, and leaves already posted atomic writes free to finish using owned
bytes. A destroyed record store replies with failure on the origin sequence;
callers must use weak callbacks. Notification callbacks may destroy the service;
later observers skip it. Final notifications keep the command busy until its
completion is delivered, and saved identity is checked again after notification.

Every nonregular persistence context is rejected before a record writer is
constructed. This is only the module boundary: the browser factory must reject
actual private contexts before getting a profile path or BookmarkModel, because
Chromium redirects its bookmark service to the original profile. Custom private
workspace UI remains disabled until a separate ephemeral backend exists.

The standalone service suite currently passes 26 native tests using real
BookmarkModel mutations and actual atomic record-file I/O in temporary test
directories. These cover intent ordering, failed writes, preserved previous
copies, pending-operation restart, no automatic recreation, authority of later
edits, loading order, explicit abandon, callback destruction/reentrancy, and
shutdown. The bookmark-acknowledgement callback is controlled by these tests;
this suite does not substitute for running the production adapter. The separate
scratch BookmarkModel/BookmarkStorage acknowledgement patch passed nine real
cleartext runtime cases; its added upstream encrypted-primary cases compiled
but have not run.

To reproduce the service suite with the standalone catalog recipe above, use
`.build/native-workspace-service-tests` as `test_output`, replace
`saved_entry_catalog_unittest.cc` with `workspace_service_unittest.cc`, and add
`workspace_record_store.cc` and `workspace_service.cc` to its source list. Keep
the catalog implementation, standalone main, Chromium sources/libraries, and
the two resource arguments. The prepared GN `workspace_model_tests` target also
includes this suite. GN execution, factory/window integration, and the custom
sidebar are separate checks and are not established by these standalone tests.
