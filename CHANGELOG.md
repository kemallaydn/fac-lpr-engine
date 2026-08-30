# Changelog

All notable changes to FAC LPR Engine are documented here. Release sections follow semantic versioning and must always include a `Breaking ABI` subsection so native consumers can distinguish compatible releases from ABI breaks.

## [Unreleased]

### Added
- Bounded temporal plate consensus, stateful recognition stream sessions and stable recognition-level duplicate suppression without changing the public C ABI v1.
- Deterministic multi-frame temporal regression coverage and real-model temporal smoke validation.
- Licensed public still-image regression coverage for the production detector/crop/OCR path using pinned Wikimedia fixture checksums.
- Exact public fixture expectations for `38VU055` and `34VZ7387`.

### Changed
- Plate-dominant input handling now preserves the full source frame as the primary crop hypothesis when the detected plate dominates the image, avoiding destructive rectification/cropping on these inputs.
- Windows self-hosted validation builds the production CLI so public real-model regression tests are registered and executed.
- Performance regression baseline lookup no longer requires the GitHub CLI on self-hosted runners; it uses authenticated GitHub REST requests.
- Performance validation is path-aware for pull requests and skips expensive benchmark work when runtime-sensitive files did not change.
- CI benchmark sampling is bounded to 10 warmup iterations, 50 measured iterations and 2 repeats for faster turnaround while keeping the existing regression thresholds.

### Fixed
- Public Turkish plate-dominant fixtures that previously regressed in the production detector/crop/OCR path now recognize exactly as expected.
- Self-hosted performance workflow portability failure caused by a missing `gh` executable.
- Meaningless performance failures on test/CI-only pull requests caused by comparing noisy historical benchmark samples despite no runtime code change.

### Breaking ABI
- None. Public C ABI v1 remains unchanged.

## [0.1.0] - 2026-08-23

### Added
- Production-oriented C/C++ LPR engine foundation, public C ABI, packaged CLI, model validation, diagnostics, regression tooling, consumer smoke tests, security and release packaging gates.

### Changed
- None.

### Fixed
- None.

### Breaking ABI
- None. Initial public ABI v1 release.
