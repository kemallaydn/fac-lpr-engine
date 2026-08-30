# Multi-platform CI

FAC LPR Engine validates production behavior with a mix of guarded hosted workflows and self-hosted native/platform workflows. The source of truth is always the workflow definition plus the executed result for the exact commit being evaluated.

## Important current gates

### Windows x64 native validation

The self-hosted Windows workflow configures and builds both Debug and Release, runs the registered CTest suites, validates the Release DLL can load cleanly, and exercises .NET P/Invoke and Python `ctypes` consumers.

The Windows configuration enables the production CLI when tests are enabled so real-model CLI regression tests, including the licensed public fixture regression, are actually registered and executed.

### C ABI / native consumer validation

The self-hosted macOS ARM64 path builds/installs the production shared engine, compiles a standalone C consumer from the installed package, and executes lifecycle plus real production inference smoke validation.

### Performance regression

Linux x64 performance validation runs inside `linux/amd64` Docker on the FAC LPR macOS ARM64 self-hosted runner.

For pull requests, the workflow first inspects the diff. Expensive Docker/bootstrap/build/benchmark work runs only when runtime/performance-sensitive paths changed. Documentation, test-fixture metadata, or CI-only changes that cannot alter runtime performance receive an explicit successful skip decision instead of spending many minutes generating meaningless benchmark noise.

For runtime-sensitive changes, the benchmark still compares latency/throughput against a successful `dev` baseline and fails when configured regression tolerances are exceeded.

Current sampling defaults:

```text
WARMUP=10
ITERATIONS=50
REPEATS=2
```

The baseline lookup uses authenticated GitHub REST requests via `curl`/Python rather than assuming the GitHub CLI is installed on a self-hosted runner.

## Hosted PR matrix

`ci-pr.yml` remains the canonical hosted Windows/Linux PR matrix when repository variable `FAC_LPR_HOSTED_CI_ENABLED` is `true`. Depending on the workflow configuration, it covers areas such as Linux GCC/Clang, Windows MSVC, static analysis, sanitizers and an aggregate required gate.

A disabled hosted matrix is not treated as executed evidence. Required self-hosted gates must still pass for the candidate being promoted.

## Clean-runner rule

Dependency-backed jobs must bootstrap or restore their pinned dependencies explicitly. Missing implicit developer-machine state is a CI defect, not a valid runner prerequisite.

## Evidence rule

Never infer a pass from workflow existence. `queued`, `skipped`, cancelled or stale runs are not interchangeable with a successful applicable gate. Promotion to `main` must be based on the exact validated `dev` candidate.
