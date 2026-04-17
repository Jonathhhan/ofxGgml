This directory stores normalized static-analysis findings used by CI to enforce a fail-on-new-issues policy.

Update workflow:

1. Run static analysis locally and capture normalized findings:
   - `cppcheck-findings.txt`
   - `clang-tidy-findings.txt`
2. Replace the baseline files in this directory with the normalized outputs.
3. Commit baseline updates only when intentionally accepting existing findings.

CI behavior:

- Findings present in baseline files are allowed.
- Any finding not present in baseline files fails CI.
