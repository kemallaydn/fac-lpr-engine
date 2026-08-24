# Static analysis policy

FAC LPR Engine keeps compiler warnings-as-errors as the common minimum standard across MSVC, GCC and Clang builds. Static analysis adds two opt-in correctness gates on top of that baseline.

## clang-tidy

Configure with:

```bash
cmake -S . -B build/static-analysis -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DFAC_LPR_ENABLE_CLANG_TIDY=ON
cmake --build build/static-analysis
```

`.clang-tidy` enables clang-analyzer, bugprone, performance and portability checks. Correctness-family findings are warnings-as-errors and therefore fail the build.

## cppcheck

After a compile database has been generated:

```bash
bash scripts/run-static-analysis.sh build/static-analysis
```

Cppcheck runs warning/performance/portability checks with a non-zero error exit code. Generated/dependency/runtime-data directories (`build`, `.tools`, vcpkg, `models`, `tests/fixtures`) are excluded.

## Suppression policy

There are currently no project suppressions. A suppression must be narrow, identify the exact diagnostic and source/third-party location, explain why the finding is a false positive, and include a removal condition. Broad wildcard suppressions and suppression of project-owned correctness defects are prohibited.

## CI

`.github/workflows/static-analysis.yml` runs on the dedicated self-hosted macOS ARM64 `fac-lpr` runner. It is available through `workflow_dispatch` and the repository-owned one-shot trigger path used for validation. The job ensures LLVM/clang-tidy and cppcheck are present, restores the real dependency graph, builds with clang-tidy enabled, then executes cppcheck from the same `compile_commands.json`. Any configure/build/analyzer failure fails the job.
