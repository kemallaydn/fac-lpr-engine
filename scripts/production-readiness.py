#!/usr/bin/env python3
import argparse
import json
from pathlib import Path

REQUIRED_GATES = (
    'linux-clean-build',
    'windows-clean-build',
    'native-tests',
    'real-model-integration',
    'golden-regression',
    'sanitizer',
    'static-analysis',
    'fuzz',
    'memory-long-run',
    'performance-regression',
    'abi-compatibility',
    'packaged-artifact-smoke',
    'sbom-license-provenance-checksum',
    'documentation-readiness',
)


def evaluate(gates: dict[str, str], sha: str, version: str) -> dict:
    normalized = {name: gates.get(name, 'missing') for name in REQUIRED_GATES}
    failed = [name for name, state in normalized.items() if state != 'success']
    return {
        'schemaVersion': 1,
        'sourceCommit': sha,
        'version': version,
        'releaseApproved': not failed,
        'gates': normalized,
        'failedGates': failed,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--gates', type=Path, required=True)
    parser.add_argument('--sha', required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raw = json.loads(args.gates.read_text(encoding='utf-8'))
    if not isinstance(raw, dict):
        raise SystemExit('gates input must be a JSON object')
    report = evaluate({str(k): str(v) for k, v in raw.items()}, args.sha, args.version)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2, sort_keys=True))
    raise SystemExit(0 if report['releaseApproved'] else 2)


if __name__ == '__main__':
    main()
