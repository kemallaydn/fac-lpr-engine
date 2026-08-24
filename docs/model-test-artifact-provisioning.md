# Model and Test Dataset Artifact Provisioning Policy

FAC LPR Engine keeps production ONNX models and sensitive plate-image datasets outside Git. CI and local development must provision them explicitly, verify integrity, and fail clearly when a required artifact is missing.

## Repository policy

The Git repository may contain:

- model metadata, expected file names, contract tests, and checksums
- synthetic/non-sensitive fixtures approved for source control
- provisioning scripts and documentation

The Git repository must not contain:

- production `.onnx` model binaries
- customer/production plate images or crops
- unrestricted validation datasets
- credentials, signed download URLs, access tokens, or storage keys

Runtime assets should live under ignored local paths such as `models/` and `test-data/` or another path supplied explicitly by configuration/environment.

## Local provisioning

Use explicit environment/config values rather than hard-coded developer paths. Recommended variables:

```text
FAC_LPR_MODEL_DIR=/absolute/path/to/models
FAC_LPR_TEST_DATA_DIR=/absolute/path/to/test-data
FAC_LPR_TEST_BEST_MODEL_PATH=/absolute/path/to/best.onnx
FAC_LPR_TEST_LPRNET_MODEL_PATH=/absolute/path/to/lprnet_turkey.onnx
```

A local provisioning flow must:

1. obtain the artifact from the approved storage location;
2. place it outside tracked source files;
3. verify the expected SHA-256/version before use;
4. point CMake/test configuration at the absolute path;
5. remove local sensitive test data when it is no longer required.

## CI provisioning

Real-model/real-dataset tests must use controlled CI artifact storage rather than Git.

Recommended flow:

1. CI authenticates to approved artifact storage using the runner's secret mechanism or workload identity.
2. The job downloads the versioned model/dataset artifact into the job workspace or ephemeral cache.
3. The job validates SHA-256 plus declared model/dataset version before running tests.
4. The job passes the resolved path through CMake cache variables/environment.
5. Tests that require the artifact run only after successful provisioning and validation.
6. The runner removes sensitive artifacts at job completion according to the runner cleanup policy.

Long-lived static credentials must not be written to repository files, generated build output, test reports, or logs.

## Missing-artifact behavior

A test requiring a real production artifact must never convert a missing artifact into a passing result.

Use one of two explicit modes:

- **required gate:** provisioning failure or checksum mismatch fails the CI job;
- **optional developer test:** the test is reported as skipped/not-run with a visible reason and cannot satisfy a release gate.

Release/readiness jobs must use required-gate behavior for every artifact named by the release acceptance criteria.

## Integrity and versioning

Each controlled artifact should have metadata containing at least:

- logical artifact name
- semantic/internal version
- SHA-256
- model/dataset purpose
- producing/training pipeline identifier when applicable
- compatible engine/model contract version
- creation date

Checksums stored in the repository are acceptable because they do not contain the artifact itself or credentials. A checksum mismatch is a hard failure for required CI gates.

## Dataset privacy and retention

Plate imagery can contain personal or operationally sensitive information. Therefore:

- use synthetic or anonymized fixtures when they are sufficient;
- restrict real datasets to approved storage and authorized CI runners/users;
- do not upload real plate images as ordinary GitHub issue attachments or test logs;
- keep CI artifacts containing images private and apply the shortest practical retention period;
- avoid embedding raw images or full plate text in machine-readable test reports unless explicitly required and approved;
- remove ephemeral dataset copies when the job/session ends;
- follow the data owner's deletion/retention policy when it is stricter than this document.

## Model licensing

Model files and their licenses are provisioned independently from the engine binary. A model must not be embedded into the engine release merely to simplify deployment. The consuming deployment must verify it has the rights to use the provisioned model.

## Release gate requirement

Before a production release is approved, the release job must record which model/dataset versions and checksums were used by real-model and acceptance tests. A release cannot claim those gates passed if required artifacts were absent, substituted silently, or failed integrity verification.