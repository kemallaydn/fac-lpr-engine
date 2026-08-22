# Public API thread-safety contract

This document is normative for the v1 C ABI.

## Engine handles

- `fac_lpr_get_version_v1` and `fac_lpr_get_last_error_v1` are reentrant across threads.
- `fac_lpr_engine_create_v1` may be called concurrently from any number of threads.
- Multiple threads may call `fac_lpr_engine_recognize_v1` concurrently with the same live handle. Each call obtains its own strong pipeline snapshot before inference.
- `fac_lpr_engine_destroy_v1` may race with recognize calls that already have the raw handle value. A recognize call either obtains a valid pipeline snapshot or returns `FAC_LPR_STATUS_CONFIGURATION_ERROR`; it must not access freed handle storage.
- Destroy is idempotent for a caller-owned handle variable that has already been set to `NULL`.

## Caller synchronization requirements

The engine never synchronizes access to memory owned by the caller. The following operations therefore require caller-side synchronization:

- two threads mutating the same `fac_lpr_engine_handle*` variable through `fac_lpr_engine_destroy_v1`;
- mutation or release of an input image buffer while a recognition call is using it;
- concurrent writes to the same output/result buffer;
- concurrent writes to the same last-error destination buffer.

Passing independent pointer variables that contain the same raw engine handle to concurrent operations is supported. Passing the same pointer variable for mutation from multiple threads is not.

## Last error

Last-error text is thread-local. An error produced by one thread does not overwrite another thread's `fac_lpr_get_last_error_v1` state. Callers must read the error on the same thread that received the failing status.

## Reload and diagnostics

The v1 C ABI does not expose model reload or diagnostics mutation operations. Internal model/session reload uses atomic shared snapshots: in-flight inference completes on its acquired session and a validated replacement is published atomically. Diagnostic snapshots exposed by higher-level APIs must be treated as immutable point-in-time values.

## Compatibility rule

A future API that adds mutable handle operations must explicitly state whether it is concurrent with recognition. Unsupported combinations must fail with a defined status rather than relying on undefined object lifetime behavior.
