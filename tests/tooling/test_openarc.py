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
              "gn_args": {"target_cpu": "arm64", "is_debug": False, "is_component_build": True, "is_official_build": False,
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

    def source_overlay(self, files=None):
        files = {"workspace/core.h": b"// original\n"} if files is None else files
        directory = self.root / "src/openarc"
        for name, content in files.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        manifest = directory / "source-overlay.json"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps({"schema_version": 1, "files": list(files)}))
        return manifest

    def interrupt_overlay(self, workflow, *, after_git=False, removing=False):
        """Simulate process loss at either side of the indexed source transition."""
        real_write = openarc.atomic_json

        def writer(path, value):
            if path == workflow.state_file:
                if after_git and "pending_overlay" not in value:
                    raise RuntimeError("interrupted after Git")
                real_write(path, value)
                if not after_git and "pending_overlay" in value:
                    raise RuntimeError("interrupted before Git")
            else:
                real_write(path, value)

        with patch.object(openarc, "atomic_json", side_effect=writer), self.assertRaisesRegex(RuntimeError, "interrupted"):
            workflow.unapply() if removing else workflow.apply()

    def test_overlay_manifest_alone_keeps_existing_inputs_and_launch_valid(self):
        workflow, executable = self.packaging_candidate()
        self.simulated_build(workflow, packaging=True)
        before = workflow.build_inputs(False, True)
        manifest = self.source_overlay()
        manifest.write_text("an evolving manifest is not installed yet")
        self.assertEqual(workflow.build_inputs(False, True), before)
        self.assertNotIn("source_overlay", before)
        self.assertEqual(workflow.verified_executable(False, True), executable)
        self.assertFalse((workflow.src / "openarc").exists())

    def test_overlay_installs_binary_content_modes_and_is_idempotent(self):
        repo = self.repository()
        self.source_overlay({"workspace/core.h": b"// original\n", "workspace/data.bin": b"\x00\xff\x01"})
        (self.root / "src/openarc/workspace/core.h").chmod(0o755)
        workflow = self.workflow()
        workflow.apply()
        before = workflow.state_file.read_bytes()
        workflow.apply()
        self.assertEqual(workflow.state_file.read_bytes(), before)
        self.assertEqual((repo / "openarc/workspace/data.bin").read_bytes(), b"\x00\xff\x01")
        self.assertTrue(git(repo, "ls-files", "--stage", "openarc/workspace/core.h").startswith("100755"))
        self.assertIn("source_overlay", workflow.build_inputs(False))
        workflow.unapply()
        self.assertEqual(git(repo, "status", "--porcelain"), "")
        self.assertNotIn("source_overlay", json.loads(workflow.state_file.read_text()))
        self.assertEqual((self.root / "src/openarc/workspace/core.h").read_bytes(), b"// original\n")

    def test_overlay_reconciles_add_update_remove_and_mode_change(self):
        repo = self.repository()
        self.source_overlay({"workspace/core.h": b"old\n", "workspace/removed.h": b"remove\n"})
        workflow = self.workflow()
        workflow.apply()
        self.source_overlay({"workspace/core.h": b"new\n", "workspace/added.h": b"add\n"})
        (self.root / "src/openarc/workspace/core.h").chmod(0o755)
        with self.assertRaisesRegex(openarc.WorkflowError, "originals or manifest changed"):
            workflow.build_inputs(False)
        workflow.apply()
        self.assertEqual((repo / "openarc/workspace/core.h").read_bytes(), b"new\n")
        self.assertFalse((repo / "openarc/workspace/removed.h").exists())
        self.assertEqual((repo / "openarc/workspace/added.h").read_bytes(), b"add\n")
        workflow.build_inputs(False)

    def test_overlay_source_drift_blocks_build_launch_but_unapply_uses_recorded_copies(self):
        repo = self.repository()
        manifest = self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        self.executable(workflow)
        self.simulated_build(workflow)
        receipt = workflow.build_receipt_path(False).read_bytes()
        (self.root / "src/openarc/workspace/core.h").unlink()
        manifest.write_text("invalid now")
        with self.assertRaises(openarc.WorkflowError):
            self.simulated_build(workflow)
        with self.assertRaises(openarc.WorkflowError):
            workflow.verified_executable(False)
        self.assertEqual(workflow.build_receipt_path(False).read_bytes(), receipt)
        workflow.unapply()
        self.assertEqual(git(repo, "status", "--porcelain"), "")
        self.assertEqual(workflow.build_receipt_path(False).read_bytes(), receipt)

    def test_overlay_ignored_collision_is_never_adopted_even_if_identical(self):
        repo = self.repository()
        self.source_overlay()
        (repo / ".git/info/exclude").write_text("openarc/\n")
        destination = repo / "openarc/workspace/core.h"
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"// original\n")
        workflow = self.workflow()
        with self.assertRaisesRegex(openarc.WorkflowError, "unowned"):
            workflow.apply()
        self.assertEqual(destination.read_bytes(), b"// original\n")
        self.assertFalse(workflow.state_file.exists())

    def test_overlay_rejects_symlinks_and_manifest_path_escapes(self):
        repo = self.repository()
        manifest = self.source_overlay()
        original = manifest.parent / "workspace/core.h"
        original.unlink()
        original.symlink_to(repo / "example.txt")
        with self.assertRaisesRegex(openarc.WorkflowError, "symlink"):
            self.workflow().apply()
        original.unlink()
        original.write_text("safe\n")
        destination = repo / "openarc"
        destination.symlink_to(manifest.parent, target_is_directory=True)
        with self.assertRaisesRegex(openarc.WorkflowError, "symlink|modified"):
            self.workflow().apply()
        destination.unlink()
        for name in ("../outside.h", "/tmp/outside.h", "workspace/.git/config", "workspace//core.h"):
            manifest.write_text(json.dumps({"schema_version": 1, "files": [name]}))
            with self.subTest(name=name), self.assertRaises(openarc.WorkflowError):
                self.workflow().apply()
        self.assertEqual((repo / "example.txt").read_text(), "base\n")

    def test_overlay_user_edits_in_worktree_or_index_block_apply_and_unapply(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        destination = repo / "openarc/workspace/core.h"
        for staged in (False, True):
            destination.write_text("user work\n")
            if staged:
                git(repo, "add", "openarc/workspace/core.h")
            for operation in (workflow.apply, workflow.unapply):
                with self.subTest(staged=staged), self.assertRaisesRegex(openarc.WorkflowError, "preserving local work"):
                    operation()
            self.assertEqual(destination.read_text(), "user work\n")
            destination.write_text("// original\n")
            git(repo, "add", "openarc/workspace/core.h")
        workflow.unapply()

    def test_overlay_ownership_checks_hidden_index_flags_and_ignored_mode_drift(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        for flag in ("assume-unchanged", "skip-worktree"):
            git(repo, "update-index", "--" + flag, "openarc/workspace/core.h")
            with self.subTest(flag=flag), self.assertRaisesRegex(openarc.WorkflowError, "ownership changed"):
                workflow.unapply()
            git(repo, "update-index", "--no-" + flag, "openarc/workspace/core.h")
        git(repo, "config", "core.filemode", "false")
        (repo / "openarc/workspace/core.h").chmod(0o755)
        with self.assertRaisesRegex(openarc.WorkflowError, "ownership changed"):
            workflow.apply()

    def test_overlay_integration_patch_namespace_collision_is_rejected_before_staging(self):
        repo = self.repository()
        self.source_overlay()
        path = self.series.parent / "bad.patch"
        path.write_text("diff --git a/openarc/other.h b/openarc/other.h\nnew file mode 100644\n"
                        "--- /dev/null\n+++ b/openarc/other.h\n@@ -0,0 +1 @@\n+owned by patch\n")
        self.series.write_text("bad.patch\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "Integration patches"):
            self.workflow().apply()
        self.assertEqual(git(repo, "status", "--porcelain"), "")

    def test_overlay_pending_before_state_blocks_build_and_explicit_apply_resumes(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        self.interrupt_overlay(workflow)
        self.assertFalse((repo / "openarc/workspace/core.h").exists())
        with self.assertRaisesRegex(openarc.WorkflowError, "Pending source overlay"):
            workflow.build_inputs(False)
        with self.assertRaisesRegex(openarc.WorkflowError, "source overlay"):
            workflow.build_inputs(True)
        workflow.apply()
        self.assertNotIn("pending_overlay", json.loads(workflow.state_file.read_text()))
        workflow.build_inputs(False)

    def test_overlay_pending_after_state_is_checkpointed_without_duplicate_application(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        self.interrupt_overlay(workflow, after_git=True)
        staged = git(repo, "diff", "--cached")
        workflow.apply()
        self.assertEqual(git(repo, "diff", "--cached"), staged)
        self.assertNotIn("pending_overlay", json.loads(workflow.state_file.read_text()))

    def test_overlay_pending_update_rolls_back_then_unapplies_without_originals(self):
        repo = self.repository()
        manifest = self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        self.source_overlay({"workspace/core.h": b"updated\n"})
        self.interrupt_overlay(workflow, after_git=True)
        manifest.unlink()
        (self.root / "src/openarc/workspace/core.h").unlink()
        workflow.unapply()
        self.assertEqual(git(repo, "status", "--porcelain"), "")
        self.assertNotIn("pending_overlay", json.loads(workflow.state_file.read_text()))

    def test_overlay_pending_before_state_can_be_cancelled_by_unapply(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        self.interrupt_overlay(workflow)
        workflow.unapply()
        self.assertEqual(git(repo, "status", "--porcelain"), "")
        self.assertNotIn("source_overlay", json.loads(workflow.state_file.read_text()))

    def test_overlay_pending_unknown_changes_are_preserved(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        self.interrupt_overlay(workflow, after_git=True)
        (repo / "openarc/workspace/core.h").write_text("new user work\n")
        before = workflow.state_file.read_bytes()
        for operation in (workflow.apply, workflow.unapply):
            with self.assertRaisesRegex(openarc.WorkflowError, "neither its recorded"):
                operation()
        self.assertEqual(workflow.state_file.read_bytes(), before)
        self.assertEqual((repo / "openarc/workspace/core.h").read_text(), "new user work\n")

    def test_overlay_promotes_historical_baseline_without_changing_seed_shape(self):
        workflow, _ = self.baseline_for_promotion()
        baseline = workflow.read_build_receipt(True)
        self.source_overlay()
        workflow.apply()
        self.executable(workflow, "OpenArc", workflow.baseline_output)
        self.simulated_build(workflow, reuse_baseline=True)
        receipt = workflow.read_build_receipt(False)
        self.assertIn("source_overlay", receipt["inputs"])
        promotion = json.loads(workflow.promotion_file.read_text())
        self.assertEqual(promotion["baseline_receipt"], baseline)
        self.assertNotIn("source_overlay", baseline["inputs"])
        workflow.verified_executable(False)

    def test_overlay_source_change_during_compilation_prevents_receipt(self):
        self.repository()
        self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        self.executable(workflow)
        real_run = workflow.run

        def runner(args, **kwargs):
            if args[0] == str(workflow.depot / "gn"):
                return subprocess.CompletedProcess(args, 0, "", "")
            if args[0] == str(workflow.depot / "autoninja"):
                (self.root / "src/openarc/workspace/core.h").write_text("changed during build\n")
                return subprocess.CompletedProcess(args, 0, "", "")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "ensure_bootstrap"), patch.object(workflow, "run", side_effect=runner):
            with self.assertRaisesRegex(openarc.WorkflowError, "originals or manifest changed"):
                workflow.build(2)
        self.assertFalse(workflow.build_receipt_path(False).exists())

    def test_overlay_missing_manifest_does_not_silently_remove_installed_copies(self):
        repo = self.repository()
        manifest = self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        manifest.unlink()
        before = workflow.state_file.read_bytes()
        with self.assertRaisesRegex(openarc.WorkflowError, "manifest is missing"):
            workflow.apply()
        self.assertEqual(workflow.state_file.read_bytes(), before)
        self.assertTrue((repo / "openarc/workspace/core.h").exists())
        workflow.unapply()
        self.assertFalse((repo / "openarc/workspace/core.h").exists())

    def test_overlay_case_collisions_and_duplicate_paths_are_rejected(self):
        repo = self.repository()
        manifest = self.source_overlay()
        for names in (["workspace/core.h", "workspace/core.h"], ["workspace/core.h", "workspace/CORE.h"]):
            manifest.write_text(json.dumps({"schema_version": 1, "files": names}))
            with self.subTest(names=names), self.assertRaisesRegex(openarc.WorkflowError, "unique"):
                self.workflow().apply()
        self.assertEqual(git(repo, "status", "--porcelain"), "")

    def test_overlay_upstream_tracked_destination_cannot_be_adopted(self):
        repo = self.repository()
        self.source_overlay()
        destination = repo / "openarc/workspace/core.h"
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b"// original\n")
        git(repo, "add", "openarc")
        git(repo, "commit", "-m", "upstream owns destination")
        self.lock["chromium"]["revision"] = git(repo, "rev-parse", "HEAD")
        self.write_lock()
        with self.assertRaisesRegex(openarc.WorkflowError, "unowned"):
            self.workflow().apply()
        self.assertEqual(git(repo, "status", "--porcelain"), "")

    def test_overlay_unapply_preserves_unowned_ignored_neighbors(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        (repo / ".git/info/exclude").write_text("openarc/workspace/personal.h\n")
        neighbor = repo / "openarc/workspace/personal.h"
        neighbor.write_text("unowned user file\n")
        workflow.unapply()
        self.assertEqual(neighbor.read_text(), "unowned user file\n")
        self.assertEqual(git(repo, "status", "--porcelain"), "")

    def test_overlay_rejects_patch_renaming_owned_source_outside_namespace(self):
        self.repository()
        self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        path = self.series.parent / "rename.patch"
        path.write_text("diff --git a/openarc/workspace/core.h b/elsewhere.h\n"
                        "similarity index 100%\nrename from openarc/workspace/core.h\nrename to elsewhere.h\n")
        self.series.write_text("rename.patch\n")
        before = workflow.state_file.read_bytes()
        with self.assertRaisesRegex(openarc.WorkflowError, "Integration patches"):
            workflow.apply()
        self.assertEqual(workflow.state_file.read_bytes(), before)

    def test_overlay_manifest_only_change_has_recoverable_metadata_transition(self):
        self.repository()
        manifest = self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        old_digest = workflow.tree_digest()
        manifest.write_text(manifest.read_text() + "\n")
        self.interrupt_overlay(workflow)
        workflow.apply()
        self.assertEqual(workflow.tree_digest(), old_digest)
        self.assertEqual(workflow.build_inputs(False)["source_overlay"]["manifest_sha256"],
                         openarc.file_digest(manifest))

    def test_overlay_checkout_check_detects_source_drift_without_reconciling(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        (self.root / "src/openarc/workspace/core.h").write_text("changed original\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "originals or manifest changed"):
            workflow.check(checkout=True)
        self.assertEqual((repo / "openarc/workspace/core.h").read_text(), "// original\n")

    def test_overlay_interrupted_removal_can_retry_without_originals(self):
        repo = self.repository()
        manifest = self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        manifest.unlink()
        (self.root / "src/openarc/workspace/core.h").unlink()
        self.interrupt_overlay(workflow, after_git=True, removing=True)
        self.assertFalse((repo / "openarc/workspace/core.h").exists())
        self.assertIn("pending_overlay", json.loads(workflow.state_file.read_text()))
        workflow.unapply()
        self.assertEqual(git(repo, "status", "--porcelain"), "")
        self.assertNotIn("source_overlay", json.loads(workflow.state_file.read_text()))

    def test_overlay_corrupt_pending_record_never_mutates_checkout(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        self.interrupt_overlay(workflow)
        valid = json.loads(workflow.state_file.read_text())
        for corrupt in ([], {"before": {}, "after": []}, dict(valid["pending_overlay"], patch="corrupted")):
            recorded = dict(valid, pending_overlay=corrupt)
            workflow.state_file.write_text(json.dumps(recorded))
            for operation in (workflow.apply, workflow.unapply):
                with self.subTest(corrupt=corrupt), self.assertRaisesRegex(openarc.WorkflowError, "Invalid pending"):
                    operation()
            self.assertEqual(git(repo, "status", "--porcelain"), "")
            self.assertEqual(json.loads(workflow.state_file.read_text()), recorded)

    def test_overlay_concurrent_index_edit_is_not_adopted_during_preparation(self):
        repo = self.repository()
        self.source_overlay()
        workflow = self.workflow()
        real_run = workflow.run

        def runner(args, **kwargs):
            result = real_run(args, **kwargs)
            if args[-3:] == ["rev-parse", "--git-path", "index"]:
                (repo / "example.txt").write_text("concurrent user work\n")
                git(repo, "add", "example.txt")
            return result

        with patch.object(workflow, "run", side_effect=runner), self.assertRaisesRegex(openarc.WorkflowError, "while preparing"):
            workflow.apply()
        self.assertFalse(workflow.state_file.exists())
        self.assertFalse((repo / "openarc/workspace/core.h").exists())
        self.assertEqual((repo / "example.txt").read_text(), "concurrent user work\n")

    def executable(self, workflow, name="Chromium", output=None):
        executable = (output or workflow.output) / f"{name}.app" / "Contents" / "MacOS" / name
        executable.parent.mkdir(parents=True, exist_ok=True)
        executable.write_text("#!/bin/sh\nexit 0\n")
        executable.chmod(0o755)
        return executable

    def simulated_build(self, workflow, baseline=False, reuse_baseline=False, packaging=False):
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
            workflow.build(2, baseline, reuse_baseline, packaging)
        return compiler_calls

    def packaging_candidate(self):
        self.repository()
        workflow = self.workflow()
        self.apply_identity(workflow)
        executable = self.executable(workflow, "OpenArc", workflow.packaging_output)
        (executable.parent.parent / "Info.plist").write_text("OpenArc packaging bundle identity\n")
        return workflow, executable

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

    def test_workspaces_launch_uses_verified_development_build_and_isolated_profile(self):
        self.repository()
        workflow = self.workflow()
        self.apply_identity(workflow)
        executable = self.executable(workflow, "OpenArc")
        self.simulated_build(workflow)
        receipt = workflow.build_receipt_path(False).read_bytes()
        url = "https://example.com/?literal=$(unchanged)&space=two%20words"
        calls = []
        real_run = subprocess.run

        def launch_helper(args, **kwargs):
            if args[0] != "/usr/bin/open":
                return real_run(args, **kwargs)
            calls.append(args)
            return subprocess.CompletedProcess(args, 0)

        with patch.object(workflow, "require_mac"), \
                patch.object(openarc.subprocess, "run", side_effect=launch_helper), \
                patch("builtins.print") as output:
            workflow.launch(url, workspaces=True)
        report = json.loads(output.call_args.args[0])
        expected = [str(executable), "--user-data-dir=" + str(workflow.work / "profiles/workspaces"),
                    "--no-first-run", "--no-default-browser-check",
                    "--enable-features=OpenArcWorkspaces", url]
        self.assertEqual(report["command"], expected)
        self.assertEqual(calls, [["/usr/bin/open", "-n", "-a", str(executable.parents[2]), "--args", *expected[1:]]])
        self.assertEqual(report["log"], str(workflow.work / "logs/workspaces-launch.log"))
        self.assertIsNone(report["pid"])
        self.assertEqual(workflow.build_receipt_path(False).read_bytes(), receipt)
        for name in ("development", "baseline", "packaging"):
            self.assertFalse((workflow.work / "profiles" / name).exists())
        self.assertEqual(workflow.launch_command(), [str(executable),
                         "--user-data-dir=" + str(workflow.work / "profiles/development"),
                         "--no-first-run", "--no-default-browser-check"])

    def test_workspaces_cli_is_explicit_exclusive_and_does_not_accept_arbitrary_flags(self):
        workflow = self.workflow()
        with patch.object(workflow, "verified_executable") as verify:
            for mode in ("baseline", "packaging"):
                with self.subTest(mode=mode), self.assertRaisesRegex(openarc.WorkflowError, "mutually exclusive"):
                    workflow.launch_command(workspaces=True, **{mode: True})
            verify.assert_not_called()
        self.assertFalse(workflow.work.exists())
        with patch.object(openarc, "Workflow") as constructor, patch("sys.stderr"):
            for arguments in (["launch", "--baseline", "--workspaces"],
                              ["launch", "--packaging", "--workspaces"],
                              ["build", "--workspaces"],
                              ["launch", "--workspaces", "--disable-features=TabStripUnification"],
                              ["launch", "--workspaces", "--user-data-dir=/existing-profile"]):
                with self.subTest(arguments=arguments), self.assertRaises(SystemExit) as error:
                    openarc.main(arguments)
                self.assertEqual(error.exception.code, 2)
            constructor.assert_not_called()
            self.assertEqual(openarc.main(["launch", "--workspaces", "https://example.com/"]), 0)
            constructor.return_value.launch.assert_called_once_with("https://example.com/", False, False, True)

    def test_workspaces_launch_requires_normal_receipt_and_unchanged_inputs_and_binary(self):
        self.repository()
        workflow = self.workflow()
        executable = self.executable(workflow, "OpenArc")
        self.simulated_build(workflow)
        normal_receipt = workflow.build_receipt_path(False)
        receipt = normal_receipt.read_bytes()
        normal_receipt.rename(workflow.build_receipt_path(False, packaging=True))
        with self.assertRaisesRegex(openarc.WorkflowError, "successful build receipt"):
            workflow.launch_command(workspaces=True)
        normal_receipt.write_bytes(receipt)
        for update, message in ((lambda value: value["inputs"].update(baseline=True), "does not match"),
                                (lambda value: value["inputs"].update(packaging=True), "does not match"),
                                (lambda value: value.update(output="out/Packaging"), "output does not match")):
            changed = json.loads(receipt)
            update(changed)
            openarc.atomic_json(normal_receipt, changed)
            with self.subTest(message=message), self.assertRaisesRegex(openarc.WorkflowError, message):
                workflow.launch_command(workspaces=True)
        normal_receipt.write_bytes(receipt)
        executable.write_text("changed executable\n")
        with self.assertRaisesRegex(openarc.WorkflowError, "executable or bundle identity changed"):
            workflow.launch_command(workspaces=True)
        self.assertFalse((workflow.work / "profiles/workspaces").exists())

    def test_workspaces_launch_rejects_overlay_drift_before_launchservices(self):
        self.repository()
        self.source_overlay()
        workflow = self.workflow()
        workflow.apply()
        self.executable(workflow, "OpenArc")
        self.simulated_build(workflow)
        (self.root / "src/openarc/workspace/core.h").write_text("// changed original\n")
        real_run = subprocess.run

        def no_launch(args, **kwargs):
            if args[0] == "/usr/bin/open":
                raise AssertionError("A stale overlay must never reach LaunchServices")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(openarc.subprocess, "run", side_effect=no_launch):
            with self.assertRaisesRegex(openarc.WorkflowError, "overlay.*changed|differ|drift"):
                workflow.launch(None, workspaces=True)
        self.assertFalse((workflow.work / "profiles/workspaces").exists())

    def test_workspaces_launch_preserves_promoted_development_output_and_receipt(self):
        workflow, _ = self.baseline_for_promotion()
        self.apply_identity(workflow)
        executable = self.executable(workflow, "OpenArc", workflow.baseline_output)
        self.simulated_build(workflow, reuse_baseline=True)
        receipt = workflow.build_receipt_path(False).read_bytes()
        promotion = workflow.promotion_file.read_bytes()
        command = workflow.launch_command(workspaces=True)
        self.assertEqual(command[0], str(executable))
        self.assertIn("--user-data-dir=" + str(workflow.work / "profiles/workspaces"), command)
        self.assertIn("--enable-features=OpenArcWorkspaces", command)
        self.assertEqual(workflow.build_receipt_path(False).read_bytes(), receipt)
        self.assertEqual(workflow.promotion_file.read_bytes(), promotion)
        with self.assertRaisesRegex(openarc.WorkflowError, "promoted"):
            workflow.launch_command(baseline=True)

    def test_workspaces_profile_and_log_cannot_redirect_to_existing_data(self):
        self.repository()
        workflow = self.workflow()
        self.executable(workflow, "OpenArc")
        self.simulated_build(workflow)
        protected = self.root / "existing-browser"
        protected.mkdir()
        marker = protected / "important-data"
        marker.write_text("preserve")
        profile = workflow.work / "profiles/workspaces"
        profile.parent.mkdir()
        profile.symlink_to(protected, target_is_directory=True)
        with self.assertRaisesRegex(openarc.WorkflowError, "symlink"):
            workflow.launch_command(workspaces=True)
        profile.unlink()
        log = workflow.work / "logs/workspaces-launch.log"
        log.parent.mkdir()
        log.symlink_to(marker)
        real_run = subprocess.run

        def no_launch(args, **kwargs):
            if args[0] == "/usr/bin/open":
                raise AssertionError("A redirected workspaces log must never reach LaunchServices")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(openarc.subprocess, "run", side_effect=no_launch):
            with self.assertRaisesRegex(openarc.WorkflowError, "symlink"):
                workflow.launch(None, workspaces=True)
        self.assertEqual(marker.read_text(), "preserve")
        self.assertEqual(list(protected.iterdir()), [marker])

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

    def test_packaging_build_records_effective_args_without_changing_other_modes(self):
        self.repository()
        workflow = self.workflow()
        baseline = self.executable(workflow, output=workflow.baseline_output)
        development = self.executable(workflow)
        self.simulated_build(workflow, baseline=True)
        self.simulated_build(workflow)
        preserved = {path: path.read_bytes() for path in (
            workflow.build_receipt_path(True), workflow.build_receipt_path(False),
            baseline, development, self.root / "upstream.lock")}
        self.apply_identity(workflow)
        self.executable(workflow, "OpenArc", workflow.packaging_output)
        patch_state = workflow.state_file.read_bytes()

        commands = self.simulated_build(workflow, packaging=True)

        expected_args = dict(self.lock["build"]["gn_args"], is_component_build=False)
        expected_gn = "\n".join(f"{name} = {openarc.gn_value(value)}" for name, value in sorted(expected_args.items()))
        self.assertEqual(commands[0], [str(workflow.depot / "gn"), "gen", "out/Packaging", "--args=" + expected_gn])
        self.assertEqual(commands[1], [str(workflow.depot / "autoninja"), "-C", "out/Packaging", "-j", "2",
                                       "chrome", "chrome/installer/mac"])
        receipt = workflow.read_build_receipt(False, packaging=True)
        self.assertEqual(receipt["output"], "out/Packaging")
        self.assertEqual(receipt["binary"]["path"], "out/Packaging/OpenArc.app/Contents/MacOS/OpenArc")
        self.assertTrue(receipt["inputs"]["packaging"])
        self.assertFalse(receipt["inputs"]["baseline"])
        self.assertEqual(receipt["inputs"]["effective_gn_args"], expected_args)
        self.assertEqual(receipt["inputs"]["upstream"], self.lock)
        self.assertEqual(receipt["inputs"]["patches"], json.loads(patch_state)["applied"])
        self.assertEqual(receipt["inputs"]["dependencies"], workflow.dependency_evidence())
        for path, contents in preserved.items():
            self.assertEqual(path.read_bytes(), contents)
        self.assertEqual(workflow.state_file.read_bytes(), patch_state)
        self.assertFalse(workflow.promotion_file.exists())

    def test_packaging_ignores_promoted_output_and_preserves_its_receipt(self):
        workflow, _ = self.baseline_for_promotion()
        development = self.executable(workflow, "OpenArc", workflow.baseline_output)
        self.simulated_build(workflow, reuse_baseline=True)
        preserved = {path: path.read_bytes() for path in (workflow.promotion_file, workflow.build_receipt_path(False), development)}
        packaging = self.executable(workflow, "OpenArc", workflow.packaging_output)
        self.simulated_build(workflow, packaging=True)
        self.assertEqual(workflow.launch_command()[0], str(development))
        self.assertEqual(workflow.launch_command(packaging=True)[0], str(packaging))
        self.assertFalse(workflow.build_receipt_path(True).exists())
        for path, contents in preserved.items():
            self.assertEqual(path.read_bytes(), contents)

    def test_failed_packaging_rebuild_invalidates_only_packaging_receipt(self):
        workflow, executable = self.packaging_candidate()
        self.executable(workflow, "OpenArc")
        self.simulated_build(workflow)
        self.simulated_build(workflow, packaging=True)
        development_receipt = workflow.build_receipt_path(False).read_bytes()
        real_run = workflow.run

        def fail_compilation(args, **kwargs):
            if args[0] == str(workflow.depot / "gn"):
                raise openarc.WorkflowError("simulated packaging GN failure")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "ensure_bootstrap"), patch.object(workflow, "run", side_effect=fail_compilation):
            with self.assertRaisesRegex(openarc.WorkflowError, "simulated packaging GN failure"):
                workflow.build(2, packaging=True)
        self.assertTrue(executable.exists())
        self.assertEqual(workflow.build_receipt_path(False).read_bytes(), development_receipt)
        self.assertFalse(workflow.build_receipt_path(False, packaging=True).exists())
        with self.assertRaisesRegex(openarc.WorkflowError, "successful build receipt"):
            workflow.launch_command(packaging=True)
        workflow.launch_command()

    def test_packaging_modes_are_exclusive_before_work_starts(self):
        workflow = self.workflow()
        with patch.object(workflow, "require_mac") as require_mac, patch.object(workflow, "ensure_space") as space:
            for options in ({"baseline": True, "packaging": True},
                            {"reuse_baseline": True, "packaging": True},
                            {"baseline": True, "reuse_baseline": True}):
                with self.subTest(options=options), self.assertRaisesRegex(openarc.WorkflowError, "mutually exclusive"):
                    workflow.build(2, **options)
            require_mac.assert_not_called()
            space.assert_not_called()
        with self.assertRaisesRegex(openarc.WorkflowError, "mutually exclusive"):
            workflow.launch_command(baseline=True, packaging=True)
        self.assertFalse(workflow.work.exists())
        with patch.object(openarc, "Workflow") as constructor, patch("sys.stderr"):
            for arguments in (["build", "--baseline", "--packaging"],
                              ["build", "--reuse-baseline", "--packaging"],
                              ["build", "--baseline", "--reuse-baseline"],
                              ["launch", "--baseline", "--packaging"]):
                with self.subTest(arguments=arguments), self.assertRaises(SystemExit) as error:
                    openarc.main(arguments)
                self.assertEqual(error.exception.code, 2)
            constructor.assert_not_called()

    def test_packaging_output_is_reserved_from_normal_configuration(self):
        self.lock["build"]["output_dir"] = "out/Packaging"
        self.write_lock()
        with self.assertRaisesRegex(openarc.WorkflowError, "out/Packaging is reserved"):
            self.workflow()

    def test_normal_build_and_launch_do_not_inspect_packaging_output_or_receipt(self):
        self.repository()
        workflow = self.workflow()
        executable = self.executable(workflow)
        protected = self.root / "unrelated-output"
        protected.mkdir()
        workflow.packaging_output.symlink_to(protected, target_is_directory=True)
        workflow.build_receipt_path(False, packaging=True).write_text("invalid packaging receipt")
        ordinary = self.workflow()
        self.simulated_build(ordinary)
        self.assertEqual(ordinary.launch_command()[0], str(executable))
        self.assertEqual(workflow.build_receipt_path(False, packaging=True).read_text(), "invalid packaging receipt")
        self.assertEqual(list(protected.iterdir()), [])

    def test_packaging_requires_applied_openarc_identity_before_compiler_invocation(self):
        self.repository()
        workflow = self.workflow()
        with self.assertRaisesRegex(openarc.WorkflowError, "OpenArc patch series and product identity"):
            self.simulated_build(workflow, packaging=True)
        self.add_patch("01.patch", "base", "first")
        with self.assertRaisesRegex(openarc.WorkflowError, "not fully applied"):
            self.simulated_build(workflow, packaging=True)
        workflow.apply()
        with self.assertRaisesRegex(openarc.WorkflowError, "OpenArc patch series and product identity"):
            self.simulated_build(workflow, packaging=True)
        self.assertFalse(workflow.packaging_output.exists())
        self.assertFalse(workflow.build_receipt_path(False, packaging=True).exists())

    def test_packaging_cannot_use_a_leftover_chromium_binary(self):
        self.repository()
        workflow = self.workflow()
        self.apply_identity(workflow)
        chromium = self.executable(workflow, output=workflow.packaging_output)
        with self.assertRaisesRegex(openarc.WorkflowError, "executable OpenArc application"):
            self.simulated_build(workflow, packaging=True)
        self.assertTrue(chromium.exists())
        self.assertFalse(workflow.build_receipt_path(False, packaging=True).exists())

    def test_packaging_requires_non_debug_non_official_before_generation_or_receipt_changes(self):
        workflow, _ = self.packaging_candidate()
        self.simulated_build(workflow, packaging=True)
        receipt_path = workflow.build_receipt_path(False, packaging=True)
        original_receipt = receipt_path.read_bytes()
        original_lock = copy.deepcopy(self.lock)
        for name in ("is_debug", "is_official_build"):
            for value in (True, None):
                with self.subTest(name=name, value=value):
                    self.lock = copy.deepcopy(original_lock)
                    if value is None:
                        self.lock["build"]["gn_args"].pop(name)
                    else:
                        self.lock["build"]["gn_args"][name] = value
                    self.write_lock()
                    candidate = self.workflow()
                    real_run = candidate.run
                    compiler_calls = []

                    def no_compilation(args, **kwargs):
                        if args[0] in (str(candidate.depot / "gn"), str(candidate.depot / "autoninja")):
                            compiler_calls.append(args)
                            raise AssertionError("Invalid packaging configuration must not reach GN or the compiler")
                        return real_run(args, **kwargs)

                    with patch.object(candidate, "require_mac"), patch.object(candidate, "ensure_space"), \
                            patch.object(candidate, "ensure_bootstrap") as bootstrap, \
                            patch.object(candidate, "run", side_effect=no_compilation):
                        with self.assertRaisesRegex(openarc.WorkflowError, name + "=false"):
                            candidate.build(2, packaging=True)
                        bootstrap.assert_not_called()
                    self.assertEqual(compiler_calls, [])
                    self.assertEqual(receipt_path.read_bytes(), original_receipt)

    def test_packaging_receipt_rejects_wrong_configuration_output_and_other_mode(self):
        workflow, _ = self.packaging_candidate()
        self.simulated_build(workflow, packaging=True)
        original = workflow.read_build_receipt(False, packaging=True)
        for mutate, message in (
                (lambda value: value["inputs"]["effective_gn_args"].update(is_component_build=True), "does not match"),
                (lambda value: value["inputs"].update(packaging=False), "does not match"),
                (lambda value: value.update(output="out/Baseline"), "receipt output does not match"),
                (lambda value: value["binary"].update(path="out/OpenArc/OpenArc.app/Contents/MacOS/OpenArc"), "identity changed")):
            receipt = copy.deepcopy(original)
            mutate(receipt)
            openarc.atomic_json(workflow.build_receipt_path(False, packaging=True), receipt)
            with self.subTest(receipt=receipt), self.assertRaisesRegex(openarc.WorkflowError, message):
                workflow.launch_command(packaging=True)
        self.executable(workflow, "OpenArc")
        self.simulated_build(workflow)
        openarc.atomic_json(workflow.build_receipt_path(False, packaging=True), workflow.read_build_receipt(False))
        with self.assertRaisesRegex(openarc.WorkflowError, "does not match"):
            workflow.launch_command(packaging=True)

    def test_packaging_launch_rejects_changed_binary_and_bundle_metadata(self):
        workflow, executable = self.packaging_candidate()
        self.simulated_build(workflow, packaging=True)
        for path in (executable, executable.parent.parent / "Info.plist"):
            original = path.read_bytes()
            path.write_text("changed after packaging build\n")
            with self.subTest(path=path), self.assertRaisesRegex(openarc.WorkflowError, "identity changed"):
                workflow.launch_command(packaging=True)
            path.write_bytes(original)

    def test_packaging_launch_rejects_changed_lock_and_dependencies(self):
        workflow, _ = self.packaging_candidate()
        self.simulated_build(workflow, packaging=True)
        self.lock["build"]["gn_args"]["symbol_level"] = 1
        self.write_lock()
        with self.assertRaisesRegex(openarc.WorkflowError, "does not match"):
            self.workflow().launch_command(packaging=True)
        self.lock["build"]["gn_args"]["symbol_level"] = 0
        self.write_lock()
        self.add_dependency(workflow.src)
        with self.assertRaisesRegex(openarc.WorkflowError, "does not match"):
            workflow.launch_command(packaging=True)

    def test_packaging_rejects_redirected_output_and_profile(self):
        self.repository()
        workflow = self.workflow()
        self.apply_identity(workflow)
        protected = self.root / "existing-browser"
        protected.mkdir()
        marker = protected / "important-data"
        marker.write_text("preserve")
        workflow.packaging_output.parent.mkdir(parents=True)
        workflow.packaging_output.symlink_to(protected, target_is_directory=True)
        with self.assertRaisesRegex(openarc.WorkflowError, "symlink"):
            self.simulated_build(workflow, packaging=True)
        workflow.packaging_output.unlink()
        self.executable(workflow, "OpenArc", workflow.packaging_output)
        self.simulated_build(workflow, packaging=True)
        profile = workflow.work / "profiles/packaging"
        profile.parent.mkdir()
        profile.symlink_to(protected, target_is_directory=True)
        with self.assertRaisesRegex(openarc.WorkflowError, "symlink"):
            workflow.launch_command(packaging=True)
        self.assertEqual(marker.read_text(), "preserve")
        self.assertEqual(list(protected.iterdir()), [marker])

    def test_packaging_launchservices_uses_separate_bundle_profile_and_log(self):
        workflow, executable = self.packaging_candidate()
        self.simulated_build(workflow, packaging=True)
        calls = []
        real_run = subprocess.run

        def launch_helper(args, **kwargs):
            if args[0] != "/usr/bin/open":
                return real_run(args, **kwargs)
            calls.append(args)
            return subprocess.CompletedProcess(args, 0)

        with patch.object(workflow, "require_mac"), \
                patch.object(openarc.subprocess, "run", side_effect=launch_helper), \
                patch("builtins.print") as output:
            workflow.launch("https://example.com/", packaging=True)
        report = json.loads(output.call_args.args[0])
        expected = [str(executable), "--user-data-dir=" + str(workflow.work / "profiles/packaging"),
                    "--no-first-run", "--no-default-browser-check", "https://example.com/"]
        self.assertEqual(report["command"], expected)
        self.assertEqual(calls, [["/usr/bin/open", "-n", "-a", str(executable.parents[2]), "--args", *expected[1:]]])
        self.assertEqual(report["log"], str(workflow.work / "logs/packaging-launch.log"))
        self.assertIsNone(report["pid"])
        self.assertFalse((workflow.work / "profiles/development").exists())
        self.assertFalse((workflow.work / "profiles/baseline").exists())

    def test_packaging_receipt_is_not_recorded_if_lock_changes_during_compilation(self):
        workflow, _ = self.packaging_candidate()
        real_run = workflow.run

        def compiler(args, **kwargs):
            if args[0] in (str(workflow.depot / "gn"), str(workflow.depot / "autoninja")):
                if args[0] == str(workflow.depot / "autoninja"):
                    self.lock["build"]["gn_args"]["symbol_level"] = 1
                    self.write_lock()
                return subprocess.CompletedProcess(args, 0, "", "")
            return real_run(args, **kwargs)

        with patch.object(workflow, "require_mac"), patch.object(workflow, "ensure_space"), \
                patch.object(workflow, "ensure_bootstrap"), patch.object(workflow, "run", side_effect=compiler):
            with self.assertRaisesRegex(openarc.WorkflowError, "upstream.lock changed"):
                workflow.build(2, packaging=True)
        self.assertFalse(workflow.build_receipt_path(False, packaging=True).exists())

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
