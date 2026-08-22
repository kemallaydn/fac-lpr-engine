# FAC LPR Engine C ABI v1

This document defines the lifecycle and versioning rules for the public C ABI introduced by roadmap issue #35.

## Stable v1 symbols

```c
fac_lpr_engine_create_v1
fac_lpr_engine_recognize_v1
fac_lpr_engine_destroy_v1
```

The `_v1` suffix is part of the binary contract. A breaking signature, calling-convention, ownership, enum-value, or struct-layout change requires a new versioned symbol/type rather than silently changing v1.

## Pure-C boundary

The public header is `include/fac_lpr/fac_lpr_engine.h` and is compiled by the test suite as C11. Public declarations may use C scalar types, enums, pointers, opaque handles and explicitly versioned C structs only. STL, C++ classes, exceptions, references and vendor types must never appear in this header.

## Struct versioning

Every public v1 input struct starts with:

```c
uint32_t struct_size;
uint32_t abi_version;
```

Callers initialize structs with the provided `*_V1_INIT` macros. v1 implementations accept `struct_size >= sizeof(v1_struct)` so a future append-only extension can remain readable by an older implementation, provided the existing prefix retains identical meaning and layout. Removing, reordering or changing the type/meaning of an existing field is an ABI break and requires a new version.

`FAC_LPR_ABI_VERSION_V1` is `1`.

## Opaque handle ownership

`fac_lpr_engine_handle` is opaque. Callers must not inspect, allocate, copy-own or free its storage.

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

A successful or no-op destroy clears the caller slot to `NULL`. Repeated destroy of the same slot, `NULL` slots and a `NULL` pointer-to-handle are safe no-ops. Internally the implementation also tracks live handle addresses so an immediately repeated stale handle value can be rejected without dereferencing freed storage. Callers should nevertheless maintain single ownership and destroy the original handle slot.

## Exception boundary

No C++ exception may cross any `fac_lpr_*` C ABI function. Internal typed engine exceptions are translated to `fac_lpr_status`; allocation failures map to `FAC_LPR_STATUS_RESOURCE_EXHAUSTED`; unknown failures map to `FAC_LPR_STATUS_INTERNAL_ERROR`.

## Image input

`fac_lpr_image_view_v1` is caller-owned and non-owning. The caller retains the image memory for the duration of the call. Width, height, stride and buffer extent are validated before the internal engine sees the view. The required strided byte extent is:

```text
(height - 1) * stride_bytes + packed_row_bytes
```

with checked arithmetic.

## Recognition result boundary

Issue #35 reserves the stable `fac_lpr_engine_recognize_v1` symbol with caller-provided output memory arguments. The concrete v1 result layout, text/evidence/alternative access, required-size behavior and explicit buffer-too-small status are defined in sequential roadmap issue #36.

Until the concrete runtime composition root is connected (#56), a handle created by this lifecycle shell has no production inference pipeline. `recognize_v1` therefore validates the ABI/image boundary and returns a configuration error rather than fabricating an inference result.

## Export and calling convention

`FAC_LPR_API` controls symbol visibility/export. `FAC_LPR_CALL` is `__cdecl` on Windows and empty on non-Windows platforms. Static builds leave the Windows import/export decoration empty. Shared-library packaging and distribution policy are completed by later packaging/release roadmap issues.
