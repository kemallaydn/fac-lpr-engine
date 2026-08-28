# Temporal Stream Recognition

FAC LPR Engine keeps the core `LprPipeline::recognize()` path stateless. Temporal behavior is an optional application-layer capability that sits above completed per-frame recognition results.

## Architecture

```text
image / frame
    ↓
LprPipeline::recognize()
    ↓
per-frame PlateRecognitionResult
    ↓
TemporalPlateConsensus
    ↓
RecognitionStreamSession
    ↓
StablePlateEventFilter
    ↓
optional stable recognition emission
```

This is deliberately separate from the existing within-frame recognition mechanisms.

- `RecognitionEnsemble` combines OCR-provider evidence inside one frame.
- `WeightedMultiCropCandidateFusion` combines candidate support across crops/evidence inside one frame.
- `SafeRecognitionDecisionPolicy` makes the technical decision for one frame.
- `TemporalPlateConsensus` considers completed results from multiple ordered frames.
- `StablePlateEventFilter` suppresses redundant recognition emissions for the same stable observation.

Temporal consensus must never be used to hide a broken detector/OCR pipeline or to rewrite single-frame semantics.

## Fail-closed status rule

Only single-frame results whose status is already `ACCEPTED` are eligible to contribute voting weight to a stable temporal result.

`REVIEW` and `REJECTED` observations may remain in the bounded history for sequence accounting, but they cannot be promoted into an `ACCEPTED` result merely because the same text repeats over time.

This keeps the technical meaning of `ACCEPTED / REVIEW / REJECTED` intact.

## Bounded state

Temporal state is bounded by configuration:

- finite `max_history`
- finite `history_ttl`
- finite duplicate cooldown
- session-local state only
- explicit `reset()` and `close()` lifecycle

No global per-camera mutable state is owned by the engine.

`RecognitionStreamSession` also rejects out-of-order timestamps and prevents ambiguous multi-plate frames from contaminating one stream consensus history.

## Product boundary

The temporal layer is recognition-level behavior only.

It does not own:

- RTSP connections or camera discovery
- vehicle tracking lifecycle
- registered vehicle lookup
- access authorization
- barrier control
- database persistence
- customer-specific cooldown rules

A consuming product such as FAC Access remains responsible for deciding whether a recognized vehicle may create an access event or open a barrier.

## Public API decision

The existing C ABI v1 remains unchanged.

The current stream/session capability stays on the C++ application surface until a real consumer requires a stable FFI stream contract. This is intentional rather than incomplete work: adding session handles, timestamps, lifecycle rules and emission semantics to the public ABI before downstream requirements are proven would create a long-lived compatibility contract with little benefit.

When a public stream API is justified, it must be additive/versioned and must not change existing `fac_lpr_engine_*_v1` layouts, enum values, ownership rules or calling conventions.

## Multi-frame regression evidence

`tests/temporal_regression_benchmark_tests.cpp` provides deterministic ordered-sequence regression coverage for:

- OCR character jitter and convergence frame count
- alternating/conflicting strong plate candidates
- review-only evidence that must never be promoted
- duplicate stable-emission suppression
- transition from one stable plate to another
- bounded peak history

These tests complement the existing `RecognitionStreamSession` integration tests, which exercise the temporal layer through the normal `LprPipeline` path.

### Release-gating metrics

The following temporal properties are release-gating:

- zero false stable result in the conflict regression sequence
- review-only evidence never becomes stable accepted output
- deterministic convergence for the known jitter sequence
- eventual convergence to the new plate after a vehicle/plate transition
- configured history bounds are never exceeded
- existing single-frame, real-model, golden, resource, ABI and consumer checks remain green

### Diagnostic metrics

The following are useful diagnostics and should be tracked when real video datasets are expanded, but are not independent release gates yet:

- exact convergence-frame distribution across a large corpus
- temporal added latency distribution
- stable-emission suppression ratio
- per-camera sequence-state utilization

A future real-video corpus may promote these diagnostics into hard thresholds once representative production baselines exist.
