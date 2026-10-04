"""Read-only version-check tests; all HTTP responses are in-memory fixtures."""

from datetime import datetime, timezone
from email.message import Message
import http.client
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import urllib.error


MODULE_PATH = Path(__file__).resolve().parents[2] / "scripts/check_upstream.py"
SPEC = importlib.util.spec_from_file_location("check_upstream", MODULE_PATH)
upstream = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(upstream)
NOW = datetime(2026, 10, 3, tzinfo=timezone.utc)


def release(version="154.0.8037.9", fraction=1, **changes):
    value = {"version": version, "name": upstream.RELEASE_PREFIX + version + "/releases/123",
             "serving": {"startTime": "2026-10-02T00:00:00Z"}, "fraction": fraction}
    value.update(changes)
    return value


class Response(io.BytesIO):
    def __init__(self, payload, status=200, content_type="application/json"):
        super().__init__(payload if isinstance(payload, bytes) else json.dumps(payload).encode())
        self.status = status
        self.headers = Message()
        self.headers["Content-Type"] = content_type
        self.read_limit = None

    def read(self, size=-1):
        self.read_limit = size
        return super().read(size)


class UpstreamCheckTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.lock_path = Path(temporary.name) / "upstream.lock"
        self.lock = {"schema_version": 1, "chromium": {"version": "154.0.8037.9"},
                     "build": {"target_os": "mac", "target_cpu": "arm64"}}
        self.write_lock()

    def write_lock(self):
        self.lock_path.write_text(json.dumps(self.lock))

    def run_check(self, payload=None, response=None):
        response = response or Response(payload)
        original = self.lock_path.read_bytes()

        def open_url(request, timeout):
            self.assertEqual(request.full_url, upstream.SOURCE_URL)
            self.assertEqual(request.get_method(), "GET")
            self.assertEqual(timeout, upstream.TIMEOUT_SECONDS)
            return response

        result = upstream.check(self.lock_path, open_url, NOW)
        self.assertEqual(self.lock_path.read_bytes(), original)
        self.assertEqual(response.read_limit, upstream.MAX_BYTES + 1)
        return result

    def test_current_reports_source_and_preserves_lock(self):
        report, status = self.run_check({"releases": [release()], "nextPageToken": ""})
        self.assertEqual(status, 0)
        self.assertEqual(report["status"], "current")
        self.assertEqual(report["source_url"], upstream.SOURCE_URL)
        self.assertEqual(report["latest_active_version"], "154.0.8037.9")

    def test_highest_numeric_active_version_includes_small_rollout(self):
        report, status = self.run_check({"releases": [release("154.0.8037.10", 0.005),
                                                                  release(), release("154.0.8037.2")]})
        self.assertEqual(status, 1)
        self.assertEqual(report["status"], "update-available")
        self.assertEqual(report["latest_active_version"], "154.0.8037.10")
        self.assertEqual(report["latest_active_fractions"], [0.005])
        self.assertEqual(report["active_versions"], ["154.0.8037.2", "154.0.8037.9", "154.0.8037.10"])

    def test_locked_ahead_of_active_is_unknown_not_current(self):
        report, status = self.run_check({"releases": [release("153.0.8037.999")]})
        self.assertEqual((report["status"], status), ("unknown", 2))
        self.assertIsNotNone(report["latest_active_version"])

    def test_empty_malformed_or_incomplete_responses_are_unknown(self):
        for payload in [None, [], {}, {"releases": {}}, {"releases": []},
                        {"releases": [None]}, {"releases": [release(), {}]},
                        {"releases": [release()], "nextPageToken": "another-page"},
                        {"releases": [release()], "nextPageToken": None}]:
            with self.subTest(payload=payload):
                report, status = self.run_check(payload)
                self.assertEqual((report["status"], status), ("unknown", 2))

    def test_invalid_version_and_release_identity_fail_closed(self):
        for version in [None, 154, "154.0.9", "154.0.0.9.1", "154.0.0.-1", "154.0.0.9beta",
                        "154.0.0.09", "154.0.0.4294967296", "154.0.0.9\n"]:
            with self.subTest(version=version):
                with self.assertRaises(upstream.CheckError):
                    upstream.version_parts(version)
        for name in [None, "chrome/platforms/mac/channels/stable/versions/154.0.8037.9/releases/1",
                     upstream.RELEASE_PREFIX + "154.0.8037.10/releases/1"]:
            report, status = self.run_check({"releases": [release(name=name)]})
            self.assertEqual((report["status"], status), ("unknown", 2))

    def test_inactive_zero_ended_and_future_records_are_excluded(self):
        entries = [release(), release("155.0.0.1", 0),
                   release("156.0.0.1", serving={"startTime": "2026-10-02T00:00:00Z",
                                                "endTime": "2026-10-02T02:00:00Z"}),
                   release("157.0.0.1", serving={"startTime": "2026-10-04T00:00:00Z"})]
        report, status = self.run_check({"releases": entries})
        self.assertEqual(status, 0)
        self.assertEqual(report["active_versions"], ["154.0.8037.9"])

    def test_bad_fraction_or_interval_rejects_whole_response(self):
        changes = [{"fraction": value} for value in [None, True, "1", -1, 1.1, 10 ** 400]]
        changes += [{"serving": value} for value in [None, {}, {"startTime": "today"},
                    {"startTime": "2026-10-02T00:00:00"},
                    {"startTime": "2026-13-02T00:00:00Z"},
                    {"startTime": "2026-10-02T00:00:00Z", "endTime": "2026-10-01T00:00:00Z"}]]
        for change in changes:
            report, status = self.run_check({"releases": [release(), release(**change)]})
            self.assertEqual((report["status"], status), ("unknown", 2))

    def test_invalid_json_encoding_duplicates_constants_and_size(self):
        for body in [b"not-json", b"\xff", b'{"releases":[],"releases":[]}',
                     b'{"releases":NaN}', b" " * (upstream.MAX_BYTES + 1)]:
            report, status = self.run_check(response=Response(body))
            self.assertEqual((report["status"], status), ("unknown", 2))

    def test_network_timeout_http_and_truncation_errors_are_non_success(self):
        for error in [TimeoutError("private-network-context"),
                      urllib.error.URLError("private-network-context"),
                      urllib.error.HTTPError(upstream.SOURCE_URL, 503, "private", None, None),
                      http.client.IncompleteRead(b"private", 10)]:
            def open_url(request, timeout):
                raise error
            report, status = upstream.check(self.lock_path, open_url, NOW)
            self.assertEqual((report["status"], status), ("unknown", 2))
            self.assertNotIn("private", report["detail"])

    def test_http_status_and_content_type_are_validated_before_body(self):
        for response in [Response({}, status=503), Response({}, content_type="text/html")]:
            report, status = upstream.check(self.lock_path, lambda *args, **kwargs: response, NOW)
            self.assertEqual((report["status"], status), ("unknown", 2))
            self.assertIsNone(response.read_limit)

    def test_bad_lock_is_unknown_without_network_access(self):
        def forbidden_network(*args, **kwargs):
            self.fail("Invalid lock must not trigger a request")
        for value in [None, {}, {**self.lock, "schema_version": True},
                      {**self.lock, "build": {"target_os": "mac", "target_cpu": "x64"}},
                      {**self.lock, "chromium": {"version": "garbage"}}]:
            self.lock_path.write_text(json.dumps(value))
            report, status = upstream.check(self.lock_path, forbidden_network, NOW)
            self.assertEqual((report["status"], status), ("unknown", 2))
        self.lock_path.unlink()
        report, status = upstream.check(self.lock_path, forbidden_network, NOW)
        self.assertEqual((report["status"], status), ("unknown", 2))

    def test_redirect_is_rejected(self):
        self.assertIsNone(upstream.NoRedirect().redirect_request(None, None, 302, "", {},
                                                                "https://another.example/"))


if __name__ == "__main__":
    unittest.main()
