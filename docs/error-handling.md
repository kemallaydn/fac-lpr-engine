# Error handling policy

The C++ core uses a stable engine error taxonomy. Public C ABI code must translate exceptions into explicit status codes and must never allow a C++ exception to cross the ABI boundary.

## Core categories

| Engine category | Public ABI status |
| --- | --- |
| configuration | `FAC_LPR_STATUS_CONFIGURATION_ERROR` |
| model_load | `FAC_LPR_STATUS_MODEL_LOAD_ERROR` |
| inference | `FAC_LPR_STATUS_INFERENCE_ERROR` |
| invalid_image | `FAC_LPR_STATUS_INVALID_IMAGE` |
| provider | `FAC_LPR_STATUS_PROVIDER_ERROR` |
| cancelled | `FAC_LPR_STATUS_CANCELLED` |
| timeout | `FAC_LPR_STATUS_TIMEOUT` |
| resource_exhausted | `FAC_LPR_STATUS_RESOURCE_EXHAUSTED` |
| internal / unknown | `FAC_LPR_STATUS_INTERNAL_ERROR` |

The public status enum lives in `include/fac_lpr/fac_lpr_error.h`. The reusable `c_api::invoke_noexcept` boundary in `include/fac_lpr/c_api/error_boundary.hpp` is the mandatory primitive for exported C functions introduced by the public ABI layer.

## ABI boundary mapping

The boundary applies this mapping:

1. `EngineError` maps from its stable `EngineErrorCode` to the matching public status.
2. `std::bad_alloc` maps to `FAC_LPR_STATUS_RESOURCE_EXHAUSTED`.
3. Every other exception, including non-standard exceptions, maps to `FAC_LPR_STATUS_INTERNAL_ERROR`.

No exception is permitted to escape an exported C function. Error strings are not propagated implicitly by the boundary, so implementation exception messages cannot accidentally cross the ABI.

## Error message safety

User-facing diagnostics may explain the failing subsystem and operation, but must not automatically include:

- image bytes or plate crops,
- secrets or credentials,
- untrusted raw payloads,
- full filesystem paths when a logical model/provider identifier is sufficient.

External library exceptions must be wrapped at the infrastructure boundary so ONNX Runtime, OpenCV and worker-specific exception types do not leak into the domain/application contracts.
