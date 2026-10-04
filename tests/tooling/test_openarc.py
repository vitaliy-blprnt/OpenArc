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

    def executable(self, workflow, name="Chromium"):
        executable = workflow.output / f"{name}.app" / "Contents" / "MacOS" / name
        executable.parent.mkdir(parents=True)
        executable.write_text("#!/bin/sh\nexit 0\n")
        executable.chmod(0o755)
        return executable

    def simulated_build(self, workflow, baseline=False):
        """Run real validation/receipts with only GN/compiler invocation simulated."""
        real_run = workflow.run

        def runner(args, **kwargs):
            if args[0] in (str(workflow.depot / "gn"), str(workflow.depot / "autoninja")):
                return subprocess.CompletedProcess(args, 0, "", "")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "ensure_bootstrap"), patch.object(workflow, "run", side_effect=runner):
            workflow.build(2, baseline)

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
        for path in ("/tmp/output", "out/../../outside", "../outside", "out", "out/./OpenArc"):
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
