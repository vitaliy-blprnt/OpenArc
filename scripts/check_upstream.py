#!/usr/bin/env python3
# Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
"""Read-only comparison with Google's active macOS ARM64 stable releases."""

from __future__ import annotations

from datetime import datetime, timezone
import http.client
import json
import math
from pathlib import Path
import re
import sys
import urllib.error
import urllib.request


SOURCE_URL = ("https://versionhistory.googleapis.com/v1/chrome/platforms/"
              "mac_arm64/channels/stable/versions/all/releases?filter=endtime=none")
RELEASE_PREFIX = "chrome/platforms/mac_arm64/channels/stable/versions/"
MAX_BYTES = 1024 * 1024
TIMEOUT_SECONDS = 15
VERSION = re.compile(r"(?:0|[1-9][0-9]{0,9})(?:\.(?:0|[1-9][0-9]{0,9})){3}\Z")
TIMESTAMP = re.compile(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?(?:Z|[+-]\d{2}:\d{2})\Z")


class CheckError(Exception):
    """Only controlled, non-sensitive diagnostic text reaches JSON output."""


def version_parts(value: object) -> tuple[int, ...]:
    if not isinstance(value, str) or not VERSION.fullmatch(value):
        raise CheckError("Expected a canonical four-component numeric version.")
    parts = tuple(int(part) for part in value.split("."))
    if any(part > 0xFFFFFFFF for part in parts):
        raise CheckError("Version component exceeds the supported numeric range.")
    return parts


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise CheckError("JSON contains duplicate object keys.")
        result[key] = value
    return result


def reject_constant(value: str) -> None:
    raise CheckError("JSON contains a non-finite constant.")


def decode_json(body: bytes) -> object:
    if len(body) > MAX_BYTES:
        raise CheckError("Input exceeds the 1 MiB response limit.")
    try:
        return json.loads(body.decode("utf-8"), object_pairs_hook=unique_object,
                          parse_constant=reject_constant)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise CheckError("Input is not valid UTF-8 JSON.") from error


def timestamp(value: object) -> datetime:
    if not isinstance(value, str) or not TIMESTAMP.fullmatch(value):
        raise CheckError("Release timestamp is missing or invalid.")
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise CheckError("Release timestamp is invalid.") from error


def active_releases(payload: object, now: datetime) -> list[dict]:
    if not isinstance(payload, dict) or not isinstance(payload.get("releases"), list):
        raise CheckError("Response must contain a releases array.")
    token = payload.get("nextPageToken", "")
    if not isinstance(token, str) or token:
        raise CheckError("Response is incomplete or paginated; highest active version is unknown.")
    active = []
    for item in payload["releases"]:
        if not isinstance(item, dict):
            raise CheckError("Release record must be an object.")
        version = item.get("version")
        version_parts(version)
        expected_name = re.escape(RELEASE_PREFIX + version + "/releases/") + r"[0-9]+\Z"
        if not isinstance(item.get("name"), str) or not re.fullmatch(expected_name, item["name"]):
            raise CheckError("Release name does not match the requested platform, channel, and version.")
        serving = item.get("serving")
        if not isinstance(serving, dict):
            raise CheckError("Release serving interval is missing.")
        start = timestamp(serving.get("startTime"))
        end = timestamp(serving["endTime"]) if "endTime" in serving else None
        if end is not None and end < start:
            raise CheckError("Release serving interval is reversed.")
        fraction = item.get("fraction")
        if (isinstance(fraction, bool) or not isinstance(fraction, (int, float))
                or not 0 <= fraction <= 1 or not math.isfinite(fraction)):
            raise CheckError("Release fraction must be finite and between zero and one.")
        if start <= now and end is None and fraction > 0:
            active.append({"version": version, "fraction": fraction})
    if not active:
        raise CheckError("Response contains no currently active stable release.")
    return active


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        return None


def fetch_releases(open_url=None) -> object:
    if open_url is None:
        open_url = urllib.request.build_opener(NoRedirect()).open
    request = urllib.request.Request(SOURCE_URL, headers={"Accept": "application/json",
                                                         "User-Agent": "OpenArc-upstream-check/1"})
    try:
        with open_url(request, timeout=TIMEOUT_SECONDS) as response:
            if response.status != 200:
                raise CheckError("Version History returned a non-success HTTP status.")
            if response.headers.get_content_type() != "application/json":
                raise CheckError("Version History did not return JSON content.")
            body = response.read(MAX_BYTES + 1)
    except urllib.error.HTTPError as error:
        error.close()
        raise CheckError("Version History returned a non-success HTTP status.") from error
    except (urllib.error.URLError, OSError, TimeoutError, http.client.HTTPException) as error:
        raise CheckError("Version History request failed or timed out.") from error
    return decode_json(body)


def check(lock_path: Path, open_url=None, now: datetime | None = None) -> tuple[dict, int]:
    now = now or datetime.now(timezone.utc)
    report = {"status": "unknown", "source_url": SOURCE_URL,
              "checked_at": now.isoformat(), "locked_version": None,
              "latest_active_version": None, "active_versions": [],
              "latest_active_fractions": []}
    try:
        try:
            with lock_path.open("rb") as stream:
                lock = decode_json(stream.read(MAX_BYTES + 1))
        except OSError as error:
            raise CheckError("Could not read upstream.lock.") from error
        if (not isinstance(lock, dict) or type(lock.get("schema_version")) is not int
                or lock["schema_version"] != 1):
            raise CheckError("Unsupported upstream.lock schema.")
        build = lock.get("build")
        if not isinstance(build, dict) or (build.get("target_os"), build.get("target_cpu")) != ("mac", "arm64"):
            raise CheckError("This checker supports only a macOS ARM64 lock.")
        chromium = lock.get("chromium")
        if not isinstance(chromium, dict):
            raise CheckError("Lock has no Chromium version record.")
        locked = chromium.get("version")
        locked_parts = version_parts(locked)
        report["locked_version"] = locked
        releases = active_releases(fetch_releases(open_url), now)
        versions = sorted({release["version"] for release in releases}, key=version_parts)
        latest = versions[-1]
        report.update(latest_active_version=latest, active_versions=versions,
                      latest_active_fractions=sorted({release["fraction"] for release in releases
                                                      if release["version"] == latest}))
        if locked_parts > version_parts(latest):
            raise CheckError("Locked version is newer than every active release; stable status is unknown.")
        if locked_parts < version_parts(latest):
            report.update(status="update-available", detail="A higher version is active in at least one stable rollout cohort; this does not select or qualify an upgrade.")
            return report, 1
        report.update(status="current", detail="Lock matches the highest active stable version observed; this does not qualify a build or update delivery.")
        return report, 0
    except CheckError as error:
        report["detail"] = str(error)
        return report, 2


def main() -> int:
    report, code = check(Path(__file__).resolve().parents[1] / "upstream.lock")
    print(json.dumps(report, sort_keys=True, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
