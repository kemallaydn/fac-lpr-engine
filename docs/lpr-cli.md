# FAC LPR offline CLI

`fac-lpr-cli` runs the real application `LprPipeline` on a JPG/PNG image without a camera or external consumer.

## Build

The CLI requires the same native inference dependencies as production recognition:

```text
FAC_LPR_WITH_ONNX_RUNTIME=ON
FAC_LPR_WITH_OPENCV=ON
FAC_LPR_BUILD_LPR_CLI=ON
```

It is intentionally not built when ONNX Runtime or OpenCV is disabled.

## Model contract

The CLI never guesses model contracts. Start from:

```text
docs/lpr-cli-contract.example
```

Populate every inspector/training-contract placeholder from the real `best.onnx` and `lprnet_turkey.onnx` artifacts. Input tensor width/height for the detector and LPRNet are read from the ONNX descriptors, but node names, YOLO output offsets/layout, OCR charset/blank index/output layout and preprocessing semantics remain explicit contract data.

## Usage

```text
fac-lpr-cli image.jpg \
  --model-dir /path/to/models \
  --config /path/to/lpr.contract \
  --log-level info
```

JSON output:

```text
fac-lpr-cli image.png --model-dir ./models --config ./lpr.contract --json
```

Human-readable provider evidence:

```text
fac-lpr-cli image.jpg --model-dir ./models --config ./lpr.contract --debug-evidence
```

Exit codes:

```text
0 success
2 invalid CLI/path arguments
3 image decode failure
4 typed FAC LPR engine/config/model/inference failure
5 unexpected runtime failure
```

## Composition

The CLI does not duplicate recognition logic. Its small composition factory wires existing providers into the application pipeline:

```text
OnnxSession(best.onnx)
→ YoloPoseOnnxDetector
→ PlateGeometryEvaluatorAdapter
→ OpenCvPerspectiveAligner
→ CropHypothesisGenerator
→ OnnxSession(lprnet_turkey.onnx)
→ LprNetOnnxOcrAdapter
→ GenericOnnxOcrRecognizer
→ RecognitionEnsemble
→ IdentityConfidenceCalibrator
→ ConnectedComponentPlateLayoutAnalyzer
→ WeightedMultiCropCandidateFusion
→ SafeRecognitionDecisionPolicy
→ LprPipeline
```

This CLI-specific wiring is intentionally small. The general provider registry/builder remains roadmap issue #56.

## Validation status

Provider contract/unit coverage can run without production model artifacts by using fake ONNX sessions. A true JPG/PNG → real `PlateRecognitionResult` acceptance run still requires the actual runtime model artifacts and their authoritative contract values. Missing artifacts must not be represented as a passing real-model test.
