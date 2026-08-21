# Error handling policy

The C++ core uses a stable engine error taxonomy. Public C ABI code must translate exceptions into explicit status codes and must never allow a C++ exception to cross the ABI boundary.

## Core categories

| Engine category | Intended public ABI status |
| --- | --- |
| configuration | `FAC_LPR_CONFIG_ERROR` |
| model_load | `FAC_LPR_MODEL_LOAD_FAILED` |
| inference | `FAC_LPR_INFERENCE_FAILED` |
| invalid_image | `FAC_LPR_INVALID_IMAGE` |
| provider | `FAC_LPR_PROVIDER_FAILED` |
| cancelled | `FAC_LPR_CANCELLED` |
| timeout | `FAC_LPR_TIMEOUT` |
| resource_exhausted | `FAC_LPR_RESOURCE_EXHAUSTED` |
| internal / unknown | `FAC_LPR_INTERNAL_ERROR` |

The actual C ABI enum is introduced with the public ABI issue; this document fixes the semantic mapping ahead of that boundary.

## ABI boundary catch order

The outer C ABI adapter must use this order:

1. `EngineError` and map its stable `EngineErrorCode`.
2. `std::bad_alloc` and map to resource exhausted.
3. `std::exception` and map to internal error.
4. Catch-all for non-standard exceptions and map to internal error.

No exception is permitted to escape an exported C function.

## Error message safety

User-facing diagnostics should explain the failing subsystem and operation, but must not automatically include:

- image bytes or plate crops,
- secrets or credentials,
- untrusted raw payloads,
- full filesystem paths when a logical model/provider identifier is sufficient.

External library exceptions must be wrapped at the infrastructure boundary so ONNX Runtime, OpenCV and worker-specific exception types do not leak into the domain/application contracts.
