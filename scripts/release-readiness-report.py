#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

REQUIRED_GATES = ['ci-pr','dependency-security','performance-regression','abi-compatibility','resource-budget','cmake-package-smoke']
REQUIRED_ARTIFACT_TOKENS = ['checksum','provenance','sbom','third-party','package-smoke']


def evaluate(data: dict) -> dict:
    workflows = data.get('workflows', {})
    artifacts = [str(x).lower() for x in data.get('artifacts', [])]
    missing_gates = [g for g in REQUIRED_GATES if workflows.get(g) != 'success']
    missing_artifacts = [t for t in REQUIRED_ARTIFACT_TOKENS if not any(t in a for a in artifacts)]
    real_model = bool(data.get('realModelIntegrationPassed'))
    golden = bool(data.get('goldenRegressionPassed'))
    long_run = bool(data.get('memoryLongRunPassed'))
    cross_platform = bool(data.get('windowsCleanBuildPassed')) and bool(data.get('linuxCleanBuildPassed'))
    blockers = [f'gate:{g}' for g in missing_gates] + [f'artifact:{a}' for a in missing_artifacts]
    if not real_model: blockers.append('real-model-integration')
    if not golden: blockers.append('golden-regression')
    if not long_run: blockers.append('memory-long-run')
    if not cross_platform: blockers.append('windows-linux-clean-build')
    return {
        'schemaVersion': 1,
        'releaseCommit': data.get('releaseCommit', ''),
        'ready': not blockers,
        'blockers': blockers,
        'checks': {
            'requiredGates': REQUIRED_GATES,
            'missingGates': missing_gates,
            'missingArtifactClasses': missing_artifacts,
            'realModelIntegrationPassed': real_model,
            'goldenRegressionPassed': golden,
            'memoryLongRunPassed': long_run,
            'windowsLinuxCleanBuildPassed': cross_platform,
        },
    }


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    report = evaluate(json.loads(args.input.read_text(encoding='utf-8')))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(0 if report['ready'] else 2)


if __name__ == '__main__':
    main()
