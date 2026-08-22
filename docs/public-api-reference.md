# FAC LPR Engine public API reference

This document describes the stable v1 C ABI exposed by `include/fac_lpr/fac_lpr_engine.h`. Consumers should compile against the installed public headers and load/link the release DLL/SO, not engine-internal C++ headers.

## Version and compatibility

`FAC_LPR_ABI_VERSION_V1` is the ABI major for all v1 structs and functions. Initialize every public struct with its `*_INIT` macro when using C, or set both `struct_size` and `abi_version` explicitly in FFI bindings.

Call `fac_lpr_get_version_v1` at startup when the consumer needs to enforce a minimum engine semantic version. A consumer built for ABI v1 may accept newer semantic versions only while `abi_major` remains `1`. A breaking struct layout, calling-convention, ownership, or function-contract change requires a new ABI major.

## Lifecycle

1. Build `fac_lpr_engine_config_v1` with `FAC_LPR_ENGINE_CONFIG_V1_INIT`.
2. Call `fac_lpr_engine_create_v1(&config, &handle)`.
3. Keep the opaque `fac_lpr_engine_handle*` until all work using it has completed.
4. Call `fac_lpr_engine_destroy_v1(&handle)`. Successful destroy sets the caller's pointer to `NULL` and is idempotent for an already-null pointer.

The current v1 C config intentionally contains only ABI/reserved fields. It does not expose model-path configuration yet. A bare v1 handle therefore validates lifecycle/error behavior but has no recognition pipeline attached. Production real-model execution is available through the packaged `fac-lpr-cli` and the C++ builder APIs. Future C configuration must be added through a backward-compatible new symbol/struct version rather than reinterpreting reserved fields.

## Image ownership

`fac_lpr_image_view_v1` is a borrowed view. The caller owns `data` and must keep the entire `[data, data + data_size)` region alive and immutable until `fac_lpr_engine_recognize_v1` returns.

For packed images:

- `GRAY8`: at least `width` bytes per row.
- `BGR8` / `RGB8`: at least `width * 3` bytes per row.
- `stride_bytes` may be larger than the packed row size.
- `data_size` must cover the last row including padding implied by the stride.

The engine never frees or retains the caller image buffer after the synchronous call returns.

## Result-buffer ownership

Recognition results use a caller-owned contiguous byte buffer. The engine writes no owning pointers into that buffer. All nested locations are byte offsets relative to the beginning of the supplied output buffer.

The buffer must be aligned to `FAC_LPR_RESULT_BUFFER_ALIGNMENT_V1`. `fac_lpr_result_v1` begins at offset zero. `fac_lpr_text_ref_v1` values reference non-NUL-terminated UTF-8/ASCII slices. Nested records and text remain valid only while the caller keeps the buffer unchanged.

A consumer should first call recognition with its chosen capacity and inspect `required_output_size` when `FAC_LPR_STATUS_BUFFER_TOO_SMALL` is returned, then allocate at least that many bytes and retry with the same input/handle state.

## Result fields

`fac_lpr_result_v1` contains the number and offset of recognition records, total latency, degraded state and provider-failure count. Each `fac_lpr_plate_result_v1` contains status, plate text, confidence values, optional bbox/quadrilateral geometry, evidence offsets, alternatives and decision-reason offsets.

Recognition status values are `ACCEPTED`, `REVIEW` and `REJECTED`. Consumers must not treat a `REVIEW` result as accepted merely because plate text is present.

## Error handling

Every exported operation returns `fac_lpr_status`:

| Status | Value | Meaning |
| --- | ---: | --- |
| `FAC_LPR_STATUS_OK` | 0 | Success |
| `FAC_LPR_STATUS_CONFIGURATION_ERROR` | 1 | Invalid config/handle/state |
| `FAC_LPR_STATUS_MODEL_LOAD_ERROR` | 2 | Model load or validation failure |
| `FAC_LPR_STATUS_INFERENCE_ERROR` | 3 | Inference failed |
| `FAC_LPR_STATUS_INVALID_IMAGE` | 4 | Invalid image view/buffer |
| `FAC_LPR_STATUS_PROVIDER_ERROR` | 5 | Provider failure |
| `FAC_LPR_STATUS_CANCELLED` | 6 | Operation cancelled |
| `FAC_LPR_STATUS_TIMEOUT` | 7 | Deadline exceeded |
| `FAC_LPR_STATUS_RESOURCE_EXHAUSTED` | 8 | Memory/resource budget exhausted |
| `FAC_LPR_STATUS_INTERNAL_ERROR` | 9 | Unexpected internal failure |
| `FAC_LPR_STATUS_BUFFER_TOO_SMALL` | 10 | Caller buffer is insufficient |

After a failure, call `fac_lpr_get_last_error_v1` on the same thread. First call with `buffer=NULL` and capacity zero to obtain `required_size`; then provide a buffer of at least that size. Error detail is thread-local and is diagnostic text, not a stable machine-readable protocol.

No C++ exception crosses the C ABI boundary.

## Thread safety

The detailed normative contract is in `docs/public-api-thread-safety.md`. In short: create/version are reentrant, recognize may run concurrently on the same live handle, last-error state is thread-local, and caller-owned memory is never synchronized by the engine. Do not concurrently mutate the same handle pointer variable or the same input/output storage without caller-side synchronization.

## Model and config provisioning

Release packages do not embed ONNX model files. For the packaged CLI, provide a model directory containing the detector and OCR models and pass an explicit contract/config path:

```text
fac-lpr-cli image.jpg --model-dir /opt/fac-lpr/models --config /etc/fac-lpr/lpr-contract.conf --json
```

Keeping models outside the engine package permits independent model versioning, integrity verification and licensing.

## Consumer examples

- Pure C: `tests/consumers/c_abi_smoke.c`. CI compiles it as C11 using only installed headers and links it to the staged release DLL/SO.
- Python: `examples/python/ctypes_consumer.py`. It declares the ABI with `ctypes`, passes an owned image buffer, reads thread-local errors and repeatedly exercises handle lifecycle.
- C#: `examples/csharp/Program.cs`. It uses explicit sequential structs and `DllImport`/cdecl against the installed native library.

The consumer smoke workflows intentionally use installed/staged artifacts so examples cannot pass by accidentally including private source-tree headers or linking private targets.

## Shutdown

Destroy every engine handle before unloading the native library or terminating an embedding runtime. All recognition calls using a handle must have returned before the caller releases input/output memory. Internal retired model sessions use shared RAII snapshots and are released after their final in-flight user completes.
