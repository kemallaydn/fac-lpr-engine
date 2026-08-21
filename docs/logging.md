# Logging policy

FAC LPR Engine logging is operational telemetry, not an evidence channel.

- Raw image bytes, image crops and recognized plate text must not be emitted by default log statements.
- Correlation should use request IDs, provider names, stage names, durations and typed error/status codes.
- `CallbackLogger` serializes callback invocation per logger instance and catches all callback/logging exceptions.
- Callback string pointers are borrowed and valid only during the callback invocation.
- Consumers that intentionally add sensitive information in their own callback remain responsible for their own retention and redaction policy.
