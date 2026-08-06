#!/usr/bin/env python3
"""Cut a release: bump the version, finalize the changelog, commit, and tag.

The canonical version lives in components/kegboard/kegboard.cpp
(KEGBOARD_VERSION). Two mirrors must always agree with it and are updated in
lockstep: docs/conf.py (Sphinx `release`) and packages/base.yaml
(esphome.project.version, which devices report over the ESPHome API).

Usage:
    script/bump.py            # 4.0.1 -> 4.0.2; 4.0.0-pre1 -> 4.0.0
    script/bump.py 4.1.0      # explicit target version
    script/bump.py --dry-run  # show what would happen, change nothing

By default the next version drops the pre-release suffix if there is one,
otherwise increments the micro version. The tool refuses to run on a dirty
working tree or if the target tag already exists. It does not push; see
docs/developer-notes.md for the full release procedure.
"""

import argparse
import datetime
from pathlib import Path
import re
import subprocess
import sys

REPO = Path(__file__).resolve().parent.parent

# (path, regex with one capture group around the version)
VERSION_FILES = [
    (
        "components/kegboard/kegboard.cpp",
        r'const char \*const KEGBOARD_VERSION = "([^"]+)";',
    ),
    ("docs/conf.py", r'release = "([^"]+)"'),
    ("packages/base.yaml", r"    version: (\S+)"),
]

CHANGELOG = "docs/changelog.md"
OPEN_HEADER = "## Current version (in development)"

VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.]+))?$")


def die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def git(*args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=REPO, check=True, capture_output=True, text=True
    ).stdout.strip()


def read_current_version() -> str:
    versions = {}
    for relpath, pattern in VERSION_FILES:
        text = (REPO / relpath).read_text()
        m = re.search(pattern, text)
        if not m:
            die(f"could not find version in {relpath}")
        versions[relpath] = m.group(1)
    if len(set(versions.values())) != 1:
        detail = ", ".join(f"{p}={v}" for p, v in versions.items())
        die(f"version files disagree ({detail}); fix them before bumping")
    return next(iter(versions.values()))


def default_next(current: str) -> str:
    m = VERSION_RE.match(current)
    if not m:
        die(f"current version {current!r} is not of the form X.Y.Z[-suffix]")
    major, minor, micro, suffix = m.groups()
    if suffix:
        return f"{major}.{minor}.{micro}"
    return f"{major}.{minor}.{int(micro) + 1}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("version", nargs="?", help="target version (default: auto)")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    current = read_current_version()
    target = args.version or default_next(current)
    if not VERSION_RE.match(target):
        die(f"target version {target!r} is not of the form X.Y.Z[-suffix]")
    if target == current:
        die(f"target version {target} is already the current version")
    tag = f"v{target}"

    if git("tag", "-l", tag):
        die(f"tag {tag} already exists")

    changelog_path = REPO / CHANGELOG
    changelog = changelog_path.read_text()
    if OPEN_HEADER not in changelog:
        die(f"{CHANGELOG} has no '{OPEN_HEADER}' section to finalize")

    today = datetime.date.today().isoformat()
    released_header = f"## {tag} ({today})"

    print(f"{current} -> {target} (tag {tag})")
    if args.dry_run:
        print("dry run; no changes made")
        return

    if git("status", "--porcelain"):
        die("working tree is not clean; commit or stash first")

    for relpath, pattern in VERSION_FILES:
        path = REPO / relpath
        text = path.read_text()
        path.write_text(
            re.sub(
                pattern, lambda m: m.group(0).replace(m.group(1), target), text, count=1
            )
        )

    # Finalize the open section and start a fresh one above it for the next
    # cycle, keepachangelog-style.
    changelog = changelog.replace(OPEN_HEADER, f"{OPEN_HEADER}\n\n{released_header}", 1)
    changelog_path.write_text(changelog)

    git("add", CHANGELOG, *(relpath for relpath, _ in VERSION_FILES))
    git("commit", "-m", f"release: {tag}")
    git("tag", "-a", tag, "-m", f"Kegboard {tag}")

    print(f"committed and tagged {tag}")
    print(f"to publish: git push origin HEAD {tag}")


if __name__ == "__main__":
    main()
