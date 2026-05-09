#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_ps1="$script_dir/build-llama-server.ps1"

if command -v pwsh >/dev/null 2>&1; then
	exec pwsh -NoProfile -File "$build_ps1" "$@"
fi

if command -v powershell >/dev/null 2>&1; then
	exec powershell -NoProfile -File "$build_ps1" "$@"
fi

printf '%s\n' "Could not find pwsh or powershell."
printf '%s\n' "Install PowerShell 7+, then rerun this script."
exit 1
