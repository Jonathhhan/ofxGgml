# Security Enhancement Notes

This document describes security improvements implemented in ofxGgml.

## Model Catalog Integrity

The `scripts/model-catalog.json` file contains SHA256 checksums, provenance metadata, and per-entry update timestamps for model verification.

### To Update Checksums (maintainer workflow)

1. Download or locate the exact model file referenced in the catalog entry.
2. Compute the SHA256 checksum:
   ```bash
   sha256sum path/to/model.gguf
   # or on macOS:
   shasum -a 256 path/to/model.gguf
   ```
3. Update the corresponding `sha256` field and provenance timestamps in `scripts/model-catalog.json`.
4. Regenerate the signed manifest:
   ```bash
   python3 scripts/dev/generate-model-catalog-manifest.py \
     scripts/model-catalog.json \
     --manifest scripts/model-catalog.manifest.json \
     --signature scripts/model-catalog.manifest.sig \
     --private-key /path/to/model-catalog-signing.pem
   ```
5. Validate with:
   ```bash
   python3 scripts/dev/validate-model-catalog.py \
     --require-official-checksums \
     --require-signature \
     scripts/model-catalog.json
   ```
6. Commit the updated catalog, manifest, signature, and public key metadata.

## Security Features Implemented

### 1. Path Validation (High Priority - Completed)
- All model paths are validated before use
- Executable paths are checked for existence and suspicious patterns
- Path traversal attempts (`..`) are blocked
- Null byte injection is prevented

### 2. Input Sanitization (High Priority - Completed)
- User prompts are sanitized to remove control characters
- Command arguments are sanitized before execution
- JSON schemas are sanitized before use

### 3. Secure Temp Files (High Priority - Completed)
- Atomic file creation using exclusive open flags
- Cryptographically random filenames
- Automatic cleanup via RAII

### 4. Model Integrity (Critical - Enforced)
- SHA256 checksum support in model catalog
- Provenance metadata and catalog update timestamps
- Signed `model-catalog.manifest.json` verification via `openssl`
- Automatic verification in download script
- CI validation requires official checksums for all official catalog entries
- Community/custom entries can remain unsigned only when explicitly marked through provenance metadata and validation warnings

### 5. Assistant Tool Guardrails (High Priority - Enforced)
- Tool policy profiles (`balanced`, `read-only`, `workspace-safe`, `strict`) constrain patching, verification, and grounding behavior
- Build-mode coding agents default to `workspace-safe`
- Plan-mode coding agents automatically downgrade to a read-only tool policy

## Recommendations for Future Work

### High Priority
1. Obtain and add real SHA256 checksums for remaining community model presets
2. Add sandboxing options for llama-cli execution
3. Add rate limiting for inference requests
4. Rotate the model catalog signing key into a maintainer-managed secret workflow

### Medium Priority
1. Add file size limits for models
2. Implement connection timeouts for model downloads
3. Add integrity checks for cached models
4. Create security audit logging

### Low Priority
1. Add HTTPS certificate pinning for model downloads
2. Implement model allowlist/denylist
3. Add security headers for any HTTP endpoints
4. Create security policy documentation

## Testing

Security features should be tested with:
- Invalid file paths (non-existent, special files, path traversal)
- Malicious prompts (null bytes, control characters, extremely long inputs)
- Corrupted model files (wrong checksums)
- Invalid executables (missing, wrong permissions, malicious paths)

## Reporting Security Issues

Security issues should be reported privately to the repository maintainers before public disclosure.
