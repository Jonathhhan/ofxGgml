#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
validate_ps1="$script_dir/validate-local.ps1"

if command -v pwsh >/dev/null 2>&1; then
	pwsh -NoProfile -File "$validate_ps1" "$@"
elif command -v powershell >/dev/null 2>&1; then
	powershell -NoProfile -File "$validate_ps1" "$@"
else
	echo "PowerShell 7+ is required to run validate-local.ps1" >&2
	exit 1
fi
