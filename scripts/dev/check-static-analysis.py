#!/usr/bin/env python3
import argparse
import re
import sys
from pathlib import Path

CPP_RE = re.compile(r"^(?P<file>.+):(?P<line>\d+):(?P<column>\d+): (?P<severity>\w+): (?P<message>.*) \[(?P<check>[^\]]+)\]$")
CLANG_RE = re.compile(r"^(?P<file>.+):(?P<line>\d+):(?P<column>\d+): (?P<severity>warning|error): (?P<message>.*) \[(?P<check>[^\]]+)\]$")
BLOCKING_CLANG_PREFIXES = (
    "clang-analyzer-",
    "bugprone-",
    "cert-",
    "security-",
    "cppcoreguidelines-pro-bounds-",
    "cppcoreguidelines-owning-memory",
)
BLOCKING_CPP_SEVERITIES = {"error", "warning"}


def parse_lines(path: Path) -> set[str]:
    if not path.exists():
        return set()
    return {line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()}


def is_blocking(tool: str, line: str) -> bool:
    regex = CPP_RE if tool == "cppcheck" else CLANG_RE
    match = regex.match(line)
    if not match:
        return False
    severity = match.group("severity").lower()
    check = match.group("check")
    if tool == "cppcheck":
        return severity in BLOCKING_CPP_SEVERITIES
    if severity == "error":
        return True
    return check.startswith(BLOCKING_CLANG_PREFIXES)


def main() -> int:
    parser = argparse.ArgumentParser(description="Filter static-analysis findings into blocking vs informational changes")
    parser.add_argument("--tool", required=True, choices=["cppcheck", "clang-tidy"])
    parser.add_argument("--findings", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--output-new", required=True)
    parser.add_argument("--output-blocking", required=True)
    args = parser.parse_args()

    findings = parse_lines(Path(args.findings))
    baseline = parse_lines(Path(args.baseline))
    new_findings = sorted(findings - baseline)
    blocking = [line for line in new_findings if is_blocking(args.tool, line)]

    Path(args.output_new).write_text("\n".join(new_findings) + ("\n" if new_findings else ""), encoding="utf-8")
    Path(args.output_blocking).write_text("\n".join(blocking) + ("\n" if blocking else ""), encoding="utf-8")

    print(f"{args.tool}: {len(findings)} total findings, {len(new_findings)} new, {len(blocking)} blocking")
    if new_findings and not blocking:
        print(f"{args.tool}: only non-blocking findings were added")
    if blocking:
        print(f"{args.tool}: blocking findings detected:")
        for line in blocking:
            print(f"  {line}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
