"""Convert Tiny Core TCZ packages into a BoxedWine-compatible ZIP overlay.

7-Zip can read SquashFS TCZ archives on Windows but cannot create Unix
symlinks without elevated privileges.  It does preserve each symlink's target
as file data during normal extraction, so this script restores the original
Unix mode from `7z l -slt` when producing the ZIP consumed by BoxedWine.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import stat
import subprocess
import time
import zipfile


def metadata(seven_zip: Path, package: Path) -> dict[str, str]:
    result = subprocess.run(
        [str(seven_zip), "l", "-slt", str(package)],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    modes: dict[str, str] = {}
    fields: dict[str, str] = {}
    for line in result.stdout.splitlines() + [""]:
        if not line:
            path = fields.get("Path")
            mode = fields.get("Mode")
            if path and mode and path != str(package):
                modes[path.replace("\\", "/")] = mode
            fields = {}
        elif " = " in line:
            key, value = line.split(" = ", 1)
            fields[key] = value
    return modes


def extract_symlink_target(seven_zip: Path, package: Path, name: str) -> bytes:
    """Read a SquashFS symlink payload without asking Windows to create it."""
    result = subprocess.run(
        [str(seven_zip), "e", "-so", str(package), name.replace("/", "\\")],
        check=True,
        stdout=subprocess.PIPE,
    )
    return result.stdout


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("package_dir", type=Path)
    parser.add_argument("extract_dir", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--seven-zip", type=Path, default=Path(r"C:\Program Files\7-Zip\7z.exe")
    )
    args = parser.parse_args()

    args.extract_dir.mkdir(parents=True, exist_ok=True)
    modes: dict[str, str] = {}
    symlink_payloads: dict[str, bytes] = {}
    packages = sorted(args.package_dir.glob("*.tcz"))
    if not packages:
        raise SystemExit("no TCZ packages found")

    for package in packages:
        extracted = subprocess.run(
            [str(args.seven_zip), "x", str(package), f"-o{args.extract_dir}", "-y"],
            check=False,
            stdout=subprocess.DEVNULL,
        )
        if extracted.returncode not in (0, 2):
            raise SystemExit(f"7-Zip extraction failed for {package}: {extracted.returncode}")
        package_modes = metadata(args.seven_zip, package)
        modes.update(package_modes)
        for name, mode in package_modes.items():
            if mode.startswith("l"):
                symlink_payloads[name] = extract_symlink_target(
                    args.seven_zip, package, name
                )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", allowZip64=True) as archive:
        for path in sorted(args.extract_dir.rglob("*")):
            if not path.is_file():
                continue
            name = path.relative_to(args.extract_dir).as_posix()
            mode = modes.get(name, "-rw-r--r--")
            info = zipfile.ZipInfo(name, time.localtime(path.stat().st_mtime)[:6])
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            if mode.startswith("l"):
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                payload = symlink_payloads[name]
            else:
                permissions = 0o755 if "x" in mode else 0o644
                info.external_attr = (stat.S_IFREG | permissions) << 16
                payload = path.read_bytes()
            archive.writestr(info, payload)

    with zipfile.ZipFile(args.output) as archive:
        bad = archive.testzip()
        if bad:
            raise SystemExit(f"bad ZIP member: {bad}")
        symlinks = sum(
            1
            for info in archive.infolist()
            if stat.S_ISLNK(info.external_attr >> 16)
        )
        print(f"packages={len(packages)} files={len(archive.infolist())} symlinks={symlinks}")


if __name__ == "__main__":
    main()
