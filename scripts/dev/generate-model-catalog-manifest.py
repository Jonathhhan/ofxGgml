#!/usr/bin/env python3
import argparse
import base64
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a signed manifest for scripts/model-catalog.json")
    parser.add_argument("catalog", nargs="?", default="scripts/model-catalog.json")
    parser.add_argument("--manifest", default="scripts/model-catalog.manifest.json")
    parser.add_argument("--signature", default="scripts/model-catalog.manifest.sig")
    parser.add_argument("--private-key", help="Optional PEM private key for openssl signing")
    parser.add_argument("--signing-key-id", default="ofxggml-model-catalog-rsa-2026-05")
    args = parser.parse_args()

    catalog_path = Path(args.catalog)
    manifest_path = Path(args.manifest)
    signature_path = Path(args.signature)

    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    models = catalog.get("models", [])
    official_count = sum(
        1
        for model in models
        if str(model.get("provenance", {}).get("source_type", "")).strip().lower() == "official"
    )
    manifest = {
        "manifest_version": 1,
        "catalog_path": str(catalog_path).replace('\\', '/'),
        "catalog_sha256": sha256_file(catalog_path),
        "generated_at": utc_now(),
        "signing_key_id": args.signing_key_id,
        "entry_count": len(models),
        "official_entry_count": official_count,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    if args.private_key:
        command = [
            "openssl",
            "dgst",
            "-sha256",
            "-sign",
            args.private_key,
            "-out",
            str(signature_path),
            str(manifest_path),
        ]
        subprocess.run(command, check=True)
        print(f"Generated manifest signature: {signature_path}")
    else:
        print("Manifest written without signature (no --private-key provided)")

    print(f"Manifest written: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
