#!/usr/bin/env python3
"""Validate the firmware's serialized output against the protocol schemas.

Drives the compiled test_events binary in emit mode to produce one batch per
event type (plus a combined batch), then validates every document against
schemas/kegboard-event.schema.json. Also asserts the schema files match the
normative appendices in docs/kegboard-event-protocol.md, so the doc, the
schema files, and the code cannot drift apart silently.

Requires: the host test binaries already built (make -C tests/core) and the
`jsonschema` package.
"""

from __future__ import annotations

import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile

import jsonschema

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_DIR = REPO_ROOT / "schemas"
DOC = REPO_ROOT / "docs" / "kegboard-event-protocol.md"
HOST_TAG = f"{platform.system()}-{platform.machine()}"
BINARY = REPO_ROOT / "tests" / "core" / "build" / HOST_TAG / "test_events"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def check_doc_matches_schemas() -> None:
    blocks = re.findall(r"```json\n(.*?)```", DOC.read_text(), re.S)
    doc_request, doc_response = json.loads(blocks[-2]), json.loads(blocks[-1])

    for name, doc_schema in [
        ("kegboard-event.schema.json", doc_request),
        ("kegboard-event-response.schema.json", doc_response),
    ]:
        file_schema = json.loads((SCHEMA_DIR / name).read_text())
        if file_schema != doc_schema:
            fail(f"{name} does not match the appendix in {DOC.name}")
    print("doc appendices match schema files")


def check_emitted_documents() -> None:
    if not BINARY.is_file():
        fail(f"{BINARY} not built; run `make -C tests/core` first")

    schema = json.loads((SCHEMA_DIR / "kegboard-event.schema.json").read_text())
    jsonschema.Draft202012Validator.check_schema(schema)
    validator = jsonschema.Draft202012Validator(schema)

    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([str(BINARY), tmp], check=True)
        samples = sorted(Path(tmp).glob("*.json"))
        if len(samples) < 5:
            fail(f"expected several emitted samples, found {len(samples)}")

        for path in samples:
            document = json.loads(path.read_text())
            errors = list(validator.iter_errors(document))
            if errors:
                for error in errors:
                    print(f"  {path.name}: {error.message}", file=sys.stderr)
                fail(f"{path.name} does not validate")
            print(f"{path.name}: valid")


def main() -> int:
    check_doc_matches_schemas()
    check_emitted_documents()
    print("All emitted documents validate against the schema.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
