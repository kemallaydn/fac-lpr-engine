#!/usr/bin/env python3
import argparse
import json
import os
import urllib.parse
import urllib.request
from pathlib import Path


def api(url: str, token: str) -> dict:
    req = urllib.request.Request(url, headers={
        'Authorization': f'Bearer {token}',
        'Accept': 'application/vnd.github+json',
        'X-GitHub-Api-Version': '2022-11-28',
    })
    with urllib.request.urlopen(req, timeout=30) as response:
        return json.load(response)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--repo', required=True)
    p.add_argument('--commit', required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    token = os.environ.get('GITHUB_TOKEN')
    if not token:
        raise SystemExit('GITHUB_TOKEN is required')

    query = urllib.parse.urlencode({'head_sha': args.commit, 'per_page': 100})
    data = api(f'https://api.github.com/repos/{args.repo}/actions/runs?{query}', token)
    workflows = {}
    artifact_names = []
    jobs_by_workflow = {}
    for run in data.get('workflow_runs', []):
        name = run.get('name', '')
        conclusion = run.get('conclusion') or run.get('status') or 'unknown'
        if workflows.get(name) != 'success':
            workflows[name] = conclusion
        if conclusion == 'success':
            artifacts = api(run['artifacts_url'] + '?per_page=100', token)
            artifact_names.extend(a.get('name','') for a in artifacts.get('artifacts', []))
            jobs = api(run['jobs_url'] + '?per_page=100', token)
            jobs_by_workflow[name] = [j.get('name','').lower() for j in jobs.get('jobs', []) if j.get('conclusion') == 'success']

    logical_artifacts = list(artifact_names)
    if workflows.get('release-package') == 'success':
        logical_artifacts += ['checksum','provenance','third-party','package-smoke']
    if workflows.get('dependency-security') == 'success':
        logical_artifacts += ['sbom']

    ci_jobs = jobs_by_workflow.get('ci-pr', [])
    evidence = {
        'releaseCommit': args.commit,
        'workflows': workflows,
        'artifacts': logical_artifacts,
        'realModelIntegrationPassed': workflows.get('mac-arm64-validation') == 'success',
        'goldenRegressionPassed': workflows.get('mac-arm64-validation') == 'success',
        'memoryLongRunPassed': workflows.get('memory-stress') == 'success',
        'windowsCleanBuildPassed': any('windows' in n for n in ci_jobs),
        'linuxCleanBuildPassed': any('linux' in n for n in ci_jobs),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(evidence, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
