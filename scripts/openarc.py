#!/usr/bin/env python3
"""Pinned, non-destructive Chromium development workflow. Python standard library only."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from typing import Any
from urllib.parse import urlparse


CHROMIUM_URL = "https://chromium.googlesource.com/chromium/src.git"
DEPOT_URL = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"
SHA = re.compile(r"[0-9a-f]{40}\Z")
GN_NAME = re.compile(r"[A-Za-z_][A-Za-z_0-9]*\Z")
GIB = 1024**3


class WorkflowError(Exception):
    """An actionable failure that must not discard local work."""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    # A unique temporary file avoids overwriting a user's stale .tmp file or
    # following a pre-existing temporary-file symlink.
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                     prefix=path.name + ".", suffix=".tmp", delete=False) as file:
        temporary = Path(file.name)
        try:
            file.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
            file.flush()
            os.fsync(file.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def relative_path(value: Any, label: str) -> Path:
    if not isinstance(value, str) or not value or "\\" in value:
        raise WorkflowError(f"{label} must be a nonempty relative POSIX path")
    path = Path(value)
    if path.is_absolute() or any(part in (".", "..") for part in value.split("/")):
        raise WorkflowError(f"{label} must not be absolute or contain . or ..")
    return path


def gn_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str) and "\n" not in value and "\r" not in value:
        # GN uses $ for expansion, including inside quoted strings.
        return json.dumps(value).replace("$", "\\$")
    if isinstance(value, list):
        return "[" + ", ".join(gn_value(item) for item in value) + "]"
    raise WorkflowError(f"Unsupported GN argument value: {value!r}")


class Workflow:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self.work = self.safe_path(self.root / ".build")
        self.depot = self.safe_path(self.work / "depot_tools")
        self.checkout = self.safe_path(self.work / "chromium")
        self.src = self.safe_path(self.checkout / "src")
        self.state_file = self.safe_path(self.work / "patch-state.json")
        self.promotion_file = self.safe_path(self.work / "baseline-promotion.json")
        try:
            self.lock = json.loads((self.root / "upstream.lock").read_text())
        except (OSError, ValueError) as exc:
            raise WorkflowError(f"Cannot read upstream.lock: {exc}") from exc
        self.validate_lock()
        self.output = self.safe_path(self.src / self.lock["build"]["output_dir"])
        self.baseline_output = self.safe_path(self.src / "out" / "Baseline")
        self.env = os.environ.copy()
        self.env["PATH"] = str(self.depot) + os.pathsep + self.env.get("PATH", "")
        self.env["DEPOT_TOOLS_UPDATE"] = "0"
        self.env["DEPOT_TOOLS_METRICS"] = "0"
        self.env["DEPOT_TOOLS_DIR"] = str(self.depot)
        self.env["DEPOT_TOOLS_BOOTSTRAP_PYTHON3"] = "1"

    def safe_path(self, path: Path) -> Path:
        """Refuse paths escaping the project or redirected through symlinks."""
        try:
            parts = path.relative_to(self.root).parts
        except ValueError as exc:
            raise WorkflowError(f"Path is outside the project: {path}") from exc
        current = self.root
        for part in parts:
            current /= part
            if current.is_symlink():
                raise WorkflowError(f"Refusing symlink in managed path: {current}")
        if not path.resolve().is_relative_to(self.root):
            raise WorkflowError(f"Path escapes the project: {path}")
        return path

    def validate_lock(self) -> None:
        lock = self.lock
        if not isinstance(lock, dict) or lock.get("schema_version") != 1:
            raise WorkflowError("upstream.lock must use schema_version 1")
        for name, url in (("chromium", CHROMIUM_URL), ("depot_tools", DEPOT_URL)):
            item = lock.get(name)
            if not isinstance(item, dict) or item.get("repository") != url:
                raise WorkflowError(f"{name}.repository must be {url}")
            if not isinstance(item.get("revision"), str) or not SHA.fullmatch(item["revision"]):
                raise WorkflowError(f"{name}.revision must be an exact 40-character lowercase commit SHA")
        build = lock.get("build")
        if not isinstance(build, dict) or build.get("target_os") != "mac" or build.get("target_cpu") != "arm64":
            raise WorkflowError("This workflow currently supports target_os mac and target_cpu arm64")
        output = relative_path(build.get("output_dir"), "build.output_dir")
        if len(output.parts) != 2 or output.parts[0] != "out":
            raise WorkflowError("build.output_dir must be a direct child of out/, e.g. out/OpenArc")
        if output == Path("out/Baseline"):
            raise WorkflowError("out/Baseline is reserved; use build --reuse-baseline for explicit promotion")
        args = build.get("gn_args")
        if not isinstance(args, dict) or args.get("target_cpu") != "arm64":
            raise WorkflowError("build.gn_args must include target_cpu: arm64")
        if args.get("use_sandbox") is False or args.get("is_chrome_branded") is True:
            raise WorkflowError("Sandbox disabling and Google Chrome branding are not supported")
        for name, value in args.items():
            if not GN_NAME.fullmatch(name):
                raise WorkflowError(f"Invalid GN argument name: {name}")
            gn_value(value)

    def run(self, args: list[str], cwd: Path | None = None, *, capture: bool = False,
            check: bool = True) -> subprocess.CompletedProcess:
        if not capture:
            print("+ " + shlex.join(str(arg) for arg in args), flush=True)
        try:
            result = subprocess.run(args, cwd=cwd or self.root, env=self.env,
                                    text=True, capture_output=capture, check=False)
        except OSError as exc:
            raise WorkflowError(f"Cannot execute {args[0]}: {exc}") from exc
        if check and result.returncode:
            detail = (result.stderr or result.stdout or "").strip()[-4000:]
            raise WorkflowError(f"Command failed ({result.returncode}): {shlex.join(args)}\n{detail}")
        return result

    def git(self, repo: Path, *args: str, check: bool = True) -> str:
        return self.run(["git", "-C", str(repo), *args], capture=True, check=check).stdout.strip()

    def revision(self, repo: Path) -> str:
        return self.git(repo, "rev-parse", "HEAD")

    def assert_pin(self, repo: Path, component: str) -> None:
        self.safe_path(repo)
        if self.revision(repo) != self.lock[component]["revision"]:
            raise WorkflowError(f"{component} checkout is not at the locked commit; use fetch before continuing")

    def status(self, repo: Path) -> str:
        return self.git(repo, "status", "--porcelain", "--untracked-files=normal")

    def assert_clean(self, repo: Path) -> None:
        status = self.status(repo)
        if status:
            raise WorkflowError(f"Preserving modified checkout {repo}; commit/stash or resolve changes manually:\n{status[:3000]}")

    def ensure_space(self, minimum_gib: int) -> None:
        free = shutil.disk_usage(self.root).free
        if free < minimum_gib * GIB:
            raise WorkflowError(f"Only {free / GIB:.1f} GiB free; this operation requires at least {minimum_gib} GiB headroom. No files were removed.")

    def doctor(self) -> int:
        tools = {name: shutil.which(name, path=self.env["PATH"])
                 for name in ("git", "python3", "xcodebuild", "xcrun", "gn", "gclient", "autoninja")}
        problems = []
        if sys.platform != "darwin" or platform.machine() != "arm64":
            problems.append("Build/launch require an Apple Silicon Mac")
        for name in ("git", "python3", "xcodebuild", "xcrun"):
            if not tools[name]:
                problems.append(f"Missing {name}")
        sdk = None
        required_sdk = None
        if tools["xcrun"]:
            result = self.run(["xcrun", "--sdk", "macosx", "--show-sdk-version"], capture=True, check=False)
            sdk = result.stdout.strip()
            if result.returncode:
                problems.append("xcrun cannot locate the macOS SDK")
        sdk_config = self.safe_path(self.src / "build/config/mac/mac_sdk.gni")
        if sdk_config.is_file():
            match = re.search(r'mac_sdk_official_version\s*=\s*"([^"\n]+)"', sdk_config.read_text())
            if match:
                required_sdk = match.group(1)
        free = round(shutil.disk_usage(self.root).free / GIB, 1)
        if free < 40:
            problems.append("Less than 40 GiB free build headroom")
        print(json.dumps({"platform": sys.platform, "machine": platform.machine(), "python": platform.python_version(),
                          "sdk": sdk, "upstream_official_sdk": required_sdk, "free_gib": free, "tools": tools,
                          "chromium_revision": self.lock["chromium"]["revision"],
                          "checkout": str(self.src), "problems": problems}, indent=2))
        print("depot_tools commands become available after fetch. SDK compatibility is determined by the pinned Chromium ref.")
        if sdk and required_sdk and sdk != required_sdk:
            print(f"Warning: installed SDK {sdk} differs from upstream official SDK {required_sdk}; development compatibility needs a real build.")
        return 1 if problems else 0

    def checkout_repo(self, path: Path, component: str) -> None:
        self.safe_path(path)
        spec = self.lock[component]
        if path.exists() and not (path / ".git").exists():
            if any(path.iterdir()):
                raise WorkflowError(f"Refusing non-repository, nonempty destination: {path}")
        path.mkdir(parents=True, exist_ok=True)
        if not (path / ".git").exists():
            self.run(["git", "init", str(path)])
            self.run(["git", "-C", str(path), "remote", "add", "origin", spec["repository"]])
        if self.git(path, "remote", "get-url", "origin") != spec["repository"]:
            raise WorkflowError(f"Unexpected origin in {path}; refusing to replace it")
        self.assert_clean(path)
        existing = self.run(["git", "-C", str(path), "rev-parse", "--verify", "HEAD"], capture=True, check=False)
        if existing.returncode == 0 and existing.stdout.strip() == spec["revision"]:
            print(f"{component}: already at locked commit")
            return
        self.run(["git", "-C", str(path), "fetch", "--depth=1", "origin", spec["revision"]])
        self.run(["git", "-C", str(path), "checkout", "--detach", spec["revision"]])
        self.assert_pin(path, component)

    def fetch(self) -> None:
        self.ensure_space(20)
        # Check BOTH existing trees before mutating either one.
        for path in (self.depot, self.src):
            if (path / ".git").exists():
                self.assert_clean(path)
        self.checkout_repo(self.depot, "depot_tools")
        self.ensure_bootstrap()
        self.checkout_repo(self.src, "chromium")
        print("Pinned source checkouts ready. Run sync to fetch dependencies and run hooks.")

    def ensure_bootstrap(self) -> None:
        # DEPOT_TOOLS_UPDATE=0 intentionally disables gclient's implicit update
        # and bootstrap. Upstream ensure_bootstrap installs the pinned tooling
        # dependencies without updating its Git checkout. Its path must be
        # absolute because bootstrap_python3 changes the working directory.
        self.run([str(self.depot / "ensure_bootstrap")], cwd=self.depot)
        # The upstream shell script can exit successfully after a package error,
        # so verify the Python launcher actually used by GN, not only a marker.
        result = self.run([str(self.depot / "python-bin" / "python3"), "--version"],
                          cwd=self.depot, capture=True)
        if not re.fullmatch(r"Python 3\.\d+\.\d+(?:\S*)", result.stdout.strip()):
            raise WorkflowError("depot_tools bootstrap did not produce a working Python 3 launcher")
        self.assert_pin(self.depot, "depot_tools")

    def bootstrap(self) -> None:
        self.assert_pin(self.depot, "depot_tools")
        self.assert_clean(self.depot)
        self.ensure_bootstrap()

    def dependency_entries(self, required: bool = False) -> dict:
        entries = self.safe_path(self.checkout / ".gclient_entries")
        if not entries.exists():
            if required:
                raise WorkflowError("Missing completed .gclient_entries dependency inventory; finish sync before building or launching")
            return {}
        try:
            tree = ast.parse(entries.read_text())
            value = next(node.value for node in tree.body if isinstance(node, ast.Assign)
                         and any(isinstance(target, ast.Name) and target.id == "entries" for target in node.targets))
            mapping = ast.literal_eval(value)
            if not isinstance(mapping, dict) or (required and not mapping):
                raise ValueError("entries must be a dictionary")
        except (OSError, ValueError, SyntaxError, StopIteration) as exc:
            raise WorkflowError(f"Cannot safely inspect .gclient_entries: {exc}") from exc
        for name, value in mapping.items():
            path, kind = self.dependency_location(name, value)
            if required and value is not None:
                if not path.is_dir():
                    raise WorkflowError(f"Missing dependency directory: {name}; finish sync")
                if kind == "git" and not (path / ".git").exists():
                    raise WorkflowError(f"Missing Git dependency checkout: {name}; finish sync")
        return mapping

    def dependency_location(self, name: str, value: str | None) -> tuple[Path, str]:
        if not isinstance(name, str) or (value is not None and not isinstance(value, str)):
            raise WorkflowError(f"Invalid dependency inventory entry: {name}")
        # gclient._SaveEntries serializes dependency.name, not a filesystem path.
        # GcsDependency uses '<directory>:<object_name>'; CipdDependency uses
        # '<directory>:<package>'. Both install into the directory before ':'.
        directory, separator, package = name.partition(":")
        kind = "git" if value is not None else "disabled"
        if separator:
            parsed = urlparse(value) if value is not None else None
            if not package or (parsed is not None and not (
                    parsed.scheme == "gs" or (parsed.scheme == "https"
                                              and parsed.netloc == "chrome-infra-packages.appspot.com"))):
                raise WorkflowError(f"Unrecognized packaged dependency entry: {name}")
            kind = "package" if value is not None else "disabled"
        path = self.safe_path(self.checkout / relative_path(directory, "dependency path"))
        return path, kind

    def dependency_repos(self) -> list[Path]:
        repos = []
        for name, value in self.dependency_entries().items():
            path, kind = self.dependency_location(name, value)
            if kind == "git" and (path / ".git").exists() and path != self.src:
                repos.append(path)
        return repos

    def dependency_evidence(self) -> dict:
        entries = self.dependency_entries(required=True)
        revisions = {}
        for name, value in sorted(entries.items()):
            if value is None:
                continue
            path, kind = self.dependency_location(name, value)
            if kind == "git" and path != self.src and (path / ".git").exists():
                self.assert_clean(path)
                revisions[name] = self.revision(path)
        return {"entries": entries, "git_revisions": revisions}

    def sync(self) -> None:
        self.ensure_space(40)
        self.assert_pin(self.depot, "depot_tools")
        self.assert_pin(self.src, "chromium")
        for repo in [self.depot, self.src, *self.dependency_repos()]:
            self.assert_clean(repo)
        self.ensure_bootstrap()
        configuration = ("solutions = " + repr([{"name": "src", "url": CHROMIUM_URL,
                         "managed": False, "custom_deps": {}, "custom_vars": {
                             "checkout_pgo_profiles": self.lock["build"]["gn_args"].get("chrome_pgo_phase", 0) != 0}}])
                         + "\ntarget_os = ['mac']\n")
        config_path = self.safe_path(self.checkout / ".gclient")
        if config_path.exists() and config_path.read_text() != configuration:
            raise WorkflowError(f"Existing {config_path} differs from locked configuration; preserving it for manual review")
        config_path.write_text(configuration)
        self.run([str(self.depot / "gclient"), "sync", "--no-history", "--revision",
                  "src@" + self.lock["chromium"]["revision"]], cwd=self.checkout)
        self.assert_pin(self.src, "chromium")

    def patches(self) -> list[tuple[str, Path, str]]:
        directory = self.safe_path(self.root / "patches" / "chromium")
        series = self.safe_path(directory / "series")
        if not series.is_file():
            raise WorkflowError("Missing patches/chromium/series (an empty file is valid for the baseline)")
        result = []
        seen = set()
        for line in series.read_text().splitlines():
            name = line.strip()
            if not name or name.startswith("#"):
                continue
            relative = relative_path(name, "patch name")
            if relative.suffix != ".patch" or name in seen:
                raise WorkflowError(f"Patch names must be unique .patch paths: {name}")
            seen.add(name)
            path = self.safe_path(directory / relative)
            if not path.is_file():
                raise WorkflowError(f"Missing patch: {path}")
            result.append((name, path, digest(path.read_bytes())))
        return result

    def tree_digest(self) -> str:
        # --index patch application puts all new files in the index. Include both
        # staged and unstaged changes relative to HEAD; untracked files are separate.
        value = self.run(["git", "-C", str(self.src), "diff", "--binary", "HEAD"], capture=True).stdout
        return digest(value.encode())

    def patch_state(self, patches: list[tuple[str, Path, str]]) -> dict:
        if not self.state_file.exists():
            self.assert_clean(self.src)
            return {"revision": self.lock["chromium"]["revision"], "applied": [], "tree_digest": self.tree_digest()}
        try:
            state = json.loads(self.state_file.read_text())
            expected = [{"name": name, "sha256": sha} for name, _, sha in patches]
            applied = state["applied"]
            if (state["revision"] != self.lock["chromium"]["revision"]
                    or not isinstance(applied, list) or applied != expected[:len(applied)]
                    or len(applied) > len(expected)):
                raise WorkflowError("Applied patch state does not match the lock/series; preserve and reconcile the checkout manually")
            untracked = self.git(self.src, "ls-files", "--others", "--exclude-standard")
            unstaged = self.git(self.src, "diff", "--name-only")
            if untracked or unstaged or state["tree_digest"] != self.tree_digest():
                raise WorkflowError("Checkout changed outside the recorded patch application; preserving local work")
            return state
        except (OSError, ValueError, KeyError, TypeError) as exc:
            raise WorkflowError(f"Invalid patch state; preserving checkout: {exc}") from exc

    def apply(self) -> None:
        self.assert_pin(self.src, "chromium")
        patches = self.patches()
        state = self.patch_state(patches)
        for name, path, sha in patches[len(state["applied"]):]:
            self.run(["git", "-C", str(self.src), "apply", "--check", "--index", str(path)])
            self.run(["git", "-C", str(self.src), "apply", "--index", str(path)])
            state["applied"].append({"name": name, "sha256": sha})
            state["tree_digest"] = self.tree_digest()
            atomic_json(self.state_file, state)
        if patches:
            print(f"{len(patches)} patches applied and verified; no duplicate application")
        else:
            print("Empty patch series: unmodified Chromium baseline")

    def unapply(self) -> None:
        self.assert_pin(self.src, "chromium")
        patches = self.patches()
        state = self.patch_state(patches)
        count = len(state["applied"])
        for _, path, _ in reversed(patches[:count]):
            self.run(["git", "-C", str(self.src), "apply", "--reverse", "--check", "--index", str(path)])
            self.run(["git", "-C", str(self.src), "apply", "--reverse", "--index", str(path)])
            state["applied"].pop()
            state["tree_digest"] = self.tree_digest()
            atomic_json(self.state_file, state)
        self.assert_clean(self.src)
        print(f"{count} recorded patches removed; source is pristine at the locked revision. Build evidence is unchanged.")

    def require_mac(self) -> None:
        if sys.platform != "darwin" or platform.machine() != "arm64":
            raise WorkflowError("Build and launch currently require an Apple Silicon Mac")

    def build_receipt_path(self, baseline: bool) -> Path:
        return self.safe_path(self.work / ("baseline-build-info.json" if baseline else "build-info.json"))

    def build_inputs(self, baseline: bool) -> dict:
        self.assert_pin(self.src, "chromium")
        try:
            current_lock = json.loads((self.root / "upstream.lock").read_text())
        except (OSError, ValueError) as exc:
            raise WorkflowError(f"Cannot revalidate upstream.lock: {exc}") from exc
        if current_lock != self.lock:
            raise WorkflowError("upstream.lock changed during the operation; rerun with the new configuration")
        if baseline:
            self.assert_clean(self.src)
            applied = []
        else:
            patches = self.patches()
            state = self.patch_state(patches)
            if len(state["applied"]) != len(patches):
                raise WorkflowError("Patch series is not fully applied; run apply before build, or use --baseline for clean upstream")
            applied = state["applied"]
        return {"baseline": baseline, "upstream": self.lock, "patches": applied,
                "checkout_diff_sha256": self.tree_digest(), "dependencies": self.dependency_evidence()}

    def binary_identity(self, executable: Path) -> dict:
        info = self.safe_path(executable.parent.parent / "Info.plist")
        return {"path": str(executable.relative_to(self.src)), "sha256": file_digest(executable),
                "info_plist_sha256": file_digest(info) if info.is_file() else None}

    def read_build_receipt(self, baseline: bool) -> dict:
        try:
            receipt = json.loads(self.build_receipt_path(baseline).read_text())
        except (OSError, ValueError) as exc:
            raise WorkflowError("No readable successful build receipt for this mode; run build"
                                + (" --baseline" if baseline else "") + " before launch or promotion") from exc
        if not isinstance(receipt, dict) or receipt.get("receipt_version") != 1:
            raise WorkflowError("Unsupported successful build receipt; rebuild before launch or promotion")
        return receipt

    def validate_baseline_seed(self, receipt: Any, inputs: dict) -> None:
        # Known applied patches are already validated by build_inputs(). The
        # original baseline must have built these same pinned inputs, before
        # the recorded patch overlay was applied.
        pristine = dict(inputs, baseline=True, patches=[], checkout_diff_sha256=digest(b""))
        if (not isinstance(receipt, dict) or receipt.get("receipt_version") != 1
                or receipt.get("inputs") != pristine or receipt.get("output") != "out/Baseline"
                or not isinstance(receipt.get("binary"), dict)
                or receipt["binary"].get("path") != "out/Baseline/Chromium.app/Contents/MacOS/Chromium"):
            raise WorkflowError("Baseline receipt does not match the current pristine lock and dependencies; cannot reuse output")

    def promotion_state(self, inputs: dict) -> dict | None:
        self.safe_path(self.promotion_file)
        if not self.promotion_file.exists():
            return None
        try:
            state = json.loads(self.promotion_file.read_text())
        except (OSError, ValueError) as exc:
            raise WorkflowError("Unreadable baseline promotion state; preserve it for manual review") from exc
        if (not isinstance(state, dict) or state.get("schema_version") != 1
                or state.get("output") != "out/Baseline"):
            raise WorkflowError("Invalid baseline promotion state; preserve it for manual review")
        self.validate_baseline_seed(state.get("baseline_receipt"), inputs)
        return state

    def build_output(self, baseline: bool, inputs: dict, reuse_baseline: bool = False) -> tuple[Path, dict | None]:
        if baseline:
            if reuse_baseline or self.promotion_file.exists():
                raise WorkflowError("Baseline output is reserved for promoted OpenArc; it cannot qualify an unmodified baseline")
            return self.baseline_output, None
        state = self.promotion_state(inputs)
        if not state and not reuse_baseline:
            return self.output, None
        branding = self.safe_path(self.src / "chrome/app/theme/chromium/BRANDING")
        if not branding.is_file() or "PRODUCT_FULLNAME=OpenArc" not in branding.read_text().splitlines():
            raise WorkflowError("Baseline promotion requires the applied OpenArc product identity")
        if not state:
            receipt = self.read_build_receipt(True)
            self.validate_baseline_seed(receipt, inputs)
            if receipt["binary"] != self.binary_identity(self.executable(True)):
                raise WorkflowError("Baseline executable or bundle identity changed; cannot reuse its build receipt")
            state = {"schema_version": 1, "output": "out/Baseline", "baseline_receipt": receipt}
        return self.baseline_output, state

    def build(self, jobs: int, baseline: bool = False, reuse_baseline: bool = False) -> None:
        self.require_mac()
        if not 1 <= jobs <= 64:
            raise WorkflowError("--jobs must be between 1 and 64")
        self.ensure_space(40)
        self.assert_pin(self.depot, "depot_tools")
        self.assert_pin(self.src, "chromium")
        self.assert_clean(self.depot)
        inputs = self.build_inputs(baseline)
        output, promotion = self.build_output(baseline, inputs, reuse_baseline)
        output_relative = str(output.relative_to(self.src))
        args = "\n".join(f"{name} = {gn_value(value)}" for name, value in sorted(self.lock["build"]["gn_args"].items()))
        receipt_path = self.build_receipt_path(baseline)
        if promotion:
            # This marker preserves historical baseline evidence, not a usable
            # baseline artifact. It also blocks baseline launch if interrupted
            # before removing its former successful receipt.
            atomic_json(self.promotion_file, promotion)
            self.build_receipt_path(True).unlink(missing_ok=True)
        # Once compilation starts, an earlier success must not qualify binaries
        # left behind by a failed or interrupted rebuild.
        receipt_path.unlink(missing_ok=True)
        self.ensure_bootstrap()
        self.run([str(self.depot / "gn"), "gen", output_relative, "--args=" + args], cwd=self.src)
        self.run([str(self.depot / "autoninja"), "-C", output_relative, "-j", str(jobs), "chrome"], cwd=self.src)
        if self.build_inputs(baseline) != inputs:
            raise WorkflowError("Source, patch series or dependency inventory changed during the build; no successful receipt recorded")
        atomic_json(receipt_path, {
            "receipt_version": 1, "inputs": inputs,
            "binary": self.binary_identity(self.executable(baseline, output, "OpenArc" if promotion else None)),
            "jobs": jobs, "output": output_relative,
            "qualification": "Compilation succeeded; runtime, extensions, signing and release qualification remain separate."})
        print(f"Build completed: {output}")

    def verified_executable(self, baseline: bool) -> Path:
        if baseline and self.promotion_file.exists():
            raise WorkflowError("Baseline output was promoted to OpenArc; the historical baseline receipt cannot authorize launch")
        receipt = self.read_build_receipt(baseline)
        inputs = self.build_inputs(baseline)
        if receipt.get("inputs") != inputs:
            raise WorkflowError("Successful build receipt does not match the current mode, lock, patches or dependencies; rebuild before launch")
        output, promotion = self.build_output(baseline, inputs)
        if receipt.get("output") != str(output.relative_to(self.src)):
            raise WorkflowError("Successful build receipt output does not match the selected managed output; rebuild before launch")
        executable = self.executable(baseline, output, "OpenArc" if promotion else None)
        if receipt.get("binary") != self.binary_identity(executable):
            raise WorkflowError("Browser executable or bundle identity changed since the successful build; rebuild before launch")
        return executable

    def executable(self, baseline: bool = False, output: Path | None = None, product: str | None = None) -> Path:
        output = output if output is not None else (self.baseline_output if baseline else self.output)
        names = ("Chromium",) if baseline else ((product,) if product else ("OpenArc", "Chromium"))
        candidates = [self.safe_path(output / (name + ".app") / "Contents" / "MacOS" / name)
                      for name in names]
        found = [path for path in candidates if path.is_file() and os.access(path, os.X_OK)]
        if len(found) != 1:
            raise WorkflowError("Expected exactly one executable " + "/".join(names) + " application in the selected output directory; found "
                                + str(len(found)) + ". Preserve/review stale build outputs manually.")
        return found[0]

    def launch_command(self, url: str | None = None, baseline: bool = False) -> list[str]:
        executable = self.verified_executable(baseline)
        profile = self.safe_path(self.work / "profiles" / ("baseline" if baseline else "development"))
        profile.mkdir(parents=True, exist_ok=True)
        args = [str(executable), "--user-data-dir=" + str(profile), "--no-first-run", "--no-default-browser-check"]
        if baseline:
            args.append("--use-mock-keychain")
        if url:
            parsed = urlparse(url)
            if parsed.scheme not in ("http", "https") or not parsed.netloc:
                raise WorkflowError("Launch URL must be an explicit http:// or https:// URL")
            args.append(url)
        return args

    def launch(self, url: str | None, baseline: bool = False) -> None:
        self.require_mac()
        args = self.launch_command(url, baseline)
        log_path = self.safe_path(self.work / "logs" / ("baseline-launch.log" if baseline else "launch.log"))
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with log_path.open("ab") as log:
            child = subprocess.Popen(args, cwd=self.src, env=self.env, stdin=subprocess.DEVNULL,
                                     stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        qualification = "Process started; visible UI and browsing have not been verified by this command."
        if baseline:
            qualification += " Baseline uses a mock Keychain and a synthetic test profile only: do not enter real credentials. Credential storage and native password-manager qualification are excluded."
        print(json.dumps({"pid": child.pid, "command": args, "log": str(log_path),
                          "qualification": qualification}, indent=2))

    def check(self, checkout: bool = False) -> None:
        patches = self.patches()
        for _, path, _ in patches:
            self.run(["git", "apply", "--numstat", str(path)], capture=True)
        details = {"lock": "valid", "patch_count": len(patches), "checkout": "not inspected (use --checkout)"}
        if checkout:
            self.assert_pin(self.src, "chromium")
            state = self.patch_state(patches)
            details["checkout"] = "locked revision; " + str(len(state["applied"])) + " recorded patches verified"
        print(json.dumps(details, indent=2))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("doctor", "fetch", "bootstrap", "sync", "apply", "unapply"):
        commands.add_parser(name)
    check = commands.add_parser("check")
    check.add_argument("--checkout", action="store_true", help="Also validate the existing checkout and recorded patch state")
    build = commands.add_parser("build")
    build.add_argument("--jobs", type=int, default=8, help="Compiler concurrency (default: 8)")
    build.add_argument("--baseline", action="store_true", help="Build clean upstream in out/Baseline without applying project patches")
    build.add_argument("--reuse-baseline", action="store_true", help="Promote a verified baseline output to incremental OpenArc output; apply patches first")
    launch = commands.add_parser("launch")
    launch.add_argument("url", nargs="?", help="Optional explicit HTTP(S) URL")
    launch.add_argument("--baseline", action="store_true", help="Launch out/Baseline with a separate baseline profile")
    args = parser.parse_args(argv)
    try:
        workflow = Workflow(Path(__file__).resolve().parent.parent)
        if args.command == "build":
            workflow.build(args.jobs, args.baseline, args.reuse_baseline)
        elif args.command == "launch":
            workflow.launch(args.url, args.baseline)
        elif args.command == "check":
            workflow.check(args.checkout)
        else:
            result = getattr(workflow, args.command)()
            if isinstance(result, int):
                return result
        return 0
    except (WorkflowError, OSError) as exc:
        print(f"openarc: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("openarc: interrupted; existing source and partial downloads preserved", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
