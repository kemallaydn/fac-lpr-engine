# CI execution policy

FAC LPR Engine uses self-hosted validation for the expensive native/platform-specific gates and keeps GitHub-hosted execution guarded where appropriate.

## Automatic validation

Pull-request and push workflows may execute automatically when their configured runner is available. Current important self-hosted paths include:

- Windows x64 native validation on the FAC LPR Windows self-hosted runner.
- macOS ARM64 C-ABI/native consumer validation on the FAC LPR macOS ARM64 self-hosted runner.
- Linux x64 performance validation through `linux/amd64` Docker on the macOS ARM64 runner.

The performance workflow is path-aware on pull requests. If a PR does not touch runtime/performance-sensitive paths, the expensive Docker/bootstrap/build/benchmark work is skipped while the workflow still reports a successful decision. Runtime-sensitive PRs continue to execute the real performance gate.

Current benchmark sampling defaults are intentionally bounded for CI turnaround:

```text
warmup:     10
iterations: 50
repeats:    2
```

These values reduce turnaround cost without converting the performance check into a fake pass; regression thresholds remain explicit and must not be weakened merely to obtain green CI.

## Hosted-runner budget guard

Workflows that use GitHub-hosted runners may still be guarded by repository variables such as `FAC_LPR_HOSTED_CI_ENABLED`. A workflow file existing is not evidence that a hosted job ran.

## Release rule

A release candidate requires executed evidence for the exact candidate on every applicable required platform/gate. `queued`, `skipped`, stale historical success, or a workflow definition by itself is not a release pass.

Self-hosted runner availability is an operational dependency. If a required runner is offline, the candidate remains unvalidated rather than silently accepted.
