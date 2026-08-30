# Current validated project state

Snapshot date: **2026-08-30**

This document is a concise operational snapshot. `PRODUCT.md` remains the canonical product/architecture specification; code, tests and executed CI evidence remain authoritative when a snapshot becomes stale.

## Branch model

- `dev` is the active validated development/release-candidate branch.
- `main` is the stable/release branch.
- Promotion is `dev` -> `main` only after the exact `dev` candidate has applicable successful validation evidence.

At this snapshot, the latest validated implementation is on `dev` and is awaiting promotion to `main`.

## Product boundary

FAC LPR Engine performs technical license-plate recognition. It does not authorize vehicles or open barriers.

`ACCEPTED` means the recognition evidence is technically strong enough. Authorization remains the responsibility of FAC Access or another consuming product.

## Current recognition pipeline

The production per-frame path remains:

```text
image/frame
  -> detector
  -> geometry validation
  -> perspective/crop hypotheses
  -> OCR
  -> confidence/layout evidence
  -> candidate fusion
  -> technical decision
  -> ACCEPTED / REVIEW / REJECTED
```

The optional stream path remains additive:

```text
LprPipeline
  -> TemporalPlateConsensus
  -> StablePlateEventFilter
  -> RecognitionStreamSession
```

C ABI v1 is unchanged by the stream/session features.

## Latest recognition hardening

The plate-dominant crop path was hardened so a detector box occupying most of the source image does not destroy useful OCR evidence through unnecessary rectification/cropping. The full source frame is preserved as the primary hypothesis for these inputs while the normal crop strategy remains available for ordinary scenes.

Deterministic public real-image regression now verifies exact recognition for:

```text
38VU055
34VZ7387
```

The fixtures are downloaded from their licensed Wikimedia sources at test time, verified against pinned SHA-1 checksums, and then executed through the production `fac-lpr-cli` detector/crop/OCR path.

Pinned fixture SHA-1 values:

```text
38_VU_055.jpg   790095caf25739f07c00c55c7a513f70e8b7ef02
34_VZ_7387.jpg  6684fe7e4a6a0e742194ec12b08c980257512858
```

Recognition expectations must not be weakened merely to match a future regression.

## Current validation evidence

The latest pre-documentation code candidate was validated with the relevant self-hosted gates, including:

- Windows x64 Debug build and tests;
- Windows x64 Release build and tests;
- Release DLL clean-load validation;
- .NET P/Invoke consumer validation;
- Python `ctypes` consumer validation;
- macOS ARM64 C ABI / installed-package consumer validation;
- production lifecycle and real inference smoke;
- public licensed fixture regression with exact expected plate text.

The documentation-only synchronization commits that follow the validated code candidate must still be checked by the workflows applicable to their changed paths before promotion.

## Performance CI behavior

Performance validation runs Linux x64 inside `linux/amd64` Docker on the FAC LPR macOS ARM64 self-hosted runner.

Pull-request performance validation is path-aware:

- runtime/performance-sensitive changes run the real benchmark and comparison;
- documentation/test-metadata/CI-only changes that cannot change runtime performance explicitly skip the expensive benchmark work;
- a skip decision is produced by the workflow itself and is not confused with a benchmark pass for a runtime change.

Current benchmark sampling defaults:

```text
warmup: 10
iterations: 50
repeats: 2
```

Regression thresholds remain explicit and must not be relaxed simply to obtain green CI.

## GitHub work state at snapshot

FAC LPR Engine software backlog after the latest hardening work:

```text
open issues: 0
open pull requests: 0
```

The most recent completed engine work includes the plate-dominant production recognition fix and the licensed public fixture regression gate.

## FAC Access relationship

FAC Access consumes the engine through the stable public C ABI in its .NET Device Service infrastructure.

FAC Access still has physical-hardware release gates that cannot be closed by repository-only evidence, including real-camera E2E/soak validation and ONVIF real-camera onboarding validation. Those gates do not indicate missing FAC LPR Engine software work.

## Promotion rule

Before promoting `dev` to `main`:

1. verify the exact `dev` head;
2. verify applicable workflows for that exact head;
3. ensure there are no unexpected open engine issues/PRs;
4. compare `main...dev` and review the complete promotion delta;
5. promote the validated candidate without rewriting or dropping accumulated `dev` changes;
6. verify `main` contains the promoted candidate and post-promotion workflows behave as expected.
