# Multi-platform CI

`ci-pr.yml` is the canonical Windows/Linux pull-request gate definition.

## Gates

When repository variable `FAC_LPR_HOSTED_CI_ENABLED` is set to `true`, pull requests targeting `main` or `dev` execute:

- Linux x64 GCC Release build + tests
- Linux x64 Clang Release build + tests
- Windows x64 MSVC Release build + tests
- clang-tidy + cppcheck static-analysis gate
- Linux ASan/LSan gate
- aggregate `Required CI gate`

Dependency-backed Linux/Windows jobs use vcpkg/ONNX Runtime caches keyed by the pinned manifests, baseline, runtime version and checksum files. CTest output/report directories are uploaded with `if: always()` so failure diagnostics remain accessible.

## Zero-spend state

The workflow is intentionally guarded by `vars.FAC_LPR_HOSTED_CI_ENABLED == 'true'`. Keep the variable unset/false while hosted Actions minutes are intentionally disabled/exhausted. This prevents a PR from consuming hosted runner minutes merely because the workflow file exists.

The gate must not be marked as a required branch-protection check until hosted CI is enabled and a clean Linux/Windows run has succeeded. Once validated, configure branch protection/rulesets to require `Required CI gate` before merge.

## Clean-runner requirement

The dependency-backed platform jobs bootstrap pinned dependencies themselves and do not rely on developer-machine state. Static-analysis/sanitizer jobs must likewise explicitly install or restore every analysis/test dependency they require; missing implicit machine dependencies are CI bugs, not runner prerequisites.
