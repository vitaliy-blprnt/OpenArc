# Workspace helper preparation

`ReconcileAssociations` is a pure helper over identifier snapshots supplied by a
future browser adapter. Its header documents validation, caller priority order,
and the separate group-reconciliation responsibility. It has no URL data, I/O,
tab ownership, or persistence.

This is preparation for the saved-tab model, **not M3 completion**. The local
`BUILD.gn` describes proposed targets; no overlay or Chromium target includes it
yet, and it has not been built through GN. Browser/profile factories, session
integration, private-window behavior, and the custom sidebar remain unverified.

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

The catalog currently rebuilds configured subtrees on relevant model callbacks
and publishes only changed snapshots. It withholds rows during extensive
changes, then publishes the rebuilt snapshot at the outer batch end. This
simple implementation is not a performance qualification for large libraries.
The caller must supply the correct regular-profile model; factory selection,
private storage isolation, metadata adoption, crash recovery, tab binding, and
browser UI integration are still outside this component.

## Tab-owned association state

`WorkspaceTabState` derives from Chromium's `ContentsObservingTabFeature` and
registers through the real tab's unowned-data host. The proposed TabFeatures
owner has not been wired yet. Its identifier binding survives navigation and
WebContents replacement during discard; a detach signal does not clear it.
Only the future `WorkspaceWindowController` may change the binding in production.
Invalid updates preserve the previous value, repeated values do not notify,
and removing an Entry ID while retaining its Space remains distinct from
clearing the complete association.

The production and six `RenderViewHostTestHarness` test translation units have
compiled with the pinned Chromium toolchain. Those six tests have not been
linked or executed. They prepare checks for navigation/discard preservation,
canceled-close-safe state, demotion and notification timing, invalid-input
preservation, and unowned-data/subscription lifetime. The prepared GN targets
have not run. No browser factory, TabFeatures ownership, window controller, or
session integration is established by compilation.

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
