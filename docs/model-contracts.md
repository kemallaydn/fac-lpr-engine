# Production model contracts

This document records the authoritative production ONNX contracts. Tensor metadata comes from `fac-lpr-model-info` / artifact inspection; model-specific preprocessing and decoder semantics must come from the corresponding training/export contract and must never be inferred from tensor dimensions alone.

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

Verified parser/preprocess contract:

- input is BGR/RGB-aware native image data converted to RGB planar CHW
- input scale: `1 / 255`
- letterbox pad value: `114`
- output layout: features-first
- output features: `cx, cy, w, h`, one plate class score, then `4 x (x, y, confidence)` keypoints
- no separate objectness slot

The model metadata labels keypoints only as `0,1,2,3`. Runtime geometry does not hard-code those semantic labels: `PlateGeometryValidator` reorders the four finite points geometrically before perspective use.

## `lprnet_turkey.onnx`

Active model: **V2 Mixed Epoch 7**.

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

### Active OCR class / CTC contract

Training/export charset order is exactly:

```text
0 1 2 3 4 5 6 7 8 9 A B C D E F G H I J K L M N O P R S T U V Y Z -
```

There are 34 output classes total. The final `-` entry is **not** a literal plate character; it is the CTC blank class at `blank_index = 33`.

The native decoder therefore uses the 33 real characters only:

```text
0123456789ABCDEFGHIJKLMNOPRSTUVYZ
```

and supplies `blank_index = 33` separately. The output layout is BCT (`[batch, classes, timesteps]`), therefore the active model has 34 class slots and 24 CTC timesteps.

CTC decode semantics:

1. for each of the 24 timesteps, select the highest-scoring class;
2. collapse immediately repeated class indices;
3. remove every class index `33` blank;
4. map remaining class indices through the exact charset order above.

### Active OCR preprocessing contract

The training/export preprocessing is exactly:

```python
img = cv2.resize(img, (160, 40), interpolation=cv2.INTER_LINEAR)
# Keep OpenCV BGR order. Do NOT convert BGR -> RGB.
img = img.astype(np.float32)
img = (img - 127.5) * 0.0078125
img = np.transpose(img, (2, 0, 1))
img = np.expand_dims(img, 0)
img = np.ascontiguousarray(img, dtype=np.float32)
```

Equivalent native preprocessing contract:

```text
size          = 160 x 40
layout        = NCHW
color order   = BGR
input_scale   = 1.0
mean          = [127.5, 127.5, 127.5]
std           = [128.0, 128.0, 128.0]
```

The native formula `((pixel * input_scale) - mean) / std` is mathematically identical to `(pixel - 127.5) * 0.0078125` because `0.0078125 = 1 / 128`.

### Deprecated OCR contract

The old model contract:

```text
input  [1, 3, 24, 94]
output [1, 34, 18]
```

must **not** be used with the active V2 Mixed Epoch 7 model.

## Artifact policy

- Runtime provisioning must verify SHA-256 before activating a model.
- Model contract regression tests must fail if names, element types, ranks, dimensions, class ordering, blank association, preprocessing semantics, opsets, or expected digests change unexpectedly.
- A deliberate model upgrade requires updating this document and the corresponding manifest/contract tests in the same reviewed change.
