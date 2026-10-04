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
3. `apply` stages the explicitly listed OpenArc source files and applies its
   ordered integration patches. Neither step establishes browser integration
   or runtime qualification by itself.
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

### Build a separate non-component packaging candidate

The pinned Chromium signing workflow requires `is_component_build=false`.
After applying the full OpenArc patch series, build its separate candidate and
matching upstream signing-support files with:

```sh
python3 scripts/openarc.py build --packaging --jobs 8
python3 scripts/openarc.py launch --packaging
```

This mode always uses `out/Packaging`, including when development output has
already been promoted to `out/Baseline`. It never reuses or promotes the baseline.
It requires the applied OpenArc product identity and builds both `chrome` and
`chrome/installer/mac`; the latter generates the matching `OpenArc Packaging`
directory containing signing scripts, configuration, and entitlement inputs.
The build itself does not invoke those signing scripts or create a DMG/PKG.

The only GN argument override is `is_component_build=false`. Every other argument
comes from `upstream.lock`, which must explicitly retain `is_debug=false` and
`is_official_build=false`. An incompatible configuration fails before generation
or invalidating an existing packaging receipt.
The original lock is unchanged. A dedicated `.build/packaging-build-info.json`
receipt records the original lock, full applied patches, source/dependency
evidence, effective GN arguments, output path, executable hash, and bundle
metadata hash. A failed packaging rebuild invalidates only this mode's receipt;
development/baseline receipts and promotion evidence remain intact.

`launch --packaging` requires that dedicated receipt and `OpenArc.app`, verifies
its inputs and binary identity, and uses `.build/profiles/packaging` with
`.build/logs/packaging-launch.log`. It does not reuse the development or baseline
profile or enable the baseline mock Keychain. The `--baseline`,
`--reuse-baseline`, and `--packaging` build options are mutually exclusive;
baseline and packaging launch modes are also mutually exclusive. The lock's
normal `output_dir` cannot select the reserved `out/Packaging` directory.

This is a candidate for separately reviewed signing and vendor-trust testing,
not a release-qualified artifact. The receipt is compilation evidence, not a
signed-artifact receipt; later signing can change executable hashes and invalidate
its launch authorization. Runtime, signing, notarization, and password-manager
trust still require their own evidence. The existing development build is kept
separate throughout.

### Stage original OpenArc source

The authored files live only under `src/openarc`. The explicit
[`source-overlay.json`](../src/openarc/source-overlay.json) manifest lists the
files that `apply` may copy to the same relative paths beneath
`.build/chromium/src/openarc`. It accepts no alternate source or destination root.
The initial list contains workspace source and GN files present at `e0d80c9`;
new service or feature files need an explicit manifest change. Native sources
remain authored in the parent repository, not in generated checkout copies or
integration patches. Patches may connect Chromium targets to `//openarc/...`,
but cannot modify files inside that owned namespace.

**Stop builds and close browsers using this checkout before running `apply` or
`unapply`.** `apply` snapshots the listed originals, checks existing ownership,
and stages additions, replacements, and removals through Git. It rejects
unowned destinations, symlinks, changed generated copies, and conflicting index
state. It records the manifest digest, file hashes and executable modes in
`.build/patch-state.json`, alongside the patch ledger and combined source digest.
Repeated application of the same snapshot is harmless. An absent manifest is
valid before installation; after installation, restore a missing manifest or
use `unapply` to remove the recorded copies.

`build`, `launch`, and `check --checkout` verify installed copies and their
originals without reconciling anything. Changing a listed original, its mode,
or the manifest requires explicit `apply` and a new build before launch. A
successful overlay build records that source evidence in its receipt and checks
it again after compilation. Until an overlay is installed, merely adding or
editing the manifest and originals leaves existing build inputs and receipt
formats unchanged. Baseline builds require the overlay to be removed. Baseline
promotion preserves the original pristine receipt and includes the overlay only
in the new development receipt.

Before an overlay Git transition, the ledger stores its exact before and after
states and reversible patch. If interrupted, `apply` resumes that transition and
then reconciles the current manifest; `unapply` rolls it back before removing the
recorded patches and copies. Recovery accepts only an exact recorded before or
after state, with matching worktree and index ownership. Any other state blocks
both operations and builds/launches, preserving files for manual review. These
operations never reset or silently repair unknown work.

### Remove the recorded patches and source overlay

For planned maintenance, after stopping builds and closing browsers using the
checkout, run `python3 scripts/openarc.py unapply`. It validates the current pin,
patch hashes and recorded source state, then reverses only the applied prefix in
reverse order, checkpointing each successful removal. Unknown edits/files are
preserved and block the operation; failures retain the remaining recorded prefix
for review or retry. After reversing patches, it removes only unchanged owned
source copies using their recorded hashes and modes; this still works if the
originals or manifest have since changed or disappeared. It never resets,
cleans, or stashes the checkout. Repeating the command when nothing remains is
harmless.

An interruption after Git reverses a patch but before its checkpoint is saved
leaves the patch ledger out of sync. The next invocation fails closed; inspect and
reconcile that state manually before retrying. Automatic retry is covered only
between completed patch checkpoints. The pending transition recovery described
above applies specifically to generated source-overlay transitions.

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
| `.build/chromium/src/out/Packaging` | Separate non-component OpenArc candidate and matching upstream signing inputs |
| `.build/packaging-build-info.json` | Dedicated candidate receipt including effective GN arguments |
| `.build/profiles/packaging` | Isolated packaging-candidate browser data |
| `.build/logs/packaging-launch.log` | Packaging-candidate LaunchServices diagnostics |

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

On macOS the launcher calls `/usr/bin/open -n -a` with the exact verified `.app`
path and passes the complete isolated-profile argument list after `--args`.
This registers the intended GUI instance through LaunchServices. Opening an app
by display name, bundle ID, or a later automation-tool app selection can activate
another instance without those arguments; do not use that as launch evidence.

The JSON result preserves the browser argument list and the LaunchServices
command. It reports `pid: null`: `open` is a short-lived helper, and its PID is
not a verified browser PID. Success means LaunchServices accepted the request,
not that the browser is running correctly. Verify the executable, command line,
and profile path in `chrome://version`, then separately observe the UI and
browsing. Any process-specific diagnostic or shutdown must first verify that
exact process belongs to this isolated launch; never terminate browsers by app
name. The launcher does not look up or terminate browser processes. Its log contains
LaunchServices diagnostics, not a guaranteed browser console log. If the bounded
launch request times out, inspect the exact app/profile before retrying: the
browser may already have started.

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

DMG/PKG creation, signing, notarization, production profile identity, and automatic
updates remain outside this build CLI. `--packaging` builds the candidate and
matching upstream signing inputs only. Follow the separate
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
