# FAC LPR Engine C ABI v1

This document defines the lifecycle, result-buffer ownership and versioning rules for the public C ABI.

## Stable v1 symbols

```c
fac_lpr_engine_create_v1
fac_lpr_engine_recognize_v1
fac_lpr_engine_destroy_v1
fac_lpr_get_last_error_v1
```

The `_v1` suffix is part of the binary contract. A breaking signature, calling-convention, ownership, enum-value, result layout or struct-layout change requires a new versioned symbol/type rather than silently changing v1.

## Pure-C boundary

The public header is `include/fac_lpr/fac_lpr_engine.h` and is compiled by the test suite as C11. Public declarations may use C scalar types, fixed-width integers, pointers, opaque handles and explicitly versioned C structs only. STL, C++ classes, exceptions, references and vendor types must never appear in this header.

## Struct versioning

Versioned input/result records use explicit v1 types. Input structs begin with:

```c
uint32_t struct_size;
uint32_t abi_version;
```

Callers initialize input structs with the provided `*_V1_INIT` macros. Existing v1 fields must never be removed, reordered or reinterpreted. Such a change is an ABI break and requires a new ABI version.

`FAC_LPR_ABI_VERSION_V1` is `1`.

Wire result struct sizes are locked by C11 static assertions in the native test suite:

```text
fac_lpr_text_ref_v1          8 bytes
fac_lpr_point_v1             8 bytes
fac_lpr_bbox_v1             16 bytes
fac_lpr_quadrilateral_v1    48 bytes
fac_lpr_candidate_v1        32 bytes
fac_lpr_evidence_v1         32 bytes
fac_lpr_plate_result_v1    140 bytes
fac_lpr_result_v1           32 bytes
```

The result buffer must begin at an address aligned to `FAC_LPR_RESULT_BUFFER_ALIGNMENT_V1`, currently 4 bytes.

## Opaque handle ownership

`fac_lpr_engine_handle` is opaque. Callers must not inspect, allocate or free its storage.

Creation:

```c
fac_lpr_engine_handle* engine = NULL;
fac_lpr_engine_config_v1 config = FAC_LPR_ENGINE_CONFIG_V1_INIT;
fac_lpr_status status = fac_lpr_engine_create_v1(&config, &engine);
```

Destruction deliberately accepts a pointer-to-handle:

```c
fac_lpr_engine_destroy_v1(&engine);
```

A successful or no-op destroy clears the caller slot to `NULL`. Repeated destroy of the same slot, `NULL` slots and a `NULL` pointer-to-handle are safe no-ops. Callers should maintain single ownership and destroy the original handle slot.

## Exception boundary

No C++ exception may cross any `fac_lpr_*` C ABI function. Internal typed engine exceptions are translated to `fac_lpr_status`; allocation failures map to `FAC_LPR_STATUS_RESOURCE_EXHAUSTED`; unknown failures map to `FAC_LPR_STATUS_INTERNAL_ERROR`.

## Image input

`fac_lpr_image_view_v1` is caller-owned and non-owning. The caller retains the image memory for the duration of the call. Width, height, stride and buffer extent are validated before the internal engine sees the view. Required strided extent is checked as:

```text
(height - 1) * stride_bytes + packed_row_bytes
```

## Recognition result ownership

The result is serialized completely into memory owned by the caller. No `char*`, candidate pointer, evidence pointer or other result pointer owned by the engine is returned.

Use the two-call size-query pattern:

```c
size_t required = 0;
fac_lpr_status status = fac_lpr_engine_recognize_v1(
    engine,
    &image,
    NULL,
    0,
    &required);

/* status == FAC_LPR_STATUS_BUFFER_TOO_SMALL when a result is available */

void* buffer = /* allocate at least required bytes, 4-byte aligned */;
status = fac_lpr_engine_recognize_v1(
    engine,
    &image,
    buffer,
    required,
    &required);
```

`FAC_LPR_STATUS_BUFFER_TOO_SMALL` has stable numeric value `10`. A buffer that is one byte short still reports the exact required size.

The buffer begins with `fac_lpr_result_v1`. Nested arrays and text are referenced by byte offsets from the start of that same caller-owned buffer:

```text
caller-owned output_buffer
┌──────────────────────────┐
│ fac_lpr_result_v1        │
│ fac_lpr_plate_result_v1[]│
│ evidence records         │
│ candidate records        │
│ decision reason values   │
│ UTF-8 / ASCII text bytes │
└──────────────────────────┘
```

For example, `fac_lpr_text_ref_v1` contains:

```c
uint32_t offset;
uint32_t length;
```

`offset` is relative to the beginning of `output_buffer`; `length` is the exact text byte count. Result text slices are not NUL-terminated. Empty text uses `{0, 0}`.

The same offset rule applies to recognition, evidence, candidate and decision-reason arrays. All v1 offsets and counts use 32-bit wire fields, so the serializer rejects output that exceeds the v1 32-bit representation limit.

## Result status and reason values

Public C ABI values are explicitly mapped from internal domain enums. Consumers must use the `FAC_LPR_RECOGNITION_*_V1` and `FAC_LPR_REASON_*_V1` constants rather than assuming internal C++ enum ordinal values.

## Error detail retrieval

A C API call that fails stores a thread-local diagnostic message. The caller retrieves it using its own buffer:

```c
size_t required = 0;
fac_lpr_get_last_error_v1(NULL, 0, &required);

char* message = /* caller allocation of required bytes */;
fac_lpr_get_last_error_v1(message, required, &required);
```

The returned error text is NUL-terminated because this API is explicitly a diagnostic string copy. The pointer remains caller-owned; the engine never exposes its internal string storage. A successful normal C API call clears the previous error detail.

## Runtime composition note

Until the concrete runtime composition root is connected by roadmap issue #56, a lifecycle-created handle may not contain a production inference pipeline. `recognize_v1` must return an explicit configuration error rather than fabricate recognition output.

## Export and calling convention

`FAC_LPR_API` controls symbol visibility/export. `FAC_LPR_CALL` is `__cdecl` on Windows and empty on non-Windows platforms. Static builds leave Windows import/export decoration empty. Shared-library packaging and distribution are completed by later packaging/release roadmap issues.
