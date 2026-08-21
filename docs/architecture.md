# Architecture

FAC LPR Engine uses an inward dependency rule:

```text
Public API / Composition Root
            |
            v
      Infrastructure
            |
            v
       Application
            |
            v
          Domain
```

## Domain

The domain layer contains stable value types for geometry, detections, recognition evidence, candidates and final recognition results.

Rules:
- Standard C++ only.
- No OpenCV or ONNX Runtime headers.
- No filesystem, network, logging or process dependencies.
- Ownership is expressed with value types and standard RAII containers.

## Application

Application code orchestrates recognition use cases and depends on domain types. Provider contracts are defined at this inward-facing boundary.

## Infrastructure

Infrastructure implements application contracts using ONNX Runtime, OpenCV, external OCR adapters and platform facilities. It may depend on application and domain, never the reverse.

## Public API and composition root

The engine entry point composes infrastructure implementations with the application pipeline. Public ABI mapping remains at the outermost boundary and must not expose vendor-specific types.

## Ownership rules

- Domain strings and collections use value semantics.
- Image and tensor lifetime is handled outside the domain layer.
- Runtime resources use deterministic RAII ownership.
- Public API ownership must be explicit and documented.
