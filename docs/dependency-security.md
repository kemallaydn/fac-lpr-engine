# Dependency security and SBOM policy

FAC LPR Engine treats third-party dependency inventory and vulnerability status as release inputs.

## Inventory sources

The dependency security workflow restores the same pinned dependency graph used by production builds, then records:

- `vcpkg list` inventory for the selected triplet;
- pinned ONNX Runtime version/checksum material under `cmake/`;
- a CycloneDX JSON SBOM generated from the checked-out source plus restored dependency/build tree.

The generated SBOM and vulnerability report are uploaded as CI artifacts and are suitable for attaching to a release/package pipeline.

## Vulnerability gate

Grype scans the generated CycloneDX SBOM. The scanner runs with `--fail-on high`, therefore an unignored High or Critical vulnerability fails the security job and must block release.

Scanner/database/tool failures are also failures; they are not interpreted as a clean scan.

## Exception policy

`security/grype.yaml` starts with an empty ignore list. A High/Critical vulnerability may only be ignored when all of the following are recorded in the same change/review:

1. exact vulnerability identifier;
2. exact affected package/component;
3. technical reason the finding is not exploitable or cannot yet be remediated;
4. compensating control, when applicable;
5. owner;
6. expiry/review date;
7. link to the tracking issue.

Broad package-family, severity-wide or wildcard suppression is prohibited. An exception must be removed as soon as the fixed dependency can be adopted. Expired exceptions are release blockers.

## Release integration

A release job should consume the security workflow outputs or run the same commands immediately before packaging. Release artifacts should include at minimum the CycloneDX SBOM and retain the vulnerability report in CI provenance.
