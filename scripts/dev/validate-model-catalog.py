#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

SHA256_RE = re.compile(r"^[a-f0-9]{64}$")
TIMESTAMP_RE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
DEFAULT_MANIFEST = Path("scripts/model-catalog.manifest.json")
DEFAULT_SIGNATURE = Path("scripts/model-catalog.manifest.sig")
DEFAULT_PUBLIC_KEY = Path("scripts/keys/model-catalog-signing.pub.pem")


def is_timestamp(value: str) -> bool:
    return bool(TIMESTAMP_RE.fullmatch(value.strip()))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_manifest_signature(manifest_path: Path, signature_path: Path, public_key_path: Path) -> tuple[bool, str]:
    if not public_key_path.exists():
        return False, f"public key not found: {public_key_path}"
    if not signature_path.exists():
        return False, f"signature not found: {signature_path}"

    command = [
        "openssl",
        "dgst",
        "-sha256",
        "-verify",
        str(public_key_path),
        "-signature",
        str(signature_path),
        str(manifest_path),
    ]
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        return False, "openssl executable not found"

    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "signature verification failed").strip()
        return False, detail
    return True, (completed.stdout or "signature verified").strip()


def validate_manifest(
    catalog_path: Path,
    models: list[dict[str, Any]],
    manifest_path: Path,
    signature_path: Path | None,
    public_key_path: Path | None,
) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []

    if not manifest_path.exists():
        errors.append(f"manifest not found: {manifest_path}")
        return errors, warnings

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as exc:  # pragma: no cover - defensive error reporting
        errors.append(f"failed to parse manifest: {exc}")
        return errors, warnings

    required_fields = [
        "manifest_version",
        "catalog_path",
        "catalog_sha256",
        "generated_at",
        "signing_key_id",
        "entry_count",
    ]
    for field in required_fields:
        if field not in manifest:
            errors.append(f"manifest missing required field: {field}")

    catalog_sha256 = str(manifest.get("catalog_sha256", "")).strip().lower()
    if catalog_sha256 and not SHA256_RE.fullmatch(catalog_sha256):
        errors.append("manifest catalog_sha256 must be 64 lowercase hex characters")
    if catalog_sha256 and catalog_sha256 != sha256_file(catalog_path):
        errors.append("manifest catalog_sha256 does not match the current catalog file")

    generated_at = str(manifest.get("generated_at", "")).strip()
    if generated_at and not is_timestamp(generated_at):
        errors.append("manifest generated_at must use UTC ISO-8601 format (YYYY-MM-DDTHH:MM:SSZ)")

    catalog_path_value = str(manifest.get("catalog_path", "")).strip()
    if catalog_path_value and Path(catalog_path_value).name != catalog_path.name:
        errors.append("manifest catalog_path does not reference the validated catalog file")

    entry_count = manifest.get("entry_count")
    if isinstance(entry_count, int):
        if entry_count != len(models):
            errors.append(
                f"manifest entry_count {entry_count} does not match catalog model count {len(models)}"
            )
    else:
        errors.append("manifest entry_count must be an integer")

    official_entries = sum(
        1
        for model in models
        if str(model.get("provenance", {}).get("source_type", "")).strip().lower() == "official"
    )
    official_count = manifest.get("official_entry_count")
    if official_count is not None:
        if not isinstance(official_count, int):
            errors.append("manifest official_entry_count must be an integer when present")
        elif official_count != official_entries:
            errors.append(
                f"manifest official_entry_count {official_count} does not match catalog official model count {official_entries}"
            )

    if signature_path and public_key_path:
        ok, detail = verify_manifest_signature(manifest_path, signature_path, public_key_path)
        if not ok:
            errors.append(f"manifest signature verification failed: {detail}")
        else:
            warnings.append(f"manifest signature: {detail}")

    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate scripts/model-catalog.json")
    parser.add_argument("path", nargs="?", default="scripts/model-catalog.json")
    parser.add_argument(
        "--require-official-checksums",
        action="store_true",
        help="Fail when an official model is missing sha256",
    )
    parser.add_argument(
        "--manifest",
        default=str(DEFAULT_MANIFEST),
        help="Path to the signed model-catalog manifest JSON",
    )
    parser.add_argument(
        "--signature",
        default=str(DEFAULT_SIGNATURE),
        help="Path to the manifest signature file",
    )
    parser.add_argument(
        "--public-key",
        default=str(DEFAULT_PUBLIC_KEY),
        help="Path to the manifest signing public key",
    )
    parser.add_argument(
        "--require-signature",
        action="store_true",
        help="Fail when the manifest or signature validation is missing or invalid",
    )
    args = parser.parse_args()

    catalog_path = Path(args.path)
    data = json.loads(catalog_path.read_text(encoding="utf-8"))
    models = data.get("models", [])

    errors: list[str] = []
    warnings: list[str] = []

    catalog_updated_at = str(data.get("catalog_updated_at", "")).strip()
    if not catalog_updated_at:
        errors.append("catalog_updated_at missing")
    elif not is_timestamp(catalog_updated_at):
        errors.append("catalog_updated_at must use UTC ISO-8601 format (YYYY-MM-DDTHH:MM:SSZ)")

    catalog_version = data.get("catalog_version")
    if catalog_version is None:
        warnings.append("catalog_version missing")
    elif not isinstance(catalog_version, int):
        errors.append("catalog_version must be an integer")

    for idx, model in enumerate(models, start=1):
        preset = model.get("preset", idx)
        name = model.get("name", f"model-{idx}")
        sha256 = str(model.get("sha256", "")).strip().lower()
        provenance = model.get("provenance") or {}
        if not isinstance(provenance, dict):
            errors.append(f"preset {preset} ({name}): provenance must be an object")
            continue

        publisher = str(provenance.get("publisher", "")).strip()
        source_type = str(provenance.get("source_type", "")).strip().lower()
        source_url = str(provenance.get("source_url", "")).strip()
        checksum_verified_at = str(provenance.get("checksum_verified_at", "")).strip()
        entry_updated_at = str(provenance.get("catalog_updated_at", "")).strip()
        verification_status = str(provenance.get("verification_status", "")).strip().lower()
        official = source_type == "official"

        if not publisher:
            errors.append(f"preset {preset} ({name}): provenance.publisher missing")
        if source_type not in {"official", "community", "custom"}:
            errors.append(
                f"preset {preset} ({name}): provenance.source_type must be official, community, or custom"
            )
        if not source_url:
            errors.append(f"preset {preset} ({name}): provenance.source_url missing")
        if not entry_updated_at:
            errors.append(f"preset {preset} ({name}): provenance.catalog_updated_at missing")
        elif not is_timestamp(entry_updated_at):
            errors.append(
                f"preset {preset} ({name}): provenance.catalog_updated_at must use UTC ISO-8601 format"
            )
        if checksum_verified_at and not is_timestamp(checksum_verified_at):
            errors.append(
                f"preset {preset} ({name}): provenance.checksum_verified_at must use UTC ISO-8601 format"
            )
        if verification_status not in {"verified-sha256", "checksum-pending"}:
            errors.append(
                f"preset {preset} ({name}): provenance.verification_status must be verified-sha256 or checksum-pending"
            )

        if sha256 and not SHA256_RE.fullmatch(sha256):
            errors.append(f"preset {preset} ({name}): sha256 must be 64 lowercase hex characters")
            continue

        if not sha256:
            msg = f"preset {preset} ({name}): sha256 missing"
            if verification_status != "checksum-pending":
                errors.append(
                    f"preset {preset} ({name}): provenance.verification_status must be checksum-pending when sha256 is missing"
                )
            if args.require_official_checksums and official:
                errors.append(msg)
            else:
                warnings.append(msg)
        elif verification_status != "verified-sha256":
            errors.append(
                f"preset {preset} ({name}): provenance.verification_status must be verified-sha256 when sha256 is populated"
            )
        elif not checksum_verified_at:
            warnings.append(
                f"preset {preset} ({name}): provenance.checksum_verified_at missing for populated sha256"
            )

    manifest_path = Path(args.manifest)
    signature_path = Path(args.signature) if args.signature else None
    public_key_path = Path(args.public_key) if args.public_key else None
    should_validate_signature = args.require_signature or manifest_path.exists()
    if should_validate_signature:
        manifest_errors, manifest_warnings = validate_manifest(
            catalog_path,
            models,
            manifest_path,
            signature_path if args.require_signature or (signature_path and signature_path.exists()) else None,
            public_key_path if args.require_signature or (public_key_path and public_key_path.exists()) else None,
        )
        errors.extend(manifest_errors)
        warnings.extend(manifest_warnings)

    if warnings:
        print("Model catalog warnings:")
        for warning in warnings:
            print(f"  - {warning}")

    if errors:
        print("Model catalog validation failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("Model catalog validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
