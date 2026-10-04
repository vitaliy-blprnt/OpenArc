"""Dry synthetic native-host checks. No browser, credentials or system registration."""

import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[2] / "scripts" / "native_probe.py"
SPEC = importlib.util.spec_from_file_location("native_probe", MODULE_PATH)
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)
EXTENSION_ID = "abcdefghijklmnopabcdefghijklmnop"


def frame(body):
    if not isinstance(body, bytes):
        body = json.dumps(body, ensure_ascii=False).encode("utf-8")
    return struct.pack("=I", len(body)) + body


class ChunkedInput(io.BytesIO):
    def read(self, count=-1):
        return super().read(min(count, 2))


class ProtocolTests(unittest.TestCase):
    def run_host(self, content, chunked=False):
        output, errors = io.BytesIO(), io.StringIO()
        reader = ChunkedInput(content) if chunked else io.BytesIO(content)
        status = probe.host_main(reader, output, errors)
        return status, output.getvalue(), errors.getvalue()

    def test_multiple_frames_and_fragmented_reads_echo_only_nonce(self):
        requests = [{"type": "ping", "nonce": "test-1"}, {"type": "ping", "nonce": "é" * 64}]
        status, output, errors = self.run_host(b"".join(frame(item) for item in requests), chunked=True)
        self.assertEqual(status, 0)
        self.assertEqual(errors, "")
        responses = io.BytesIO(output)
        for request in requests:
            self.assertEqual(json.loads(probe.read_frame(responses)),
                             {"type": "pong", "nonce": request["nonce"], "protocol": 1})
        self.assertIsNone(probe.read_frame(responses))

    def test_clean_empty_input_is_success_without_output(self):
        self.assertEqual(self.run_host(b""), (0, b"", ""))

    def test_invalid_or_truncated_input_fails_without_stdout(self):
        invalid = [b"\x01", struct.pack("=I", 0), struct.pack("=I", 0xFFFFFFFF),
                   struct.pack("=I", probe.MAX_MESSAGE_BYTES + 1), struct.pack("=I", 8) + b"{}",
                   frame(b"\xff"), frame(b"not-json"), frame(b'{"type":"ping","nonce":"a","nonce":"b"}'),
                   frame(b'{"type":"ping","nonce":NaN}'), frame([]),
                   frame({"type": "read-file", "nonce": "private"}),
                   frame({"type": "ping", "nonce": ""}), frame({"type": "ping", "nonce": True}),
                   frame({"type": "ping", "nonce": "a" * 129}), frame({"type": "ping", "nonce": "é" * 65}),
                   frame({"type": "ping", "nonce": "a", "path": "/private/data"}),
                   frame(b'{"type":"ping","nonce":"\\ud800"}')]
        for message in invalid:
            with self.subTest(message=message[:24]):
                status, output, errors = self.run_host(message)
                self.assertNotEqual(status, 0)
                self.assertEqual(output, b"")
                self.assertTrue(errors.startswith("native-probe: "))
                self.assertNotIn("/private/data", errors)

    def test_host_subcommand_has_only_framed_stdout(self):
        result = subprocess.run([sys.executable, str(MODULE_PATH), "host"],
                                input=frame({"type": "ping", "nonce": "subprocess"}), capture_output=True)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, b"")
        self.assertEqual(json.loads(probe.read_frame(io.BytesIO(result.stdout))),
                         {"type": "pong", "nonce": "subprocess", "protocol": 1})


class InstallerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        (self.root / "scripts").mkdir()
        shutil.copyfile(MODULE_PATH, self.root / "scripts/native_probe.py")

    def installer(self, mode="baseline", extension_id=EXTENSION_ID):
        return probe.Installer(self.root, mode, extension_id)

    def test_install_is_scoped_idempotent_and_exact_origin_only(self):
        for mode, directory in (("baseline", "baseline"), ("development", "development"),
                                ("reference", "fixture-reference"), ("packaging", "packaging")):
            installer = self.installer(mode)
            report = installer.install()
            before = installer.manifest.read_bytes()
            installer.install()
            self.assertEqual(installer.manifest.read_bytes(), before)
            self.assertEqual(installer.manifest, self.root / ".build/profiles" / directory /
                             "NativeMessagingHosts/org.openarc.platform_probe.json")
            manifest = json.loads(before)
            self.assertEqual(manifest["allowed_origins"], ["chrome-extension://" + EXTENSION_ID + "/"])
            self.assertEqual(manifest["path"], str(installer.wrapper))
            self.assertTrue(os.access(installer.wrapper, os.X_OK))
            self.assertIn("unverified", report["qualification"])
            self.assertFalse((self.root / "Library").exists())

    def test_reference_cli_preserves_other_profiles_and_unrelated_reference_data(self):
        preserved = []
        for directory in ("baseline", "development", "packaging"):
            path = self.root / ".build/profiles" / directory / "NativeMessagingHosts" / (probe.HOST_NAME + ".json")
            path.parent.mkdir(parents=True)
            path.write_text("another registration")
            preserved.append(path)
        unrelated = self.root / ".build/profiles/fixture-reference/Preferences"
        unrelated.parent.mkdir(parents=True)
        unrelated.write_text("reference browser data")
        script = self.root / "scripts/native_probe.py"
        installed = subprocess.run([sys.executable, str(script), "install", "--reference",
                                    "--extension-id", EXTENSION_ID], capture_output=True, text=True)
        self.assertEqual(installed.returncode, 0, installed.stderr)
        report = json.loads(installed.stdout)
        manifest = self.root / ".build/profiles/fixture-reference/NativeMessagingHosts" / (probe.HOST_NAME + ".json")
        wrapper = self.root / ".build/tools/native-probe-reference"
        self.assertEqual(report["mode"], "reference")
        self.assertEqual(report["manifest"], str(manifest))
        self.assertEqual(report["wrapper"], str(wrapper))
        self.assertTrue(manifest.is_file())
        self.assertTrue(wrapper.is_file())
        self.assertFalse((self.root / ".build/profiles/reference").exists())
        self.assertIn("do not qualify OpenArc", report["qualification"])
        removed = subprocess.run([sys.executable, str(script), "uninstall", "--reference",
                                  "--extension-id", EXTENSION_ID], capture_output=True, text=True)
        self.assertEqual(removed.returncode, 0, removed.stderr)
        self.assertEqual(set(json.loads(removed.stdout)["removed"]), {str(manifest), str(wrapper)})
        self.assertEqual(unrelated.read_text(), "reference browser data")
        for path in preserved:
            self.assertEqual(path.read_text(), "another registration")
        self.assertFalse((self.root / "Library").exists())

    def test_reference_and_packaging_symlinks_cannot_redirect_registration(self):
        baseline = self.root / ".build/profiles/baseline"
        baseline.mkdir(parents=True)
        for mode, directory in (("reference", "fixture-reference"), ("packaging", "packaging")):
            with self.subTest(mode=mode):
                (baseline.parent / directory).symlink_to(baseline, target_is_directory=True)
                with self.assertRaisesRegex(probe.ProbeError, "symlink"):
                    self.installer(mode).install()
                self.assertFalse((baseline / "NativeMessagingHosts").exists())
                self.assertFalse((self.root / ".build/tools").exists())

    def test_uninstall_removes_only_matching_owned_files(self):
        installer = self.installer()
        installer.install()
        unrelated = installer.manifest.parent / "another-host.json"
        unrelated.write_text("preserve")
        report = installer.uninstall()
        self.assertEqual(set(report["removed"]), {str(installer.manifest), str(installer.wrapper)})
        self.assertEqual(unrelated.read_text(), "preserve")
        self.assertEqual(installer.uninstall()["removed"], [])

    def test_unrecognized_existing_manifest_prevents_any_wrapper_write(self):
        installer = self.installer()
        installer.manifest.parent.mkdir(parents=True)
        installer.manifest.write_text("belongs to someone else")
        with self.assertRaisesRegex(probe.ProbeError, "unrecognized"):
            installer.install()
        self.assertFalse(installer.wrapper.exists())
        self.assertEqual(installer.manifest.read_text(), "belongs to someone else")

    def test_changed_file_and_different_extension_id_prevent_uninstall(self):
        installer = self.installer()
        installer.install()
        with self.assertRaisesRegex(probe.ProbeError, "unrecognized"):
            self.installer(extension_id="p" * 32).uninstall()
        installer.wrapper.write_text("a different native program")
        with self.assertRaisesRegex(probe.ProbeError, "unrecognized"):
            installer.uninstall()
        self.assertTrue(installer.manifest.is_file())
        self.assertEqual(installer.wrapper.read_text(), "a different native program")

    def test_symlinks_cannot_escape_project_or_redirect_existing_files(self):
        outside = self.root / "outside"
        outside.mkdir()
        (self.root / ".build").symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(probe.ProbeError, "symlink"):
            self.installer()
        (self.root / ".build").unlink()
        installer = self.installer()
        installer.manifest.parent.mkdir(parents=True)
        target = outside / "important.json"
        target.write_text("preserve")
        installer.manifest.symlink_to(target)
        with self.assertRaisesRegex(probe.ProbeError, "symlink"):
            installer.install()
        self.assertEqual(target.read_text(), "preserve")

    def test_hardlinked_existing_file_is_not_modified(self):
        installer = self.installer()
        installer.wrapper.parent.mkdir(parents=True)
        original = self.root / "important"
        original.write_text(installer.wrapper_text)
        os.link(original, installer.wrapper)
        with self.assertRaisesRegex(probe.ProbeError, "non-owned"):
            installer.install()
        self.assertFalse(installer.manifest.exists())

    def test_invalid_extension_ids_cannot_change_registration(self):
        for extension_id in ("a" * 31, "q" * 32, "A" * 32, "a" * 32 + "/", "../escape"):
            with self.assertRaisesRegex(probe.ProbeError, "Extension ID"):
                self.installer(extension_id=extension_id)
        self.assertFalse((self.root / ".build").exists())

    def test_wrapper_quotes_paths_and_ignores_native_host_arguments(self):
        tricky = self.root / "space ' ; $(touch INJECTED)"
        (tricky / "scripts").mkdir(parents=True)
        shutil.copyfile(MODULE_PATH, tricky / "scripts/native_probe.py")
        installer = probe.Installer(tricky, "baseline", EXTENSION_ID)
        installer.install()
        result = subprocess.run([str(installer.wrapper), "chrome-extension://" + EXTENSION_ID + "/"],
                                cwd=self.root, input=frame({"type": "ping", "nonce": "quoted-path"}), capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(probe.read_frame(io.BytesIO(result.stdout)))["nonce"], "quoted-path")
        self.assertFalse((self.root / "INJECTED").exists())
        self.assertIn(sys.executable, installer.wrapper_text)


if __name__ == "__main__":
    unittest.main()
