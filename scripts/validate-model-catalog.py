#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path

SHA256_RE = re.compile(r"^[a-f0-9]{64}$")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate scripts/model-catalog.json")
    parser.add_argument("path", nargs="?", default="scripts/model-catalog.json")
    parser.add_argument(
        "--require-official-checksums",
        action="store_true",
        help="Fail when an official model is missing sha256",
    )
    args = parser.parse_args()

    catalog_path = Path(args.path)
    data = json.loads(catalog_path.read_text(encoding="utf-8"))
    models = data.get("models", [])

    errors: list[str] = []
    warnings: list[str] = []

    for idx, model in enumerate(models, start=1):
        preset = model.get("preset", idx)
        name = model.get("name", f"model-{idx}")
        sha256 = str(model.get("sha256", "")).strip().lower()
        checksum_optional = bool(model.get("checksum_optional", False))
        official = not checksum_optional

        if sha256 and not SHA256_RE.fullmatch(sha256):
            errors.append(f"preset {preset} ({name}): sha256 must be 64 lowercase hex characters")
            continue

        if not sha256:
            msg = f"preset {preset} ({name}): sha256 missing"
            if args.require_official_checksums and official:
                errors.append(msg)
            else:
                warnings.append(msg)

    if warnings:
        print("Model catalog warnings:")
        for w in warnings:
            print(f"  - {w}")

    if errors:
        print("Model catalog validation failed:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    print("Model catalog validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
