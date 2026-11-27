#!/usr/bin/env python3
"""Package our SDK sources and portable research, never console dumps or keys."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

SDK = Path(__file__).resolve().parent
ROOT = SDK.parents[1]
VERSION = "0.1.0"
LINK = re.compile(r"(\[[^\]]*\]\()([^\s)#]+)(#[^\s)]*)?(\))")


def allowed_reference(path):
    relative = path.relative_to(ROOT).as_posix()
    return path.suffix in {".md", ".json"} and (
        relative.startswith("research/gameshare-probe/notes/")
        or relative.startswith("research/gameshare-probe/records/")
        or relative.startswith("research/gameshare-host/records/")
        or (path.parent == ROOT / "research/gameshare-probe" and path.suffix == ".md")
        or relative in {
            "research/gameshare-probe/START_HERE.md",
            "research/gameshare-probe/HOST_BRINGUP.md",
            "research/gameshare-probe/SDK_EXTRACTION.md",
            "research/gameshare-host/RESEARCH.md", "research/gameshare-host/README.md", "docs/SWITCH2.md",
        }
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--library", type=Path, help="Optional SDK-only libgameshare.a; no linked runtime")
    args = parser.parse_args()
    files = {}
    for path in sorted(SDK.rglob("*")):
        if not path.is_file() or any(part in {"__pycache__", "build", "dist", "evidence", ".git"} for part in path.parts):
            continue
        if path.suffix not in {".c", ".cpp", ".hpp", ".h", ".py", ".md", ".txt", ".cmake", ".in"} and path.name != "LICENSE":
            continue
        files[path.relative_to(SDK).as_posix()] = path.read_bytes()
    queue = [(SDK / name, name) for name in files if name.endswith(".md")]
    visited = set()
    while queue:
        source, packaged = queue.pop()
        if source in visited:
            continue
        visited.add(source)

        def replace(match):
            link = match[2]
            if ":" in link or link.startswith(("/", "#")):
                return match[0]
            target = (source.parent / link).resolve()
            if not target.is_file() or not target.is_relative_to(ROOT):
                return match[0]
            if target.is_relative_to(SDK):
                archive = target.relative_to(SDK).as_posix()
            elif allowed_reference(target):
                archive = "reference/" + target.relative_to(ROOT).as_posix()
                if archive not in files:
                    files[archive] = target.read_bytes()
                    if target.suffix == ".md":
                        queue.append((target, archive))
            else:
                # No private artifact is copied into a redistributable package.
                label = match[1][1:-2]
                return f"{label} (source checkout: `{target.relative_to(ROOT).as_posix()}`)"
            import posixpath
            rewritten = posixpath.relpath(archive, posixpath.dirname(packaged))
            return match[1] + rewritten + (match[3] or "") + match[4]

        files[packaged] = LINK.sub(replace, files[packaged].decode()).encode()
    if args.library:
        files["lib/libgameshare.a"] = args.library.read_bytes()
    files["PACKAGE.json"] = json.dumps({
        "version": VERSION,
        "files": {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())},
        "contains_nintendo_code_or_keys": False,
        "device_qualification": "See docs/QUALIFICATION.md for measured results and remaining checks",
    }, indent=2).encode() + b"\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as package:
        for name, data in sorted(files.items()):
            package.writestr(f"gameshare-sdk-{VERSION}/{name}", data)
    print(f"Packaged {len(files)} files: {args.output}")


if __name__ == "__main__":
    main()
