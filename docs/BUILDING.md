# Building OpenArc

The repository currently provides the foundation for fetching, checking,
patching, building, and launching Chromium. A successful tooling check is not a
successful Chromium build, and a Chromium baseline build is not a completed
OpenArc browser. The custom sidebar, Spaces, extension compatibility, and release
qualification require the separate gates in [BROWSER-PLAN.md](BROWSER-PLAN.md).

## Prerequisites

The current build and launch tooling requires an Apple Silicon Mac; Intel,
Windows, and Linux application builds are not implemented here. Use Git,
Python 3, and the Xcode/macOS SDK required by the locked
Chromium revision. Follow that revision's `docs/mac_build_instructions.md` for
host requirements, filesystem requirements, supported architectures, and toolchain
setup. The upstream [macOS build instructions](https://chromium.googlesource.com/chromium/src/+/main/docs/mac_build_instructions.md)
are a discovery reference; the checked-out revision is authoritative for a build.

Keep the working copy on a suitable local volume and check available disk space
before fetching. Chromium source, dependencies, and build output are substantial;
the lightweight repository checks below do not download them. Build from a
dedicated development checkout and keep real browser profiles outside it.

## Lightweight checks

Run from the OpenArc repository root:

```sh
python3 scripts/openarc.py --help
python3 scripts/openarc.py check
python3 -m unittest discover -s tests/tooling -v
node --test tests/extensions/platform-probe/scope.test.mjs
python3 scripts/openarc.py doctor
```

`check` validates locked inputs and the ordered patch series. The unit tests
exercise repository tooling. `doctor` reports host prerequisites and checkout
status. Missing depot_tools commands before `fetch` are informational; a missing
SDK, unsupported host, or inadequate disk headroom returns a failure. The current
free-space floors are 20 GiB for `fetch` and 40 GiB for `sync`/`build`, not guarantees
that the complete source and build will fit. These checks do not compile or run
browser code.

The optional extension-fixture checks require Node.js 24 or later. The Chromium
build itself uses the tools supplied by its dependency checkout.

After fetching, `python3 scripts/openarc.py check --checkout` also verifies the
source revision and recorded applied-patch state. The default `check` stays
independent of the local Chromium checkout and works in lightweight CI.

## Fetch and build the development application

The following operations use network access and create the local source/build
tree. They are explicit steps so the dependency checkout and hooks are reviewable:

```sh
python3 scripts/openarc.py fetch
python3 scripts/openarc.py sync
python3 scripts/openarc.py apply
python3 scripts/openarc.py build
```

The sequence is:

1. `fetch` checks out pinned depot_tools, explicitly bootstraps its tools, and
   fetches the pinned Chromium source.
2. `sync` resolves Chromium's pinned dependencies and runs its hooks.
3. `apply` applies OpenArc's ordered integration patches. An empty series builds
   the upstream baseline; it does not implement the planned OpenArc interface.
4. `build` generates `out/OpenArc` with GN and builds the Chromium `chrome` target
   using `autoninja`.

For the initial unmodified Chromium baseline, run these commands after `sync`
and before applying OpenArc patches:

```sh
python3 scripts/openarc.py build --baseline --jobs 8
python3 scripts/openarc.py launch --baseline
```

The baseline uses `out/Baseline` and `.build/profiles/baseline`; it must have a
clean Chromium checkout. Normal `build` requires the complete recorded OpenArc
patch series. Do not reverse or discard local changes merely to rerun a baseline.
Baseline launch uses Chromium's mock Keychain so it cannot access an existing
Chromium Safe Storage entry. Use synthetic browsing data only in that mode; it
does not qualify credential storage or native password-manager integration.

### Reuse the completed baseline compilation

After the unmodified baseline has compiled and its launch has been verified,
**quit its browser process and windows** before promoting its output. Component
libraries are rebuilt in place; a leftover `Chromium.app` is not an independent
baseline installation. Then run:

```sh
python3 scripts/openarc.py apply
python3 scripts/openarc.py build --reuse-baseline --jobs 8
python3 scripts/openarc.py launch
```

This explicit option keeps the same `out/Baseline` path and Siso incremental
state. GN and `autoninja` still run to rebuild every affected target; reuse does
not substitute an earlier compilation for the patched build. It requires the
complete recorded patches, the OpenArc product identity, a successful baseline
receipt matching the current lock/dependencies, and unchanged baseline executable
and bundle metadata. It does not change `upstream.lock` or clean build outputs.

Before bootstrap, GN, or compilation can mutate output, the workflow records
`.build/baseline-promotion.json` and invalidates both launch receipts. That file
preserves the baseline receipt as **historical evidence only**. The old Chromium
bundle is retained, but baseline launch remains blocked after promotion, including
after a failed build. Only a successful incremental build creates a development
receipt for `OpenArc.app`, explicitly recording `out/Baseline`; normal launch
uses `.build/profiles/development` without the baseline mock Keychain flag.

Subsequent `build` commands, including retries after interruption, retain the
promoted output selection. Keep the promotion record: removing it is not a way
to restore a baseline. Its seed remains bound to the original lock/dependencies;
an upstream change requires a separately reviewed output transition. These
receipts establish compilation provenance, not runtime or release qualification.

### Remove the recorded patch overlay

For planned maintenance, after stopping builds and closing browsers using the
checkout, run `python3 scripts/openarc.py unapply`. It validates the current pin,
patch hashes and recorded source state, then reverses only the applied prefix in
reverse order, checkpointing each successful removal. Unknown edits/files are
preserved and block the operation; failures retain the remaining recorded prefix
for review or retry. It never resets, cleans, or stashes the checkout. Repeating
the command when no patches remain is harmless.

An interruption after Git reverses a patch but before its checkpoint is saved
leaves the ledger out of sync. The next invocation fails closed; inspect and
reconcile that state manually before retrying. Automatic retry is covered only
between completed checkpoints.

Build receipts and historical promotion evidence remain intact; removing patches
does not authorize launching mismatched binaries or restore a promoted baseline.
The matching empty patch state is retained for reapplication at the same pin.
Before changing the pin, explicitly review and archive that old state along with
the maintenance evidence. Pin changes, dependency updates, output ownership and
rebuild qualification require a separate reviewed transition; `unapply` performs
none of those steps automatically.

Pinning depot_tools disables automatic updates, including its implicit bootstrap.
The workflow explicitly invokes the pinned `ensure_bootstrap` script and checks
that its Python launcher works. To repair an already fetched checkout independently
of a source download, run `python3 scripts/openarc.py bootstrap`. `sync` and
`build` also ensure this setup is present before invoking their upstream tools.

The exact upstream revisions and GN arguments live in [`upstream.lock`](../upstream.lock).
The initial configuration targets `arm64`, uses a non-debug component build, sets
symbol levels and PGO phase to zero, and disables remote execution. Chromium's
dependencies supply its compiler. `build` defaults to eight compiler jobs; use
`build --jobs 4`, for example, to set concurrency explicitly. This component build
is a development baseline; the signed release process needs its own validated
non-component configuration.

The tooling keeps its workspace under the repository's `.build` directory:

| Location | Purpose |
| --- | --- |
| `.build/depot_tools` | Pinned Chromium build tooling |
| `.build/chromium/src` | Chromium source and dependency checkout |
| `.build/chromium/src/out/OpenArc` | Default development build output |
| `.build/profiles/development` | Isolated development browser data |
| `.build/chromium/src/out/Baseline` | Unmodified baseline output, or explicitly promoted incremental OpenArc output |
| `.build/profiles/baseline` | Isolated baseline browser data |
| `.build/baseline-promotion.json` | Promoted output ownership and historical baseline receipt |

Do not use arbitrary environment overrides or local GN edits as release evidence.
Record every intentional configuration change and its source revision. A
component/debug development build and a signed distribution build have different
acceptance requirements.

## Launch safely with isolated data

```sh
python3 scripts/openarc.py launch
```

An optional positional HTTP(S) URL can be supplied, for example:

```sh
python3 scripts/openarc.py launch https://example.com/
```

The launcher uses `.build/profiles/development` and locates the expected
application in the output recorded by the successful receipt. For an explicitly
promoted output it requires `OpenArc.app`, preserving and ignoring stale
`Chromium.app`; ordinary outputs still reject ambiguous application candidates.
It does not accept an external `--user-data-dir` override.
It also requires a successful build receipt matching the lock, patch state,
dependency revisions, executable, and bundle metadata. A failed rebuild
invalidates the prior receipt; it cannot silently launch an older binary under
new source evidence. These development checks do not replace release signing.
Never replace the development profile with a link to a Chrome, Arc, Chromium,
or daily-use OpenArc profile. Chromium owns its data compatibility, while future
OpenArc metadata has its own migration requirements.

The baseline may retain Chromium branding until the application integration and
branding milestones are implemented. Do not infer a saved-tab or Space feature
from the output-directory name. Use disposable data for tests; do not include
generated profiles or build artifacts in contributions.

## Validate the actual browser

After compilation, separately record application launch, visible UI behavior,
extension installation/runtime/update behavior, native messaging, and recovery
results. Include the OpenArc commit, exact Chromium revision, architecture,
toolchain, and build arguments with each result. Run the focused upstream and
OpenArc browser tests required by the changed behavior.

The [extension platform probe](EXTENSION-PROBE.md) provides an unpacked MV3
fixture and an optional synthetic native host for the isolated profiles.

An extension that loads in an unsigned local build has not necessarily passed
native password-manager trust in the final signed app. A finite test suite is not
a claim that every extension works. Report the scope of evidence and unresolved
failures explicitly.

Packaging, signing, notarization, production profile identity, and automatic
updates are not provided by the initial build CLI. Follow the separate
[release contract](RELEASING.md) before publishing or claiming a qualified release.

## Diagnose failures

Re-run `doctor` and `check`, then record the failing command and its redacted
output. Distinguish a missing prerequisite, dependency download, patch mismatch,
compiler failure, and runtime failure. Preserve local changes and logs when
investigating; do not resolve a patch failure by silently dropping a patch or
switching to an unrecorded upstream revision.

When filing an issue, include the lock/build configuration and a minimal
reproduction. Remove private paths, credentials, URLs, and profile contents.
Security reports belong in the private route described by [SECURITY.md](../SECURITY.md).
