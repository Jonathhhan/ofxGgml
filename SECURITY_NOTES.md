# Security Enhancement Notes

This document describes security improvements implemented in ofxGgml.

## Model Checksums

The `scripts/model-catalog.json` file contains SHA256 checksums for model verification. These are placeholder values that should be replaced with actual checksums:

### To Update Checksums

1. Download a model using the download script
2. Compute the SHA256 checksum:
   ```bash
   sha256sum path/to/model.gguf
   # or on macOS:
   shasum -a 256 path/to/model.gguf
   ```
3. Update the corresponding entry in `scripts/model-catalog.json`
4. The download script will automatically verify the checksum on future downloads

### Current Status

Model presets currently require maintainer-supplied SHA256 values from model publishers. Use `./scripts/update-model-checksums.sh` to populate them and `python3 scripts/validate-model-catalog.py` to validate catalog checksum formatting and completeness.

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

### 4. Model Integrity (Critical - Framework Complete)
- SHA256 checksum support in model catalog
- Automatic verification in download script
- Warning messages when checksums are missing
- **Action Required**: Replace placeholder checksums with actual values

## Recommendations for Future Work

### High Priority
1. Obtain and add real SHA256 checksums for model presets
2. Add sandboxing options for llama-cli execution
3. Add rate limiting for inference requests
4. Implement model signature verification (GPG/code signing)

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
