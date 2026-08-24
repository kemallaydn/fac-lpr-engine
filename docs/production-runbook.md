# FAC LPR Engine Production Runbook

This runbook is for operators and consumer teams diagnosing startup, model/configuration, recognition quality, latency, memory, crash, and rollback issues on Windows and Linux.

## 1. Safe diagnostics defaults

- Do not log raw plate images, full frames, or cropped plates by default.
- Do not log complete plate text unless the consuming system's privacy policy explicitly allows it.
- Prefer request/correlation IDs, engine/model/config versions, provider name, status, latency, error code, and redacted evidence.
- Treat model files, calibration files, production config, dumps, and captured images as controlled artifacts.
- Collect the minimum data required to reproduce a problem and apply the consuming system's retention policy.

## 2. Minimum diagnostic snapshot

Capture these values before changing anything:

- engine version and source/build identifier
- operating system and architecture
- active execution provider
- model file paths, versions, and expected SHA-256 values
- config version and effective thresholds
- process RSS/private bytes and available system memory
- recent P50/P95/P99 latency and throughput
- accepted/review/rejected counts
- engine error code and error detail
- timestamps and request/correlation IDs

### Windows

Useful commands/tools:

```powershell
Get-Process <consumer-process> | Select-Object Id,ProcessName,CPU,WorkingSet64,PrivateMemorySize64
Get-FileHash <model.onnx> -Algorithm SHA256
Get-WinEvent -LogName Application -MaxEvents 100
```

For native crashes, preserve the Windows Error Reporting/minidump artifact together with the exact engine package and symbols used by the process.

### Linux

Useful commands/tools:

```bash
ps -o pid,ppid,%cpu,%mem,rss,vsz,etime,cmd -p <pid>
sha256sum <model.onnx>
cat /proc/<pid>/status
journalctl -u <consumer-service> --since "30 min ago"
dmesg | tail -n 100
```

If the kernel OOM killer is suspected, inspect `dmesg`/journal output before restarting the service so evidence is not lost.

## 3. Startup / readiness failures

### Symptoms

- engine handle cannot be created
- readiness/self-test fails
- consumer starts but rejects all recognition calls

### Checks

1. Confirm model/config paths exist and are readable by the service account.
2. Verify model SHA-256 against the expected artifact manifest.
3. Confirm architecture compatibility: x64 vs arm64 and Windows vs Linux package.
4. Confirm required runtime libraries are present next to the binary or in the documented runtime search path.
5. Confirm the configured execution provider is available. Do not silently assume GPU availability.
6. Run startup/self-test diagnostics and record the first concrete engine error.

### Recovery

- Restore the last known-good model/config pair if checksum, contract, or self-test validation fails.
- Do not bypass readiness checks in production to force the process online.

## 4. Model checksum / contract failures

### Symptoms

- model load failure
- unexpected tensor/input/output contract
- startup self-test rejection after a model update

### Checks

1. Compare SHA-256 with the expected release/provisioning manifest.
2. Confirm the detector/recognizer model version matches the engine configuration.
3. Verify model input dimensions, output names/shapes, and preprocessing contract.
4. Confirm the artifact was not partially copied or replaced in place.

### Recovery

- Stage model artifacts under a versioned path.
- Validate checksum and self-test before activation.
- If validation fails, keep the currently active session/model unchanged.

## 5. Low accuracy troubleshooting

Do not start by lowering acceptance thresholds. That can turn a recognition-quality problem into a false-accept problem.

Check, in order:

1. Input image dimensions, pixel format, stride, orientation, and corruption.
2. Detector confidence and whether plates are being missed before OCR.
3. Geometry/alignment success and crop quality.
4. Motion blur, shutter/exposure, glare, IR bloom, plate size in pixels, and camera angle.
5. Recognizer alternatives and calibrated confidence.
6. Accepted/review/rejected distribution compared with the previous known-good release.
7. Model/config version drift across hosts.

When comparing two releases, use the same fixed validation dataset and report exact-plate accuracy, character accuracy, false accepts, review rate, and rejection rate separately.

## 6. Latency or throughput regression

