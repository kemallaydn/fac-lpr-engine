# Runtime model artifacts

ONNX and other model binaries are runtime artifacts and must not be committed to this repository.

For local development, point configuration to an external model directory or a path under `.runtime/`, which is ignored by Git.

Initial production models:
- `best.onnx` — plate detector / pose model
- `lprnet_turkey.onnx` — Turkish plate recognizer
