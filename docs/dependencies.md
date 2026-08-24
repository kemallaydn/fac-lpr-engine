# Dependency management

FAC LPR Engine uses vcpkg in manifest mode. The registry/toolchain baseline is pinned to:

`7f3781e19cc7d4e4882a4caec01668c6f7b5c163`

At that baseline the direct production/test dependencies resolve to:

- OpenCV (`opencv4`) 4.12.0, port-version 9; minimal features: intrinsics, JPEG, PNG, thread
- ONNX Runtime (`onnxruntime`) 1.23.2, port-version 1; CPU baseline
- GoogleTest (`gtest`) 1.18.0
- spdlog (`spdlog`) 1.17.0, port-version 1

The baseline commit pins the full transitive dependency graph. Do not update the baseline casually: dependency changes must pass Windows/Linux restore and build validation.

## One-command local restore

Linux:

```bash
./scripts/bootstrap-dependencies.sh
```

Windows PowerShell:

```powershell
./scripts/bootstrap-dependencies.ps1
```

Both scripts bootstrap vcpkg at the repository-pinned commit and restore the manifest into `build/vcpkg_installed`.

Runtime ONNX model files are intentionally not dependencies and must remain outside the repository.
