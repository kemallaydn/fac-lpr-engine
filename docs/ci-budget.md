# CI budget policy

FAC LPR Engine does not start GitHub-hosted runners automatically while the repository is operating under a zero-spend Actions policy.

- `foundation-build` is manual (`workflow_dispatch`). Use `quick` for a single Linux GCC Release build and `full` only for release validation.
- `dependency-restore` is manual. Validate Linux first; run Windows only when a Windows-specific or release-level check is required.
- Automatic push/PR workflows must not be re-enabled without an explicit CI budget decision.
- A release candidate still requires the full cross-platform validation matrix before production acceptance.

This policy prevents routine development commits from consuming hosted-runner minutes unexpectedly while preserving reproducible validation workflows for manual use or a future self-hosted runner.
