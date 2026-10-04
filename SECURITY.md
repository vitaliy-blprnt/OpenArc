# Security policy

## Supported status

OpenArc is in initial development. No browser release is currently qualified or
supported for daily use. A passing source or tooling check does not establish a
secure browsing release. Supported versions and their exact Chromium revisions
will be listed here when a release process is qualified.

## Report a vulnerability privately

Use **Report a vulnerability** in this repository's GitHub Security area when
private vulnerability reporting is enabled. That setting is separate from this
file; the presence of this policy does not imply it has been enabled.
[GitHub's reporting instructions](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/report-privately)

If the private report option is unavailable, use a private contact method
published on a repository maintainer's GitHub profile. If none is available, open
an issue asking only for a private security contact. Do not include vulnerability
details, exploit code, credentials, or sensitive data in that public request.
There is no project security mailbox or guaranteed response schedule yet.

Include the affected OpenArc commit/release and Chromium revision, platform,
impact, and a minimal reproduction using synthetic data. Identify whether the
problem also occurs in upstream Chromium if you have already checked; reproducing
it upstream is not a condition for reporting it. Never send an entire real browser
profile, saved passwords, session cookies, tokens, or private browsing history.

For a vulnerability known to be in upstream Chromium, also use Chromium's private
[security reporting route](https://www.chromium.org/Home/chromium-security/reporting-security-bugs/).
Reference an existing private report without publishing its restricted contents.

## Maintainer handling

Keep reports and reproduction artifacts private while investigating. Coordinate
upstream fixes when applicable and publish an advisory with affected/fixed
versions once disclosure is appropriate. Do not claim an issue is resolved until
the fix is verified in the affected build configuration.

Before a supported release, enable and test private reporting, establish current
maintainer contact information, and document security-update ownership. Follow
the release gates in [RELEASING.md](docs/RELEASING.md); do not ship stale Chromium
builds as supported merely because the OpenArc interface still runs.
