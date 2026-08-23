# Production release readiness

FAC LPR Engine production releases are fail-closed. Creating a `vMAJOR.MINOR.PATCH` tag is not sufficient to publish a release: the exact tagged commit must produce a successful machine-readable readiness report.

The production gate requires Linux and Windows clean package builds, native tests, real-model integration, golden regression, sanitizer and static-analysis gates, fuzzing, long-run memory validation, performance regression checks, ABI compatibility, packaged-artifact smoke tests, SBOM/license/provenance/checksum evidence, and release documentation.

A gate state of `missing`, `skipped`, `cancelled`, or `failure` is treated as a release failure. In particular, disabling hosted Windows/Linux CI does not silently waive those platforms for a production release.

The `production-readiness` workflow writes `production-readiness.json` with the source commit, version, every required gate state, failed gates, and the final `releaseApproved` decision. The automated `publish-release` workflow waits for that exact-commit report and refuses publication unless `releaseApproved` is `true`.

Release artifacts and the readiness report are kept together so a consumer can trace a published binary back to the quality gates that approved it.
