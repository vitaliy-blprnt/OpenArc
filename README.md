# OpenArc

OpenArc is an open-source, macOS-first Chromium browser project built around a left sidebar: persistent saved tabs and folders above, open tabs below, and Spaces for separate activities. Saved tabs open in place. The product has no browser AI features.

Full upstream Chrome extension support is a core requirement. Compatibility has
not yet been demonstrated in an OpenArc binary. OpenArc preserves Chromium's
browser platform rather than implementing a subset of extension APIs.

## Development status

The initial foundation includes a pinned Chromium/depot_tools checkout workflow,
an isolated development launcher, an ordered patch series for OpenArc's macOS
identity and native vertical-tab default, and tooling tests. The Arc-style saved
tab lifecycle and Spaces are not implemented yet. There is no qualified browser
release or download.

See [current status and evidence](docs/STATUS.md) for build and validation results.
Source/tooling checks, browser compilation, visible UI, extension compatibility,
and signed release qualification are tracked separately.

## Build from source

On an Apple Silicon Mac with Git, Python 3, and suitable Xcode:

```sh
python3 scripts/openarc.py check
python3 scripts/openarc.py doctor
python3 scripts/openarc.py fetch
python3 scripts/openarc.py sync
python3 scripts/openarc.py apply
python3 scripts/openarc.py build
python3 scripts/openarc.py launch
```

Chromium source, dependencies, and compiler output are large. Read the
[build guide](docs/BUILDING.md) before fetching. The pinned development build is
not an official signed release. Existing Chrome and Arc profiles are not used.

## Project documentation

- [Browser plan](docs/BROWSER-PLAN.md): confirmed requirements, proposed behavior, architecture, and dependency-ordered implementation gates.
- [Arc interaction research](docs/research/arc-interactions.md): first-party UX research and visual references.
- [Chromium foundations](docs/research/chromium-foundations.md): technical options, compatibility, and maintenance requirements.
- [Extension compatibility gate](docs/EXTENSION-COMPATIBILITY.md): required platform and native-integration checks.
- [Contributing](CONTRIBUTING.md), [security policy](SECURITY.md), and [release process](docs/RELEASING.md).

## License and attribution

Original OpenArc code is licensed under [BSD-3-Clause](LICENSE). Chromium and
third-party components retain their own licenses; see [notices](THIRD_PARTY_NOTICES).
Arc is a design reference. OpenArc is an independent project and does not include
Arc's proprietary source code or brand assets.
