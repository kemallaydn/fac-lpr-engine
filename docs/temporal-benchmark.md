# Temporal recognition benchmark

`fac-lpr-temporal-benchmark` evaluates ordered frame sequences through the same production pipeline used by normal recognition, followed by the optional `RecognitionStreamSession` temporal consensus layer and `StablePlateEventFilter`.

It is deliberately separate from the single-image latency benchmark. A temporal result must never hide a per-frame regression.

## Manifest format

The input is a UTF-8 TSV file with three columns:

```text
timestamp_ms<TAB>expected_plate_or_-<TAB>image_path
```

Rules:

- timestamps are non-negative and monotonic;
- `-` means no plate is expected for that frame;
- relative image paths are resolved from the manifest directory;
- malformed rows, unreadable images and out-of-order timestamps fail closed.

Example:

```text
0	34ABC123	frames/0001.jpg
40	34ABC123	frames/0002.jpg
80	34ABC123	frames/0003.jpg
120	34ABC123	frames/0004.jpg
160	06XYZ456	frames/0005.jpg
200	06XYZ456	frames/0006.jpg
```

## Usage

```text
fac-lpr-temporal-benchmark sequence.tsv \
  --model-dir models \
  --config docs/lpr-cli-contract.example \
  --report temporal-benchmark.json
```

## Reported metrics

The JSON report keeps per-frame and temporal behavior separate:

- `perFrameAccuracy`: correctness of the ordinary pipeline on frames with an expected plate;
- `stableEmitted`: number of stable recognition events emitted after temporal consensus and duplicate filtering;
- `stableCorrect`: emitted stable events matching the expected plate on that frame;
- `falseStable`: emitted stable events that do not match the expected plate;
- `stablePrecision`: `stableCorrect / stableEmitted`;
- `duplicateSuppressed`: repeated stable events suppressed by the recognition-level cooldown;
- `ambiguousFrames`: frames containing multiple recognitions and therefore excluded from temporal consensus;
- `firstCorrectStableFrame`: first sequence frame at which a correct stable result was emitted;
- `maxHistoryObserved`: maximum bounded temporal history observed during the sequence.

A non-zero `falseStable` count makes the benchmark process return a non-zero exit code. False stable recognition is treated as a release-significant failure because temporal smoothing must not convert conflicting evidence into confident wrong output.

## Dataset expectations

Versioned regression sequences should cover at least:

1. a stable vehicle observed across many adjacent frames;
2. one-frame OCR character jitter;
3. motion blur and partial plate visibility;
4. low-confidence or rejected gaps;
5. conflicting candidate sequences;
6. a transition from one vehicle/plate to another;
7. multi-plate frames, which must not contaminate stream consensus.

Raw frame data is not retained by the temporal layer. Only bounded recognition results are held in session history.
