"""Create an immutable ZIP variant while replacing selected members."""

from __future__ import annotations

import argparse
from pathlib import Path
import zipfile


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "replacement", nargs="+", help="archive/member=local/file"
    )
    args = parser.parse_args()

    replacements: dict[str, Path] = {}
    for value in args.replacement:
        member, separator, filename = value.partition("=")
        if not separator:
            raise SystemExit(f"invalid replacement: {value}")
        replacements[member.replace("\\", "/")] = Path(filename)

    seen: set[str] = set()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.source) as source, zipfile.ZipFile(
        args.output, "w", allowZip64=True
    ) as output:
        for info in source.infolist():
            data = replacements.get(info.filename, None)
            if data is not None:
                payload = data.read_bytes()
                seen.add(info.filename)
            else:
                payload = source.read(info)
            output.writestr(info, payload)

    missing = set(replacements) - seen
    if missing:
        args.output.unlink(missing_ok=True)
        raise SystemExit(f"members not found: {sorted(missing)}")

    with zipfile.ZipFile(args.output) as archive:
        bad = archive.testzip()
        if bad:
            raise SystemExit(f"bad ZIP member: {bad}")
        print(f"entries={len(archive.infolist())} replacements={len(seen)}")


if __name__ == "__main__":
    main()
