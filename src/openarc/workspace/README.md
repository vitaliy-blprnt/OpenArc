# Workspace helper preparation

`ReconcileAssociations` is a pure helper over identifier snapshots supplied by a
future browser adapter. Its header documents validation, caller priority order,
and the separate group-reconciliation responsibility. It has no URL data, I/O,
tab ownership, or persistence.

This is preparation for the saved-tab model, **not M3 completion**. The local
`BUILD.gn` describes proposed targets; no overlay or Chromium target includes it
yet, and it has not been built through GN. Browser, session, BookmarkModel,
private-window, and extension integration remain unverified.

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
