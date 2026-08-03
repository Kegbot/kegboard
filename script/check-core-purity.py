#!/usr/bin/env python3
"""Enforce the framework-agnostic boundary of the kegboard core.

The core files listed in components/kegboard/CORE.md must depend only on the
C++ standard library. If an ESPHome (or Arduino, or ESP-IDF) header ever
creeps in, the host unit tests stop building and the escape hatch to a
non-ESPHome framework quietly closes -- so this runs in CI.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

REPO_ROOT = Path(__file__).resolve().parent.parent
CORE_DIR = REPO_ROOT / "components" / "kegboard"

# Files that make up the framework-agnostic core. Everything else in the
# component directory is the ESPHome adapter layer and may include freely.
CORE_FILES = [
    "pour_session.h",
    "pour_session.cpp",
    "tick_series.h",
    "tick_series.cpp",
    "kegbot_request.h",
    "kegbot_request.cpp",
    "ring_queue.h",
    "auth_session.h",
    "auth_session.cpp",
]

FORBIDDEN = re.compile(
    r'^\s*#\s*include\s*[<"]('
    r"esphome/|Arduino\.h|esp_|freertos/|driver/|nvs|sdkconfig"
    r")",
    re.IGNORECASE,
)

# Standard library headers are unrestricted; sibling core headers are listed
# explicitly so a typo cannot silently pull in an adapter header.
ALLOWED_LOCAL = set(CORE_FILES)

LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


def main() -> int:
    errors: list[str] = []

    for name in CORE_FILES:
        path = CORE_DIR / name
        if not path.is_file():
            errors.append(f"{name}: listed in CORE_FILES but missing from {CORE_DIR}")
            continue

        for lineno, line in enumerate(path.read_text().splitlines(), start=1):
            if FORBIDDEN.match(line):
                errors.append(
                    f"{name}:{lineno}: core must not include framework headers: "
                    f"{line.strip()}"
                )
                continue

            match = LOCAL_INCLUDE.match(line)
            if match and match.group(1) not in ALLOWED_LOCAL:
                errors.append(
                    f"{name}:{lineno}: core may only include other core headers: "
                    f"{line.strip()}"
                )

    if errors:
        print("Core purity check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        print(
            "\nSee components/kegboard/CORE.md for what this boundary is for.",
            file=sys.stderr,
        )
        return 1

    print(f"Core purity check passed ({len(CORE_FILES)} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
