# Startup self-test and readiness

FAC LPR Engine starts in `ReadinessState::failed`. A composition root must register integrity-verified active model metadata in `EngineDiagnostics`, then call `LprPipeline::startup_self_test()` before reporting the engine as ready.

The self-test performs three classes of checks:

1. **Model integrity**: at least one active model must be registered and every active model must include a non-empty SHA-256 and `integrity_verified=true`. The production composition obtains this state from `ModelLifecycleManager::validate_and_activate()`, which validates model path, exact size and SHA-256 before activation.
2. **Detector inference**: the configured detector is invoked with a small synthetic BGR image. Successful tensor preprocessing, ONNX session execution and output parsing must complete without an exception. A detector failure is fatal.
3. **Recognizer inference**: every enabled recognizer is invoked with a small synthetic BGR plate crop. `RecognizerRegistration::required` controls readiness policy. A required recognizer failure is fatal; an optional recognizer failure produces `degraded` readiness.

Readiness states:

- `ready`: every registered check passed;
- `degraded`: all required checks passed, at least one optional check failed;
- `failed`: a required check failed, model integrity is missing/unverified, or no enabled recognizer exists.

The complete check list and summaries are written to `EngineDiagnostics` and exposed by `LprPipeline::diagnostics_snapshot()`. No image, crop, tensor, plate text or OCR candidate is retained in the readiness diagnostics.

The active CLI contract also pins the expected SHA-256 and exact size for `best.onnx` and `lprnet_turkey.onnx`. The general production composition/root is responsible for turning that contract into `ModelLifecycleManager` activation metadata before invoking the self-test.
