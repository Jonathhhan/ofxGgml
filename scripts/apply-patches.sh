#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# apply-patches.sh — Apply local patches to the bundled ggml source.
#
# Applies source-level fixups that are needed for cross-platform
# compatibility but have not yet been merged upstream.  This script is
# called automatically by update-ggml-source.sh after refreshing the
# bundled copy, but it can also be run standalone.
#
# Usage:
#   ./scripts/apply-patches.sh
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GGML_DIR="$ADDON_ROOT/libs/ggml"

# Portable sed -i: macOS requires a backup extension argument.
sedi() {
	if [[ "$(uname)" == "Darwin" ]]; then
		sed -i '' "$@"
	else
		sed -i "$@"
	fi
}

write_step() {
	printf '==> %s\n' "$1"
}

write_ok() {
	printf '  ✓ %s\n' "$1"
}

write_skip() {
	printf '  - %s (not applicable)\n' "$1"
}

# ---------------------------------------------------------------------------
# Patch: Initialize nv_bfloat162 tmpx_gate in mmvf.cu
#
# MSVC emits C4101 ("declared but never referenced") for the
# nv_bfloat162 tmpx_gate variable which is only used inside
# if constexpr (has_fusion) blocks.  Initialize it to silence the
# warning, consistent with all other type branches (float2, half2, int).
# ---------------------------------------------------------------------------
apply_mmvf_tmpx_gate_init() {
	local file="$GGML_DIR/src/ggml-cuda/mmvf.cu"
	local desc="mmvf.cu: initialize nv_bfloat162 tmpx_gate (MSVC C4101)"

	if [[ ! -f "$file" ]]; then
		write_skip "$desc"
		return
	fi

	if grep -q 'nv_bfloat162 tmpx_gate;' "$file"; then
		sedi 's/\([[:space:]]*\)nv_bfloat162 tmpx_gate;/\1nv_bfloat162 tmpx_gate = {0.0f, 0.0f};/' "$file"
		write_ok "$desc"
	else
		write_skip "$desc"
	fi
}

# ---------------------------------------------------------------------------
# Patch: Suppress NVCC warning #221-D in common.cuh
#
# On Windows MSVC defines the INFINITY macro as ((float)(1e+300)).
# NVCC warns (#221-D, "floating-point value does not fit in required
# floating-point type") because the intermediate double 1e+300 overflows
# when cast to float.  The overflow is intentional — it produces IEEE 754
# infinity.  Add a pragma to suppress the warning at the top of common.cuh,
# which is transitively included by every CUDA source file.
# ---------------------------------------------------------------------------
apply_cuda_infinity_warning_suppress() {
	local file="$GGML_DIR/src/ggml-cuda/common.cuh"
	local desc="common.cuh: suppress NVCC warning #221-D (MSVC INFINITY)"

	if [[ ! -f "$file" ]]; then
		write_skip "$desc"
		return
	fi

	if grep -q 'nv_diag_suppress 221' "$file"; then
		write_skip "$desc"
		return
	fi

	# Insert the pragma block right after #pragma once
	sedi '/^#pragma once$/a\
\
// Suppress NVCC warning #221-D: "floating-point value does not fit in required\
// floating-point type" triggered by MSVC'\''s INFINITY macro ((float)(1e+300)).\
// The double-to-float overflow is intentional — it produces IEEE 754 infinity.\
#if defined(__CUDACC__)\
#pragma nv_diag_suppress 221\
#endif' "$file"
	write_ok "$desc"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

write_step "Applying local patches to bundled ggml source..."

apply_mmvf_tmpx_gate_init
apply_cuda_infinity_warning_suppress

write_step "Patches applied."
