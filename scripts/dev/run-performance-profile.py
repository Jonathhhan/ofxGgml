#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding='utf-8'))


def main() -> int:
    parser = argparse.ArgumentParser(description='Run a deterministic ofxGgml performance profile and compare it to a baseline')
    parser.add_argument('--config', default='scripts/dev/performance-profiles.json')
    parser.add_argument('--profile', required=True)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--write-baseline', action='store_true')
    args = parser.parse_args()

    config = load_json(Path(args.config))
    profiles = config.get('profiles', {})
    if args.profile not in profiles:
        raise SystemExit(f'Unknown performance profile: {args.profile}')
    profile = profiles[args.profile]
    baseline_path = Path(profile['baseline'])
    allow_skip = bool(profile.get('allow_skip', False))

    with tempfile.TemporaryDirectory(prefix='ofxggml-perf-') as tmp_dir:
        output_path = Path(tmp_dir) / f"{args.profile}.json"
        env = os.environ.copy()
        env['OFXGGML_PERF_PROFILE'] = str(profile['perf_profile'])
        env['OFXGGML_PERF_OUTPUT'] = str(output_path)
        completed = subprocess.run(
            [args.binary, profile.get('catch_filter', '[performance_profile]')],
            check=False,
            env=env,
            text=True,
        )
        if completed.returncode != 0:
            return completed.returncode
        result = load_json(output_path)

    if result.get('skipped'):
        print(f"{args.profile}: skipped ({result.get('reason', 'no reason provided')})")
        return 0 if allow_skip else 1

    metric_values = {
        name: float(metric.get('p50_ms', 0.0))
        for name, metric in result.get('metrics', {}).items()
    }
    if args.write_baseline:
        baseline_path.parent.mkdir(parents=True, exist_ok=True)
        baseline = {
            'profile': args.profile,
            'perf_profile': profile['perf_profile'],
            'backend': result.get('backend', ''),
            'allowed_regression_percent': 35.0,
            'metrics': metric_values,
        }
        baseline_path.write_text(json.dumps(baseline, indent=2) + '\n', encoding='utf-8')
        print(f'Wrote performance baseline: {baseline_path}')
        return 0

    baseline = load_json(baseline_path)
    threshold = float(baseline.get('allowed_regression_percent', 35.0))
    failures = []
    for name, actual in metric_values.items():
        if name not in baseline.get('metrics', {}):
            failures.append(f'missing baseline metric: {name}')
            continue
        expected = float(baseline['metrics'][name])
        limit = expected * (1.0 + threshold / 100.0)
        print(f'{args.profile}: {name} actual={actual:.6f} baseline={expected:.6f} limit={limit:.6f}')
        if actual > limit:
            failures.append(
                f'{name} regressed from {expected:.6f}ms to {actual:.6f}ms (limit {limit:.6f}ms)'
            )

    if failures:
        print(f'{args.profile}: performance regression detected:')
        for failure in failures:
            print(f'  - {failure}')
        return 1

    print(f'{args.profile}: performance gate passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
