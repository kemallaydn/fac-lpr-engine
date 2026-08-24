# Engine diagnostics snapshot

`fac_lpr::application::EngineDiagnostics` is an in-process, dependency-free diagnostics service. It does not depend on HTTP, Prometheus, OpenTelemetry, or any external metrics backend.

`LprPipelineDependencies` owns a diagnostics instance by default and `LprPipeline::diagnostics_snapshot()` exposes the current snapshot to embedding applications.

The snapshot contains only operational metadata:

- accepted / review / rejected counters;
- provider-failure count;
- active provider name/version/role metadata;
- active model name/version/SHA-256/integrity status metadata;
- aggregate stage latency count/mean/min/max;
- worker/queue active/pending/capacity/dropped state;
- a bounded last-error summary.

Recognition text, license-plate values, image bytes, crops, tensors, and candidate strings are intentionally not represented in the diagnostics model.

Decision/failure counters use relaxed atomics. Small metadata and dynamic stage-latency summaries are protected by one short-lived mutex. Snapshot creation copies the bounded operational state so consumers can serialize or export it without holding an engine lock.

External adapters may translate a snapshot to Prometheus, HTTP, logs, or another monitoring system outside the engine core.
