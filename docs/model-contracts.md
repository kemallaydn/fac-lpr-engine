# Production model contracts

This document records metadata inspected from the production ONNX artifacts without committing the model binaries to the repository.

The values below were produced with `fac-lpr-model-info` and the model SHA-256 digests were calculated independently. Model-specific parser/preprocessor implementations must use these inspected contracts rather than guessed tensor dimensions.

## `best.onnx`

- SHA-256: `22a9a65f6b0ef12baaba5e96d41b25fb9e475332229bd99b1c387f3345aea53d`
- File size: `12,972,328` bytes
- ONNX IR version: `7`
- Producer: `pytorch`
- Producer version: `2.11.0`
- Default opset: `12`
- Graph: `main_graph`
- Task metadata: `pose`
- Head metadata: `Pose`
- Batch metadata: `1`
- Image size metadata: `[960, 960]`
- Channels metadata: `3`
- Class metadata: `{0: 'plate'}`
- Keypoint metadata: `kpt_shape=[4, 3]`
- NMS metadata: `False`

Tensor contract:

```text
input  images   float32 [1, 3, 960, 960]
output output0  float32 [1, 17, 18900]
```

The metadata labels keypoints only as `0,1,2,3`; it does **not** define semantic corner order. Corner semantics must therefore be verified from the training/export convention or a real inference sample before hard-coding LU/RU/RD/LD meaning.

## `lprnet_turkey.onnx`

- SHA-256: `2d8fa236f468615ccd8b9ad6748c3e71b3d19ea53affdf5d5fee5a59719e310d`
- File size: `1,308,628` bytes
- ONNX IR version: `10`
- Producer: `pytorch`
- Producer version: `2.11.0+cu128`
- Default opset: `18`
- Graph: `main_graph`

Tensor contract:

```text
input  input   float32 [1, 3, 40, 160]
output output  float32 [1, 34, 24]
```

The model metadata does **not** encode the OCR charset, CTC blank index, channel normalization convention, or class-to-character mapping. Those values must be sourced from the exporter/training configuration or verified against known samples; they must not be inferred merely from the output class count of 34.

## Artifact policy

- Production `.onnx` binaries remain outside git.
- Runtime provisioning must verify SHA-256 before activating a model.
- Model contract regression tests should fail if names, element types, ranks, dimensions, opsets, or expected digests change unexpectedly.
- A deliberate model upgrade requires updating this document and the corresponding manifest/contract test in the same reviewed change.
