"""Clone a ZIP while replacing exactly one member and preserving its metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("member")
    parser.add_argument("replacement", type=Path)
    args = parser.parse_args()

    if args.destination.exists():
        raise FileExistsError(args.destination)
    replacement = args.replacement.read_bytes()
    replaced = 0
    with zipfile.ZipFile(args.source, "r") as source, zipfile.ZipFile(
        args.destination, "w", allowZip64=True
    ) as destination:
        for info in source.infolist():
            data = replacement if info.filename == args.member else source.read(info)
            if info.filename == args.member:
                replaced += 1
            destination.writestr(info, data)
    if replaced != 1:
        args.destination.unlink(missing_ok=True)
        raise RuntimeError(f"expected one member, replaced {replaced}: {args.member}")

    digest = hashlib.sha256(args.destination.read_bytes()).hexdigest().upper()
    print(
        json.dumps(
            {
                "path": str(args.destination.resolve()),
                "size": args.destination.stat().st_size,
                "sha256": digest,
                "member": args.member,
                "replacement_size": len(replacement),
            },
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
