# Release packaging policy

FAC LPR Engine consumer packages contain only the versioned native engine binary, Windows import library when applicable, public C/C++ headers, CMake target metadata and a runtime dependency manifest.

## Package names

- `fac-lpr-engine-<version>-linux-x64.tar.gz`
- `fac-lpr-engine-<version>-windows-x64.zip`

Each archive is accompanied by a SHA-256 checksum file with the same name plus `.sha256`.

## Package contents

The install tree contains `bin/` and/or `lib/`, `include/fac_lpr/`, `lib/cmake/fac_lpr_engine/` and `share/fac-lpr-engine/runtime-manifest.json`. ONNX model files are runtime/customer assets and MUST NOT be included in the engine binary package. Packaging CI fails if any `*.onnx` file appears in the staging tree.

The runtime manifest records engine/ABI version, target system/processor, library type and external runtime dependency information. OpenCV and ONNX Runtime remain external runtime dependencies unless a future platform-specific redistribution policy explicitly bundles them.

## Debug symbol policy

Production runtime packages do not include debug symbols. Symbols are produced and retained only as separate restricted CI/release artifacts when a `RelWithDebInfo`/platform symbol build is requested. Windows PDB files and Linux debug information must never be mixed into the public runtime archive by default. Stripping/symbol extraction must not change the public ABI or checksum verification flow.

## Release gate

Packaging is separate from validation. A production release should publish package archives only after platform CI and the dependency-security/SBOM gate succeed. Package jobs always upload the archives and checksum files as CI artifacts so the exact bytes proposed for release can be reviewed.