### Checks

- compare P50/P95/P99 rather than only averages
- confirm warm-up has completed before measuring
- inspect detector, alignment, recognizer, and post-processing stage timing separately
- check thread/concurrency settings and request queue depth
- confirm execution provider did not fall back unexpectedly
- check CPU saturation, memory pressure, thermal throttling, and I/O contention
- compare the exact model/config/build identifiers with the baseline

### Recovery

- revert the model/config/build that introduced the regression if the release budget is exceeded
- prefer bounded concurrency and backpressure over unbounded request accumulation

## 7. Memory growth / OOM

### Checks

1. Compare process RSS/private bytes over repeated create/recognize/destroy cycles.
2. Confirm request queues are bounded.
3. Check maximum image, crop/tile, tensor/output, and workspace limits.
4. Distinguish allocator high-water retention from continuously increasing live memory.
5. On Linux, check for kernel OOM events. On Windows, capture private bytes/working set and a dump if growth is persistent.
6. Record whether growth correlates with a particular model, resolution, concurrency level, or malformed input.

### Recovery

- reduce concurrency or reject oversized work if the configured resource budget is exceeded
- map allocation/resource exhaustion to a controlled engine error; native exceptions must not cross the C ABI
- restart only after collecting enough evidence to distinguish a leak from expected allocator retention

## 8. Crash / native exception

1. Preserve the exact engine package, model/config versions, and crash dump/core.
2. Record the last engine error/detail and correlation ID.
3. Reproduce with the same input only in a controlled environment; do not copy production plate images into developer systems without authorization.
4. Verify that consumer ABI/header version matches the loaded binary.
5. Run sanitizer/debug builds against the smallest reproducible case when available.

A native C++ exception must never escape through the public C ABI. Consumers should receive a documented error code/detail instead.

## 9. Common error categories

| Category | Meaning | Operator action |
| --- | --- | --- |
| invalid image | buffer, dimensions, stride, format, or image limits are invalid | validate caller input; do not retry unchanged input |
| configuration error | config version/value/path is invalid | revert/fix config and restart or reload safely |
| model load/contract error | model missing, checksum mismatch, unsupported contract | restore known-good model; verify manifest |
| provider error | requested execution provider is unavailable or failed | follow configured fail-fast/fallback policy and verify diagnostics |
| inference error | runtime inference failed for otherwise valid request | capture model/provider/build IDs and minimal reproduction |
| timeout/cancelled | request exceeded deadline or was cancelled | inspect queueing/concurrency and caller deadline |
| resource exhausted | configured memory/work/resource budget was exceeded | reduce load/input size; inspect memory pressure |
| internal error | unexpected internal failure mapped at the API boundary | capture diagnostics/dump and rollback if release-related |

Use the symbolic and numeric error values from the public API shipped with the exact release package; do not infer compatibility from an older header.

## 10. Safe model/config rollout and rollback

### Rollout

1. Provision the new model/config under a new versioned path.
2. Verify SHA-256 and file permissions.
3. Load and validate the new model in the background when reload is supported.
4. Run startup/self-test/known-sample checks before activation.
5. Atomically activate the validated session.
6. Keep the previous version available during the observation window.
7. Watch false accepts, review/reject distribution, latency, memory, and provider diagnostics.

### Rollback

1. Stop promoting traffic to the new model/session.
2. Reactivate the last known-good validated model/config pair.
3. Let in-flight requests finish on the session they started with when supported.
4. Confirm readiness and a small known-sample smoke test.
5. Record the failed model/config/build identifiers and reason for rollback.
6. Do not delete failed artifacts until the incident has enough evidence for diagnosis.

## 11. Escalation bundle

When escalating an incident, include:

- engine/package version and commit/build identifier
- OS/architecture and execution provider
- model/config versions and checksums
- exact engine error code/detail
- redacted diagnostics snapshot
- latency/memory trend around the incident
- minidump/core when applicable
- smallest authorized reproduction input or synthetic reproduction
- whether rollback restored normal operation

Never attach unrestricted production image datasets or raw plate logs merely because an incident ticket exists.