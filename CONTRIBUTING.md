# Contributing to OpenArc

OpenArc is an early Chromium browser project for macOS. Its core scope is a left
sidebar, saved tabs that open in place, Spaces, and the upstream extension
platform. AI features are outside the product scope. Read the
[browser plan](docs/BROWSER-PLAN.md) before proposing architectural changes.

## Start here

1. Use the [build guide](docs/BUILDING.md) for the current tooling and its limits.
2. For a bug, include the OpenArc commit, locked Chromium revision, macOS version,
   CPU architecture, reproduction steps, and observed versus expected behavior.
3. For larger changes, open a focused design issue describing the user problem,
   affected data, and intended behavior before implementing it.
4. Report vulnerabilities through [SECURITY.md](SECURITY.md), not public issues.

Use a dedicated development profile. Do not point development builds at an
existing Chrome, Chromium, Arc, or daily-use profile. Reports and fixtures must
exclude credentials, cookies, personal history, profile databases, and private
URLs. Use synthetic data when reproducing persistence and import problems.

## Change boundaries

Keep browser organization in OpenArc's browser-layer modules and integration
patches focused. Preserve Chromium's renderer sandbox, site isolation, extension
permissions, real tab/window IDs, profile boundaries, and native prompts. Spaces
organize tabs; they do not create a separate security boundary.

Saved entries and live tabs have different lifetimes. A change to closing,
moving, restoring, or deleting them must follow the behavior contract and preserve
pending page work. Include migration and recovery behavior when persistent data
changes. Private browsing must not write private URLs into ordinary workspace
state, backups, exports, or diagnostics.

Full upstream extension support remains a requirement. A tested extension set
provides evidence, not an allowlist or proof that every extension works. Keep
failures visible, including native messaging, signed-app trust, and store/update
flows. Do not bypass vendor trust checks or weaken Chromium security to make a
test pass.

## Checks and review

Run the repository checks in [BUILDING.md](docs/BUILDING.md). Add focused tests
for changed behavior and meaningful failure cases. Tooling tests do not validate
browser behavior; browser changes need the relevant Chromium tests and actual
application checks described in the plan. Document checks that could not run.

A pull request should explain the problem, the resulting behavior, validation,
and any known limitations. Include screenshots for visible UI changes and
reproduction fixtures for state transitions. Keep unrelated cleanup separate.
Reviewers should be able to identify the exact source and build used for evidence.

## Licensing

Submit only work you have the right to contribute. Original contributions are
provided under the repository's [BSD-3-Clause license](LICENSE). Preserve existing
upstream notices and document new third-party code or assets in
[THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Linked design references are not a
license to copy another product's branding or assets.
