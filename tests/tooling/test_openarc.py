"""Safety and workflow tests using disposable repositories; no network or Chromium build."""

import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


MODULE_PATH = Path(__file__).resolve().parents[2] / "scripts" / "openarc.py"
SPEC = importlib.util.spec_from_file_location("openarc", MODULE_PATH)
openarc = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(openarc)

LOCK = {
    "schema_version": 1,
    "chromium": {"repository": openarc.CHROMIUM_URL, "revision": "a" * 40, "version": "154.0.0.0"},
    "depot_tools": {"repository": openarc.DEPOT_URL, "revision": "b" * 40},
    "build": {"target_os": "mac", "target_cpu": "arm64", "output_dir": "out/OpenArc",
              "gn_args": {"target_cpu": "arm64", "is_debug": False, "is_component_build": True,
                          "symbol_level": 0, "chrome_pgo_phase": 0}},
}


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], text=True, capture_output=True, check=True).stdout.strip()


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()
        self.lock = copy.deepcopy(LOCK)
        self.write_lock()
        self.series = self.root / "patches" / "chromium" / "series"
        self.series.parent.mkdir(parents=True)
        self.series.write_text("")

    def write_lock(self):
        (self.root / "upstream.lock").write_text(json.dumps(self.lock))

    def workflow(self):
        return openarc.Workflow(self.root)

    def repository(self):
        repo = self.root / ".build" / "chromium" / "src"
        repo.mkdir(parents=True)
        git(repo, "init")
        git(repo, "config", "user.name", "OpenArc Tooling Test")
        git(repo, "config", "user.email", "test@example.invalid")
        git(repo, "config", "commit.gpgsign", "false")
        git(repo, "config", "core.hooksPath", os.devnull)
        git(repo, "config", "core.autocrlf", "false")
        (repo / "example.txt").write_text("base\n")
        (repo / ".gitignore").write_text("out/\nthird_party/\n")
        branding = repo / "chrome/app/theme/chromium/BRANDING"
        branding.parent.mkdir(parents=True)
        branding.write_text("PRODUCT_FULLNAME=Chromium\n")
        git(repo, "add", ".")
        git(repo, "commit", "-m", "fixture")
        self.lock["chromium"]["revision"] = git(repo, "rev-parse", "HEAD")
        git(repo, "clone", "--local", str(repo), str(repo.parent.parent / "depot_tools"))
        self.lock["depot_tools"]["revision"] = self.lock["chromium"]["revision"]
        (repo.parent / ".gclient_entries").write_text("entries = " + repr({"src": openarc.CHROMIUM_URL}) + "\n")
        self.write_lock()
        return repo

    def add_patch(self, name, before, after):
        path = self.series.parent / name
        path.write_text("diff --git a/example.txt b/example.txt\n"
                        "--- a/example.txt\n+++ b/example.txt\n@@ -1 +1 @@\n"
                        f"-{before}\n+{after}\n")
        with self.series.open("a") as file:
            file.write(name + "\n")
        return path

    def executable(self, workflow, name="Chromium", output=None):
        executable = (output or workflow.output) / f"{name}.app" / "Contents" / "MacOS" / name
        executable.parent.mkdir(parents=True, exist_ok=True)
        executable.write_text("#!/bin/sh\nexit 0\n")
        executable.chmod(0o755)
        return executable

    def simulated_build(self, workflow, baseline=False, reuse_baseline=False):
        """Run real validation/receipts with only GN/compiler invocation simulated."""
        real_run = workflow.run
        compiler_calls = []

        def runner(args, **kwargs):
            if args[0] in (str(workflow.depot / "gn"), str(workflow.depot / "autoninja")):
                compiler_calls.append(args)
                return subprocess.CompletedProcess(args, 0, "", "")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "ensure_bootstrap"), patch.object(workflow, "run", side_effect=runner):
            workflow.build(2, baseline, reuse_baseline)
        return compiler_calls

    def apply_identity(self, workflow):
        path = self.series.parent / "identity.patch"
        name = "chrome/app/theme/chromium/BRANDING"
        path.write_text(f"diff --git a/{name} b/{name}\n--- a/{name}\n+++ b/{name}\n"
                        "@@ -1 +1 @@\n-PRODUCT_FULLNAME=Chromium\n+PRODUCT_FULLNAME=OpenArc\n")
        self.series.write_text("identity.patch\n")
        workflow.apply()

    def baseline_for_promotion(self):
        self.repository()
        workflow = self.workflow()
        executable = self.executable(workflow, output=workflow.baseline_output)
        self.simulated_build(workflow, baseline=True)
        self.apply_identity(workflow)
        return workflow, executable

    def add_dependency(self, repo):
        dependency = repo / "third_party" / "example"
        dependency.parent.mkdir(parents=True)
        git(repo, "clone", "--local", str(repo), str(dependency))
        git(dependency, "config", "user.name", "OpenArc Tooling Test")
        git(dependency, "config", "user.email", "test@example.invalid")
        git(dependency, "config", "commit.gpgsign", "false")
        git(dependency, "config", "core.hooksPath", os.devnull)
        (repo.parent / ".gclient_entries").write_text("entries = " + repr({
            "src": openarc.CHROMIUM_URL, "src/third_party/example": openarc.CHROMIUM_URL}) + "\n")
        return dependency

    def test_static_check_needs_no_checkout_or_macos(self):
        with patch.object(openarc.sys, "platform", "linux"):
            self.workflow().check()
        self.assertFalse((self.root / ".build").exists())

    def test_static_check_does_not_inspect_inflight_fetch(self):
        repo = self.root / ".build/chromium/src"
        repo.mkdir(parents=True)
        git(repo, "init")
        self.workflow().check()

    def test_static_check_rejects_invalid_patch_syntax(self):
        (self.series.parent / "invalid.patch").write_text("not a patch\n")
        self.series.write_text("invalid.patch\n")
        with self.assertRaises(openarc.WorkflowError):
            self.workflow().check()

    def test_moving_refs_and_external_remotes_are_rejected(self):
        for revision in ("main", "refs/tags/154.0.0.0", "a" * 39, "A" * 40):
            self.lock["chromium"]["revision"] = revision
            self.write_lock()
            with self.assertRaisesRegex(openarc.WorkflowError, "exact 40-character"):
                self.workflow()
        self.lock = copy.deepcopy(LOCK)
        self.lock["chromium"]["repository"] = "https://example.invalid/source.git"
        self.write_lock()
        with self.assertRaisesRegex(openarc.WorkflowError, "repository must be"):
            self.workflow()

    def test_managed_paths_cannot_escape_or_redirect(self):
        for path in ("/tmp/output", "out/../../outside", "../outside", "out", "out/./OpenArc", "out/Baseline"):
            self.lock["build"]["output_dir"] = path
            self.write_lock()
            with self.assertRaises(openarc.WorkflowError):
                self.workflow()
        self.lock = copy.deepcopy(LOCK)
        self.write_lock()
        destination = self.root / "outside"
        destination.mkdir()
        (self.root / ".build").symlink_to(destination, target_is_directory=True)
        with self.assertRaisesRegex(openarc.WorkflowError, "symlink"):
            self.workflow()

    def test_sandbox_disabling_and_brand_impersonation_are_rejected(self):
        for key, value in (("use_sandbox", False), ("is_chrome_branded", True)):
            self.lock = copy.deepcopy(LOCK)
            self.lock["build"]["gn_args"][key] = value
            self.write_lock()
            with self.assertRaisesRegex(openarc.WorkflowError, "not supported"):
                self.workflow()

    def test_patch_stack_is_idempotent_even_when_patches_overlap(self):
        repo = self.repository()
        self.add_patch("01.patch", "base", "first")
        self.add_patch("02.patch", "first", "second")
        workflow = self.workflow()
        workflow.apply()
        before = git(repo, "diff", "--cached")
        workflow.apply()
        self.assertEqual((repo / "example.txt").read_text(), "second\n")
        self.assertEqual(git(repo, "diff", "--cached"), before)
        self.assertEqual(len(json.loads(workflow.state_file.read_text())["applied"]), 2)

    def test_existing_user_changes_are_never_overwritten(self):
        repo = self.repository()
        self.add_patch("01.patch", "base", "first")
        (repo / "example.txt").write_text("valuable user work\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "Preserving modified"):
            self.workflow().apply()
        self.assertEqual((repo / "example.txt").read_text(), "valuable user work\n")
        self.assertFalse((self.root / ".build" / "patch-state.json").exists())

    def test_edits_after_patching_are_preserved(self):
        repo = self.repository()
        self.add_patch("01.patch", "base", "first")
        workflow = self.workflow()
        workflow.apply()
        (repo / "example.txt").write_text("new user work\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "preserving local work"):
            workflow.apply()
        self.assertEqual((repo / "example.txt").read_text(), "new user work\n")

    def test_changed_applied_patch_is_not_silently_accepted(self):
        repo = self.repository()
        path = self.add_patch("01.patch", "base", "first")
        workflow = self.workflow()
        workflow.apply()
        path.write_text(path.read_text().replace("+first", "+different"))
        with self.assertRaisesRegex(openarc.WorkflowError, "does not match"):
            workflow.apply()
        self.assertEqual((repo / "example.txt").read_text(), "first\n")

    def test_failed_later_patch_retains_recorded_recoverable_prefix(self):
        repo = self.repository()
        self.add_patch("01.patch", "base", "first")
        self.add_patch("02.patch", "not-the-content", "second")
        workflow = self.workflow()
        with self.assertRaises(openarc.WorkflowError):
            workflow.apply()
        self.assertEqual((repo / "example.txt").read_text(), "first\n")
        self.assertEqual(len(json.loads(workflow.state_file.read_text())["applied"]), 1)
        with self.assertRaises(openarc.WorkflowError):
            workflow.apply()
        self.assertEqual((repo / "example.txt").read_text(), "first\n")

    def test_unapply_reverses_overlapping_stack_and_repeat_is_harmless(self):
        repo = self.repository()
        before = (repo / "example.txt").read_bytes()
        self.add_patch("01.patch", "base", "first")
        self.add_patch("02.patch", "first", "second")
        workflow = self.workflow()
        workflow.apply()
        workflow.unapply()
        self.assertEqual((repo / "example.txt").read_bytes(), before)
        self.assertEqual(git(repo, "status", "--porcelain"), "")
        state_bytes = workflow.state_file.read_bytes()
        self.assertEqual(json.loads(state_bytes)["applied"], [])
        workflow.unapply()
        self.assertEqual(workflow.state_file.read_bytes(), state_bytes)
        workflow.apply()
        self.assertEqual((repo / "example.txt").read_text(), "second\n")

    def test_unapply_removes_only_applied_prefix_after_failed_application(self):
        repo = self.repository()
        self.add_patch("01.patch", "base", "first")
        self.add_patch("02.patch", "different", "second")
        workflow = self.workflow()
        with self.assertRaises(openarc.WorkflowError):
            workflow.apply()
        workflow.unapply()
        self.assertEqual((repo / "example.txt").read_text(), "base\n")
        self.assertEqual(git(repo, "status", "--porcelain"), "")
        self.assertEqual(json.loads(workflow.state_file.read_text())["applied"], [])

    def test_unapply_preserves_unknown_staged_unstaged_and_untracked_work(self):
        repo = self.repository()
        self.add_patch("01.patch", "base", "first")
        workflow = self.workflow()
        workflow.apply()
        (repo / "example.txt").write_text("valuable staged edit\n")
        git(repo, "add", "example.txt")
        (repo / "example.txt").write_text("valuable unstaged edit\n")
        (repo / "notes.txt").write_text("valuable untracked file\n")
        state = workflow.state_file.read_bytes()
        staged = git(repo, "diff", "--cached")
        with self.assertRaisesRegex(openarc.WorkflowError, "preserving local work"):
            workflow.unapply()
        self.assertEqual((repo / "example.txt").read_text(), "valuable unstaged edit\n")
        self.assertEqual((repo / "notes.txt").read_text(), "valuable untracked file\n")
        self.assertEqual(git(repo, "diff", "--cached"), staged)
        self.assertEqual(workflow.state_file.read_bytes(), state)

    def test_unapply_rejects_changed_patch_digest_and_pin(self):
        repo = self.repository()
        path = self.add_patch("01.patch", "base", "first")
        workflow = self.workflow()
        workflow.apply()
        path.write_text(path.read_text() + "\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "does not match"):
            workflow.unapply()
        self.lock["chromium"]["revision"] = "f" * 40
        self.write_lock()
        with self.assertRaisesRegex(openarc.WorkflowError, "not at the locked commit"):
            self.workflow().unapply()
        self.assertEqual((repo / "example.txt").read_text(), "first\n")

    def test_unapply_failure_checkpoints_remaining_prefix_and_can_retry(self):
        repo = self.repository()
        first = self.add_patch("01.patch", "base", "first")
        self.add_patch("02.patch", "first", "second")
        workflow = self.workflow()
        workflow.apply()
        real_run = workflow.run

        def fail_later_reverse(args, **kwargs):
            if "--reverse" in args and "--check" not in args and args[-1] == str(first):
                raise openarc.WorkflowError("simulated reverse application failure")
            return real_run(args, **kwargs)

        with patch.object(workflow, "run", side_effect=fail_later_reverse):
            with self.assertRaisesRegex(openarc.WorkflowError, "simulated reverse application failure"):
                workflow.unapply()
        self.assertEqual((repo / "example.txt").read_text(), "first\n")
        state = workflow.patch_state(workflow.patches())
        self.assertEqual([item["name"] for item in state["applied"]], ["01.patch"])
        self.workflow().unapply()
        self.assertEqual((repo / "example.txt").read_text(), "base\n")
        self.assertEqual(git(repo, "status", "--porcelain"), "")

    def test_unapply_preserves_evidence_and_cannot_reauthorize_promoted_baseline(self):
        workflow, _ = self.baseline_for_promotion()
        self.executable(workflow, "OpenArc", workflow.baseline_output)
        self.simulated_build(workflow, reuse_baseline=True)
        receipt = workflow.build_receipt_path(False).read_bytes()
        promotion = workflow.promotion_file.read_bytes()
        workflow.unapply()
        self.assertEqual(workflow.build_receipt_path(False).read_bytes(), receipt)
        self.assertEqual(workflow.promotion_file.read_bytes(), promotion)
        with self.assertRaisesRegex(openarc.WorkflowError, "not fully applied"):
            workflow.launch_command()
        with self.assertRaisesRegex(openarc.WorkflowError, "promoted"):
            workflow.launch_command(baseline=True)

    def test_patch_series_cannot_read_outside_patch_directory(self):
        for value in ("../outside.patch", "/tmp/outside.patch", "01.patch\n01.patch"):
            self.series.write_text(value + "\n")
            with self.assertRaises(openarc.WorkflowError):
                self.workflow().patches()

    def test_dependency_inventory_is_data_not_executable_python(self):
        repo = self.repository()
        marker = self.root / "executed"
        (repo.parent / ".gclient_entries").write_text(f"entries = __import__('pathlib').Path({str(marker)!r}).touch()\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "safely inspect"):
            self.workflow().dependency_repos()
        self.assertFalse(marker.exists())

    def test_launch_uses_private_profile_and_preserves_sandbox(self):
        self.repository()
        workflow = self.workflow()
        executable = self.executable(workflow)
        self.simulated_build(workflow)
        command = workflow.launch_command("https://example.com")
        self.assertEqual(command[0], str(executable))
        self.assertIn("--user-data-dir=" + str(self.root / ".build/profiles/development"), command)
        self.assertIn("--no-default-browser-check", command)
        self.assertNotIn("--no-sandbox", command)
        self.assertNotIn("--use-mock-keychain", command)
        self.assertEqual(command[-1], "https://example.com")
        for url in ("--no-sandbox", "file:///private/data", "javascript:alert(1)", "https://"):
            with self.assertRaisesRegex(openarc.WorkflowError, "HTTP|http"):
                workflow.launch_command(url)

    def test_launchservices_uses_exact_verified_bundle_and_preserves_browser_arguments(self):
        self.repository()
        workflow = self.workflow()
        for baseline in (False, True):
            with self.subTest(baseline=baseline):
                executable = self.executable(workflow, output=workflow.baseline_output if baseline else workflow.output)
                self.simulated_build(workflow, baseline=baseline)
                url = "https://example.com/?query=two%20words&literal=$(unchanged)"
                expected = workflow.launch_command(url, baseline)
                calls = []
                real_run = subprocess.run

                def launch_helper(args, **kwargs):
                    if args[0] != "/usr/bin/open":
                        return real_run(args, **kwargs)
                    calls.append((args, kwargs))
                    result = subprocess.CompletedProcess(args, 0)
                    result.pid = 12345  # A helper PID must never become a browser PID.
                    return result

                with patch.object(workflow, "require_mac"), \
                        patch.object(openarc.subprocess, "run", side_effect=launch_helper), \
                        patch("builtins.print") as output:
                    workflow.launch(url, baseline)
                report = json.loads(output.call_args.args[0])
                self.assertEqual(len(calls), 1)
                command, options = calls[0]
                self.assertEqual(command, ["/usr/bin/open", "-n", "-a", str(executable.parents[2]),
                                           "--args", *expected[1:]])
                self.assertEqual(report["command"], expected)
                self.assertEqual(report["launch_command"], command)
                self.assertIn("LaunchServices", report["launch_mechanism"])
                self.assertIsNone(report["pid"])
                self.assertIn("have not been verified", report["qualification"])
                self.assertEqual(options["timeout"], 30)
                self.assertFalse(options.get("shell", False))
                self.assertEqual(options["stdin"], subprocess.DEVNULL)
                self.assertEqual(options["cwd"], workflow.src)
                self.assertEqual(options["stdout"].name, report["log"])
                self.assertTrue(options["stdout"].closed)
                self.assertEqual("--use-mock-keychain" in command, baseline)
                self.assertNotIn("--no-sandbox", command)

    def test_launchservices_failures_never_report_a_browser_start_or_retry(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow)
        self.simulated_build(workflow)
        real_run = subprocess.run
        for failure, message in ((3, "LaunchServices failed"),
                                 (subprocess.TimeoutExpired("/usr/bin/open", 30), "may have started"),
                                 (OSError("helper unavailable"), "Cannot invoke macOS LaunchServices")):
            with self.subTest(failure=failure):
                calls = []

                def launch_helper(args, **kwargs):
                    if args[0] != "/usr/bin/open":
                        return real_run(args, **kwargs)
                    calls.append(args)
                    if isinstance(failure, Exception):
                        raise failure
                    return subprocess.CompletedProcess(args, failure)

                with patch.object(workflow, "require_mac"), \
                        patch.object(openarc.subprocess, "run", side_effect=launch_helper), \
                        patch("builtins.print") as output:
                    with self.assertRaisesRegex(openarc.WorkflowError, message):
                        workflow.launch(None)
                self.assertEqual(len(calls), 1)
                output.assert_not_called()
                self.assertTrue(workflow.build_receipt_path(False).is_file())

    def test_launchservices_is_not_invoked_for_unverified_binary_or_unsafe_url(self):
        self.repository()
        workflow = self.workflow()
        executable = self.executable(workflow)
        self.simulated_build(workflow)
        calls = []
        real_run = subprocess.run

        def no_launch(args, **kwargs):
            if args[0] == "/usr/bin/open":
                calls.append(args)
                raise AssertionError("LaunchServices must not run before validation")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(openarc.subprocess, "run", side_effect=no_launch):
            with self.assertRaisesRegex(openarc.WorkflowError, "http"):
                workflow.launch("--user-data-dir=/private/existing-profile")
            executable.write_text("changed binary\n")
            with self.assertRaisesRegex(openarc.WorkflowError, "executable or bundle identity changed"):
                workflow.launch(None)
        self.assertEqual(calls, [])

    def test_launchservices_rejects_redirected_log_before_invocation(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow)
        self.simulated_build(workflow)
        log = workflow.work / "logs/launch.log"
        log.parent.mkdir()
        protected = self.root / "unrelated-data"
        protected.write_text("preserve\n")
        log.symlink_to(protected)
        real_run = subprocess.run

        def no_launch(args, **kwargs):
            if args[0] == "/usr/bin/open":
                raise AssertionError("A redirected log must never reach LaunchServices")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(openarc.subprocess, "run", side_effect=no_launch):
            with self.assertRaisesRegex(openarc.WorkflowError, "symlink"):
                workflow.launch(None)
        self.assertEqual(protected.read_text(), "preserve\n")

    def test_profile_symlink_cannot_redirect_launch_to_existing_browser(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow)
        self.simulated_build(workflow)
        profiles = self.root / ".build/profiles"
        profiles.mkdir()
        elsewhere = self.root / "existing-browser"
        elsewhere.mkdir()
        (elsewhere / "important-data").write_text("preserve")
        (profiles / "development").symlink_to(elsewhere, target_is_directory=True)
        with self.assertRaisesRegex(openarc.WorkflowError, "symlink"):
            workflow.launch_command()
        self.assertEqual((elsewhere / "important-data").read_text(), "preserve")

    def test_brand_executable_discovery_rejects_ambiguity(self):
        self.repository()
        workflow = self.workflow()
        openarc_exe = self.executable(workflow, "OpenArc")
        self.assertEqual(workflow.executable(), openarc_exe)
        self.executable(workflow, "Chromium")
        with self.assertRaisesRegex(openarc.WorkflowError, "exactly one"):
            workflow.executable()

    def test_fetch_checks_existing_dirty_tree_before_network(self):
        repo = self.repository()
        (repo / "example.txt").write_text("work in progress\n")
        workflow = self.workflow()
        with patch.object(workflow, "ensure_space"), patch.object(workflow, "checkout_repo") as checkout:
            with self.assertRaisesRegex(openarc.WorkflowError, "Preserving modified"):
                workflow.fetch()
            checkout.assert_not_called()

    def test_build_passes_locked_arguments_without_shell_or_remote_execution(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow)
        calls = []

        def fake_run(args, **kwargs):
            calls.append((args, kwargs))
            return subprocess.CompletedProcess(args, 0, "", "")

        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "assert_pin"), patch.object(workflow, "ensure_bootstrap"), \
                patch.object(workflow, "run", side_effect=fake_run):
            workflow.build(3)
        gn = next(args for args, _ in calls if args[0] == str(workflow.depot / "gn"))
        ninja = next(args for args, _ in calls if args[0] == str(workflow.depot / "autoninja"))
        self.assertEqual(gn[:3], [str(workflow.depot / "gn"), "gen", "out/OpenArc"])
        self.assertIn("is_component_build = true", gn[3])
        self.assertEqual(ninja[-3:], ["-j", "3", "chrome"])
        self.assertEqual(workflow.env["DEPOT_TOOLS_UPDATE"], "0")
        self.assertTrue((workflow.work / "build-info.json").is_file())

    def test_build_rejects_unapplied_patches_and_unknown_edits(self):
        repo = self.repository()
        self.add_patch("01.patch", "base", "first")
        workflow = self.workflow()
        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "assert_pin"):
            with self.assertRaisesRegex(openarc.WorkflowError, "not fully applied"):
                workflow.build(2)
            workflow.apply()
            (repo / "example.txt").write_text("unknown work\n")
            with self.assertRaisesRegex(openarc.WorkflowError, "preserving local work"):
                workflow.build(2)

    def test_baseline_launch_has_distinct_output_and_profile(self):
        self.repository()
        workflow = self.workflow()
        executable = workflow.baseline_output / "Chromium.app/Contents/MacOS/Chromium"
        executable.parent.mkdir(parents=True)
        executable.write_text("#!/bin/sh\nexit 0\n")
        executable.chmod(0o755)
        self.simulated_build(workflow, baseline=True)
        command = workflow.launch_command(baseline=True)
        self.assertEqual(command[0], str(executable))
        self.assertIn("--user-data-dir=" + str(self.root / ".build/profiles/baseline"), command)
        self.assertIn("--use-mock-keychain", command)

    def test_launch_requires_successful_build_receipt(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow)
        with self.assertRaisesRegex(openarc.WorkflowError, "successful build receipt"):
            workflow.launch_command()

    def test_launch_rejects_binary_changed_after_successful_build(self):
        self.repository()
        workflow = self.workflow()
        executable = self.executable(workflow)
        self.simulated_build(workflow)
        executable.write_text("changed executable\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "executable or bundle identity changed"):
            workflow.launch_command()

    def test_launch_rejects_changed_lock_configuration(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow)
        self.simulated_build(workflow)
        self.lock["build"]["gn_args"]["is_debug"] = True
        self.write_lock()
        with self.assertRaisesRegex(openarc.WorkflowError, "does not match"):
            self.workflow().launch_command()

    def test_launch_rejects_newly_applied_patches_until_rebuilt(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow)
        self.simulated_build(workflow)
        self.add_patch("01.patch", "base", "first")
        workflow.apply()
        with self.assertRaisesRegex(openarc.WorkflowError, "does not match"):
            workflow.launch_command()

    def test_failed_rebuild_invalidates_previous_success(self):
        self.repository()
        workflow = self.workflow()
        executable = self.executable(workflow)
        self.simulated_build(workflow)
        real_run = workflow.run

        def fail_compilation(args, **kwargs):
            if args[0] == str(workflow.depot / "gn"):
                raise openarc.WorkflowError("simulated GN failure")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "ensure_bootstrap"), patch.object(workflow, "run", side_effect=fail_compilation):
            with self.assertRaisesRegex(openarc.WorkflowError, "simulated GN failure"):
                workflow.build(2)
        self.assertTrue(executable.exists())
        self.assertFalse(workflow.build_receipt_path(False).exists())
        with self.assertRaisesRegex(openarc.WorkflowError, "successful build receipt"):
            workflow.launch_command()

    def test_promotion_requires_successful_matching_baseline_before_mutation(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow, output=workflow.baseline_output)
        self.apply_identity(workflow)
        with self.assertRaisesRegex(openarc.WorkflowError, "successful build receipt"):
            self.simulated_build(workflow, reuse_baseline=True)
        self.assertFalse(workflow.promotion_file.exists())
        self.assertFalse(workflow.build_receipt_path(False).exists())

    def test_promotion_rejects_modified_baseline_binary_without_losing_receipt(self):
        workflow, baseline = self.baseline_for_promotion()
        baseline.write_text("changed baseline\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "Baseline executable or bundle identity changed"):
            self.simulated_build(workflow, reuse_baseline=True)
        self.assertFalse(workflow.promotion_file.exists())
        self.assertTrue(workflow.build_receipt_path(True).exists())

    def test_promotion_selects_openarc_and_preserves_historical_baseline(self):
        workflow, baseline = self.baseline_for_promotion()
        original = workflow.read_build_receipt(True)
        baseline_bytes = baseline.read_bytes()
        development = self.executable(workflow, "OpenArc", workflow.baseline_output)
        commands = self.simulated_build(workflow, reuse_baseline=True)
        self.assertEqual(commands[0][:3], [str(workflow.depot / "gn"), "gen", "out/Baseline"])
        self.assertEqual(commands[1][:3], [str(workflow.depot / "autoninja"), "-C", "out/Baseline"])
        self.assertEqual(baseline.read_bytes(), baseline_bytes)
        self.assertEqual(json.loads(workflow.promotion_file.read_text())["baseline_receipt"], original)
        self.assertFalse(workflow.build_receipt_path(True).exists())
        receipt = workflow.read_build_receipt(False)
        self.assertEqual(receipt["output"], "out/Baseline")
        self.assertFalse(receipt["inputs"]["baseline"])
        self.assertEqual(workflow.launch_command()[0], str(development))
        self.assertIn("--user-data-dir=" + str(workflow.work / "profiles/development"), workflow.launch_command())
        self.assertNotIn("--use-mock-keychain", workflow.launch_command())
        # Even accidentally restoring the old receipt cannot authorize a
        # baseline launch from output containing rebuilt shared components.
        openarc.atomic_json(workflow.build_receipt_path(True), original)
        with self.assertRaisesRegex(openarc.WorkflowError, "promoted"):
            workflow.launch_command(baseline=True)

    def test_failed_promotion_blocks_both_launches_and_retry_uses_same_output(self):
        workflow, _ = self.baseline_for_promotion()

        def failed_bootstrap():
            self.assertTrue(workflow.promotion_file.exists())
            self.assertFalse(workflow.build_receipt_path(True).exists())
            self.assertFalse(workflow.build_receipt_path(False).exists())
            raise openarc.WorkflowError("simulated bootstrap failure")

        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "ensure_bootstrap", side_effect=failed_bootstrap):
            with self.assertRaisesRegex(openarc.WorkflowError, "simulated bootstrap failure"):
                workflow.build(2, reuse_baseline=True)
        with self.assertRaisesRegex(openarc.WorkflowError, "promoted"):
            workflow.launch_command(baseline=True)
        with self.assertRaisesRegex(openarc.WorkflowError, "successful build receipt"):
            workflow.launch_command()
        # A new process must retain output ownership, including after failure.
        retry = self.workflow()
        development = self.executable(retry, "OpenArc", retry.baseline_output)
        self.simulated_build(retry)
        self.assertEqual(retry.launch_command()[0], str(development))
        self.assertFalse(retry.output.exists())

    def test_promotion_never_accepts_leftover_chromium_as_openarc_result(self):
        workflow, baseline = self.baseline_for_promotion()
        with self.assertRaisesRegex(openarc.WorkflowError, "executable OpenArc application"):
            self.simulated_build(workflow, reuse_baseline=True)
        self.assertTrue(baseline.exists())
        self.assertFalse(workflow.build_receipt_path(False).exists())
        self.assertFalse(workflow.build_receipt_path(True).exists())

    def test_promotion_rejects_changed_dependencies_before_reusing_output(self):
        workflow, _ = self.baseline_for_promotion()
        self.add_dependency(workflow.src)
        with self.assertRaisesRegex(openarc.WorkflowError, "current pristine lock and dependencies"):
            self.simulated_build(workflow, reuse_baseline=True)
        self.assertFalse(workflow.promotion_file.exists())
        self.assertTrue(workflow.build_receipt_path(True).exists())

    def test_launch_rejects_receipt_with_another_output_directory(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow)
        self.simulated_build(workflow)
        receipt = workflow.read_build_receipt(False)
        receipt["output"] = "../../another-profile"
        openarc.atomic_json(workflow.build_receipt_path(False), receipt)
        with self.assertRaisesRegex(openarc.WorkflowError, "receipt output does not match"):
            workflow.launch_command()

    def test_build_rejects_missing_dependency_inventory(self):
        repo = self.repository()
        (repo.parent / ".gclient_entries").unlink()
        workflow = self.workflow()
        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"):
            with self.assertRaisesRegex(openarc.WorkflowError, "dependency inventory"):
                workflow.build(2)

    def test_build_rejects_dirty_dependency_and_preserves_it(self):
        repo = self.repository()
        dependency = self.add_dependency(repo)
        (dependency / "example.txt").write_text("valuable dependency work\n")
        workflow = self.workflow()
        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"):
            with self.assertRaisesRegex(openarc.WorkflowError, "Preserving modified checkout"):
                workflow.build(2)
        self.assertEqual((dependency / "example.txt").read_text(), "valuable dependency work\n")

    def test_launch_rejects_changed_dependency_revision(self):
        repo = self.repository()
        dependency = self.add_dependency(repo)
        workflow = self.workflow()
        self.executable(workflow)
        self.simulated_build(workflow)
        git(dependency, "commit", "--allow-empty", "-m", "new dependency revision")
        with self.assertRaisesRegex(openarc.WorkflowError, "does not match"):
            workflow.launch_command()

    def test_build_rejects_missing_dependency_directory(self):
        repo = self.repository()
        (repo.parent / ".gclient_entries").write_text("entries = " + repr({
            "src": openarc.CHROMIUM_URL, "src/third_party/missing": openarc.CHROMIUM_URL}) + "\n")
        workflow = self.workflow()
        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"):
            with self.assertRaisesRegex(openarc.WorkflowError, "Missing dependency directory"):
                workflow.build(2)

    def test_gclient_packaged_dependency_names_use_directory_before_colon(self):
        repo = self.repository()
        # Formats emitted by the locked depot_tools GcsDependency and
        # CipdDependency, observed in a successful actual Chromium sync.
        gcs_name = "src/base/tracing/test/data:test_data/chrome_5672_histograms.pftrace.gz-a09bd44078ac71bcfbc901b0544750e8344d0d0f6f96e220f700a5a53fa932ee"
        cipd_name = "src/buildtools/mac:gn/gn/mac-${arch}"
        entries = {
            "src": openarc.CHROMIUM_URL,
            gcs_name: "gs://perfetto/test_data/chrome_5672_histograms.pftrace.gz-a09bd44078ac71bcfbc901b0544750e8344d0d0f6f96e220f700a5a53fa932ee",
            cipd_name: "https://chrome-infra-packages.appspot.com/gn/gn/mac-${arch}@git_revision:150a9d6ba0aa7f407aa4feeabc5f03ce9aa7e04b",
        }
        for directory in ("base/tracing/test/data", "buildtools/mac"):
            (repo / directory).mkdir(parents=True)
        (repo.parent / ".gclient_entries").write_text("entries = " + repr(entries) + "\n")
        workflow = self.workflow()
        evidence = workflow.dependency_evidence()
        self.assertEqual(evidence["entries"], entries)
        self.assertEqual(evidence["git_revisions"], {})
        self.assertEqual(workflow.dependency_repos(), [])
        self.assertFalse((repo.parent / gcs_name).exists())
        self.assertFalse((repo.parent / cipd_name).exists())

    def test_packaged_dependency_cannot_escape_managed_checkout(self):
        repo = self.repository()
        (repo.parent / ".gclient_entries").write_text("entries = " + repr({
            "../elsewhere:package": "gs://bucket/package"}) + "\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "must not be absolute or contain"):
            self.workflow().dependency_evidence()

    def test_bootstrap_uses_absolute_pinned_script_and_verifies_python(self):
        self.repository()
        workflow = self.workflow()
        calls = []
        real_run = workflow.run

        def runner(args, **kwargs):
            calls.append((args, kwargs))
            if args[0] == str(workflow.depot / "ensure_bootstrap"):
                return subprocess.CompletedProcess(args, 0, "", "")
            if args[0] == str(workflow.depot / "python-bin/python3"):
                return subprocess.CompletedProcess(args, 0, "Python 3.11.8\n", "")
            return real_run(args, **kwargs)

        with patch.object(workflow, "run", side_effect=runner):
            workflow.bootstrap()
        self.assertIn(([str(workflow.depot / "ensure_bootstrap")], {"cwd": workflow.depot}), calls)
        self.assertTrue(Path(str(workflow.depot / "ensure_bootstrap")).is_absolute())
        self.assertEqual(workflow.env["DEPOT_TOOLS_UPDATE"], "0")
        self.assertEqual(workflow.env["DEPOT_TOOLS_DIR"], str(workflow.depot))
        self.assertEqual(workflow.env["DEPOT_TOOLS_BOOTSTRAP_PYTHON3"], "1")

    def test_successful_upstream_bootstrap_exit_without_working_python_is_failure(self):
        self.repository()
        workflow = self.workflow()

        def runner(args, **kwargs):
            if args[0] == str(workflow.depot / "python-bin/python3"):
                raise openarc.WorkflowError("python3_bin_reldir.txt not found")
            return subprocess.CompletedProcess(args, 0, "", "")

        with patch.object(workflow, "run", side_effect=runner):
            with self.assertRaisesRegex(openarc.WorkflowError, "python3_bin_reldir"):
                workflow.ensure_bootstrap()

    def test_fetch_bootstraps_before_fetching_chromium(self):
        workflow = self.workflow()
        actions = []
        with patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "checkout_repo", side_effect=lambda path, component: actions.append(component)), \
                patch.object(workflow, "ensure_bootstrap", side_effect=lambda: actions.append("bootstrap")):
            workflow.fetch()
        self.assertEqual(actions, ["depot_tools", "bootstrap", "chromium"])


if __name__ == "__main__":
    unittest.main()
