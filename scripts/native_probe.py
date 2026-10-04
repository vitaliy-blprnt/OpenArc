#!/usr/bin/env python3
"""Synthetic native-messaging ping probe; installation is confined to .build."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import stat
import struct
import sys
import tempfile
from typing import BinaryIO, TextIO


HOST_NAME = "org.openarc.platform_probe"
MAX_MESSAGE_BYTES = 4096
MAX_NONCE_BYTES = 128
EXTENSION_ID = re.compile(r"[a-p]{32}\Z")
HEADER = struct.Struct("=I")  # Native byte order, exactly four bytes.


class ProbeError(Exception):
    pass


def read_exact(stream: BinaryIO, count: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < count:
        chunk = stream.read(count - len(chunks))
        if not chunk:
            break
        chunks.extend(chunk)
    return bytes(chunks)


def read_frame(stream: BinaryIO) -> bytes | None:
    header = read_exact(stream, HEADER.size)
    if not header:
        return None
    if len(header) != HEADER.size:
        raise ProbeError("Truncated native message header")
    size = HEADER.unpack(header)[0]
    if not 1 <= size <= MAX_MESSAGE_BYTES:
        raise ProbeError("Native message length is outside the supported bounds")
    body = read_exact(stream, size)
    if len(body) != size:
        raise ProbeError("Truncated native message body")
    return body


def unique_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ProbeError("Duplicate message key")
        result[key] = value
    return result


def reject_constant(value: str) -> None:
    raise ProbeError("Invalid JSON constant")


def reply_for(body: bytes) -> dict:
    try:
        request = json.loads(body.decode("utf-8"), object_pairs_hook=unique_object,
                             parse_constant=reject_constant)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise ProbeError("Invalid JSON request") from exc
    if not isinstance(request, dict) or set(request) != {"type", "nonce"} or request["type"] != "ping":
        raise ProbeError("Only a ping request with type and nonce is supported")
    nonce = request["nonce"]
    try:
        valid_nonce = isinstance(nonce, str) and 1 <= len(nonce.encode("utf-8")) <= MAX_NONCE_BYTES
    except UnicodeError:
        valid_nonce = False
    if not valid_nonce:
        raise ProbeError("Nonce must be a string of 1 to 128 UTF-8 bytes")
    return {"type": "pong", "nonce": nonce, "protocol": 1}


def write_frame(stream: BinaryIO, message: dict) -> None:
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(body) > MAX_MESSAGE_BYTES:
        raise ProbeError("Response exceeds the supported bounds")
    stream.write(HEADER.pack(len(body)) + body)
    stream.flush()


def host_main(reader: BinaryIO, writer: BinaryIO, errors: TextIO) -> int:
    """The host reads only its framed input and writes only framed replies."""
    try:
        while True:
            body = read_frame(reader)
            if body is None:
                return 0
            write_frame(writer, reply_for(body))
    except (ProbeError, OSError) as exc:
        # Do not log message contents, environment values, or filesystem data.
        message = str(exc) if isinstance(exc, ProbeError) else "Native message I/O failed"
        errors.write("native-probe: " + message + "\n")
        return 2


class Installer:
    def __init__(self, root: Path, mode: str, extension_id: str):
        if mode not in ("baseline", "development"):
            raise ProbeError("Mode must be baseline or development")
        if not EXTENSION_ID.fullmatch(extension_id):
            raise ProbeError("Extension ID must contain exactly 32 lowercase letters a through p")
        self.root = root.resolve()
        self.mode = mode
        self.build = self.safe(self.root / ".build")
        self.host_script = self.root / "scripts" / "native_probe.py"
        if ((self.root / "scripts").is_symlink() or self.host_script.is_symlink()
                or not self.host_script.is_file() or not self.host_script.resolve().is_relative_to(self.root)):
            raise ProbeError("Expected a regular tracked scripts/native_probe.py")
        self.wrapper = self.safe(self.build / "tools" / ("native-probe-" + mode))
        self.manifest = self.safe(self.build / "profiles" / mode / "NativeMessagingHosts" / (HOST_NAME + ".json"))
        identity = "\n".join((str(self.root), str(self.host_script), sys.executable, mode))
        owner = hashlib.sha256(identity.encode()).hexdigest()
        self.wrapper_text = ("#!/bin/sh\n# OpenArc-owned synthetic native probe v1 " + owner + "\n"
                             + "exec " + shlex.quote(sys.executable) + " " + shlex.quote(str(self.host_script)) + " host\n")
        self.manifest_data = {
            "name": HOST_NAME,
            "description": "OpenArc synthetic transport probe v1 " + owner,
            "path": str(self.wrapper),
            "type": "stdio",
            "allowed_origins": ["chrome-extension://" + extension_id + "/"],
        }
        self.manifest_text = json.dumps(self.manifest_data, indent=2, sort_keys=True) + "\n"

    def safe(self, path: Path) -> Path:
        try:
            relative = path.relative_to(self.root / ".build")
        except ValueError as exc:
            raise ProbeError("Native probe paths must stay inside the project's .build directory") from exc
        current = self.root
        for part in (".build", *relative.parts):
            current /= part
            if current.is_symlink():
                raise ProbeError("Refusing a symlink in a native probe path")
        if not path.resolve().is_relative_to(self.root / ".build"):
            raise ProbeError("Native probe path escapes .build")
        return path

    def verify_existing(self, path: Path, expected: str) -> bool:
        self.safe(path)
        try:
            metadata = path.lstat()
        except FileNotFoundError:
            return False
        if (not stat.S_ISREG(metadata.st_mode) or metadata.st_uid != os.getuid()
                or metadata.st_nlink != 1):
            raise ProbeError("Refusing a non-owned native probe file: " + str(path))
        try:
            actual = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            raise ProbeError("Refusing an unreadable existing native probe file: " + str(path)) from exc
        if actual != expected:
            raise ProbeError("Refusing to replace or remove an unrecognized existing native probe file: " + str(path))
        return True

    def create_owned(self, path: Path, content: str, mode: int) -> None:
        self.safe(path)
        if self.verify_existing(path, content):
            if mode & stat.S_IXUSR and not os.access(path, os.X_OK):
                path.chmod(mode)
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(prefix=".openarc-native-", dir=path.parent)
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as file:
                file.write(content)
                file.flush()
                os.fsync(file.fileno())
                os.fchmod(file.fileno(), mode)
            # Atomic creation without overwriting any file created after preflight.
            try:
                os.link(temporary, path)
            except FileExistsError as exc:
                raise ProbeError("Native probe destination appeared during install; preserving it: " + str(path)) from exc
        finally:
            temporary.unlink(missing_ok=True)

    def install(self) -> dict:
        # Check both destinations before writing either one.
        self.verify_existing(self.wrapper, self.wrapper_text)
        self.verify_existing(self.manifest, self.manifest_text)
        self.create_owned(self.wrapper, self.wrapper_text, 0o700)
        self.create_owned(self.manifest, self.manifest_text, 0o600)
        return {"action": "installed", "mode": self.mode, "manifest": str(self.manifest),
                "wrapper": str(self.wrapper), "allowed_origins": self.manifest_data["allowed_origins"],
                "qualification": "Synthetic transport probe only. Browser host discovery and transport are unverified; this does not qualify password-manager vendor trust or credentials."}

    def uninstall(self) -> dict:
        targets = [(self.manifest, self.manifest_text), (self.wrapper, self.wrapper_text)]
        present = [(path, text) for path, text in targets if self.verify_existing(path, text)]
        for path, text in present:
            self.verify_existing(path, text)
            path.unlink()
        return {"action": "uninstalled", "mode": self.mode, "removed": [str(path) for path, _ in present]}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("host", help="Run the bounded framed-JSON ping host")
    for name in ("install", "uninstall"):
        command = commands.add_parser(name)
        modes = command.add_mutually_exclusive_group(required=True)
        modes.add_argument("--baseline", action="store_true")
        modes.add_argument("--development", action="store_true")
        command.add_argument("--extension-id", required=True)
    args = parser.parse_args(argv)
    if args.command == "host":
        return host_main(sys.stdin.buffer, sys.stdout.buffer, sys.stderr)
    try:
        installer = Installer(Path(__file__).resolve().parent.parent,
                              "baseline" if args.baseline else "development", args.extension_id)
        print(json.dumps(getattr(installer, args.command)(), indent=2))
        return 0
    except (ProbeError, OSError) as exc:
        print("native-probe: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
