#!/usr/bin/env python3
"""Actual YOLO -> bounding-box crop -> two real OCR models on repository JPEGs.
Diagnostic only. Not the C++ CLI, perspective alignment, native beam or fusion.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ocr-compare"))
sys.path.insert(0, str(ROOT / "tools" / "paddle-worker"))
from compare_real_ocr import lprnet_recognize, parse_contract, validate_model  # noqa: E402
from worker import recognize_crop  # noqa: E402


def manifest(path):
    result = []
    for raw in (path / "golden-manifest.tsv").read_text(encoding="utf-8").splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        parts = raw.split("\t")
        if len(parts) < 3:
            raise ValueError("Invalid fixture manifest row")
        result.append((parts[0].strip(), parts[2].strip().split(",")))
    if len(result) != 3 or {item[0] for item in result} != {
            "plate_01.jpg", "plate_02.jpg", "plate_03.jpg"}:
        raise ValueError("Expected exactly three pinned real fixture photos")
    return result


def verify_yolo(path, config):
    if path.stat().st_size != int(config["detector.size_bytes"]):
        raise ValueError("YOLO model size mismatch")
    with path.open("rb") as handle:
        actual = hashlib.file_digest(handle, "sha256").hexdigest()
    if actual != config["detector.sha256"]:
        raise ValueError("YOLO model SHA mismatch")


def letterbox(image):
    import cv2
    import numpy as np
    height, width = image.shape[:2]
    scale = min(960 / width, 960 / height)
    rw = max(1, min(960, int(math.floor(width * scale + 0.5))))
    rh = max(1, min(960, int(math.floor(height * scale + 0.5))))
    left, top = (960-rw)//2, (960-rh)//2
    canvas = np.full((960, 960, 3), 114, dtype=np.uint8)
    canvas[top:top+rh, left:left+rw] = cv2.resize(image, (rw, rh))
    input_tensor = cv2.cvtColor(canvas, cv2.COLOR_BGR2RGB).astype(np.float32)
    return (input_tensor.transpose(2, 0, 1)[None] / 255.0), scale, left, top


def iou(a, b):
    x0, y0 = max(a[0], b[0]), max(a[1], b[1])
    x1, y1 = min(a[2], b[2]), min(a[3], b[3])
    inter = max(0, x1-x0)*max(0, y1-y0)
    union = (a[2]-a[0])*(a[3]-a[1]) + (b[2]-b[0])*(b[3]-b[1]) - inter
    return inter/union if union > 0 else 0.0


def detect(session, image, cfg):
    import numpy as np
    tensor, scale, pad_left, pad_top = letterbox(image)
    output = session.run([cfg["detector.output_name"]],
                         {cfg["detector.input_name"]: tensor})[0]
    if output.shape != (1, 17, 18900) or not np.isfinite(output).all():
        raise ValueError("Unexpected YOLO tensor")
    rows = output[0].T
    h, w = image.shape[:2]
    candidates = []
    for i in np.where(rows[:, 4] >= float(cfg["detector.confidence_threshold"]))[0]:
        x, y, bw, bh, conf = (float(v) for v in rows[i, :5])
        if bw <= 0 or bh <= 0:
            continue
        box = [max(0, min(w, (x-bw/2-pad_left)/scale)),
               max(0, min(h, (y-bh/2-pad_top)/scale)),
               max(0, min(w, (x+bw/2-pad_left)/scale)),
               max(0, min(h, (y+bh/2-pad_top)/scale))]
        if box[2] > box[0] and box[3] > box[1]:
            candidates.append((conf, int(i), box))
    candidates.sort(key=lambda t: (-t[0], t[1]))
    kept = []
    for item in candidates:
        if not any(iou(item[2], other[2]) > float(cfg["detector.nms_iou_threshold"])
                   for other in kept):
            kept.append(item)
            if len(kept) == 32:
                break
    return kept


def crop_plate(image, b):
    h, w = image.shape[:2]
    dx, dy = 0.04*(b[2]-b[0]), 0.06*(b[3]-b[1])
    x1 = max(0, math.floor(b[0]-dx))
    y1 = max(0, math.floor(b[1]-dy))
    x2 = min(w, math.ceil(b[2]+dx))
    y2 = min(h, math.ceil(b[3]+dy))
    return image[y1:y2, x1:x2]


def run(args):
    import cv2
    import onnxruntime as ort
    from paddleocr import TextRecognition

    cfg = parse_contract(args.contract)
    verify_yolo(args.model_dir / cfg["detector.model"], cfg)
    validate_model(args.model_dir / cfg["ocr.model"], cfg)
    photos = manifest(args.fixture_dir)
    detector = ort.InferenceSession(str(args.model_dir / cfg["detector.model"]),
                                    providers=["CPUExecutionProvider"])
    lprnet = ort.InferenceSession(str(args.model_dir / cfg["ocr.model"]),
                                  providers=["CPUExecutionProvider"])
    paddle = TextRecognition(model_name="PP-OCRv5_mobile_rec", device="cpu")
    crops = args.out / "detected-crops"
    crops.mkdir(parents=True, exist_ok=True)
    result = []
    for filename, required in photos:
        frame = cv2.imread(str(args.fixture_dir / filename), cv2.IMREAD_COLOR)
        if frame is None:
            raise RuntimeError("Could not read fixture " + filename)
        detections = detect(detector, frame, cfg)
        readings = []
        for n, (confidence, _, box) in enumerate(detections[:8]):
            cropped = crop_plate(frame, box)
            if cropped.size == 0:
                raise RuntimeError("Empty YOLO crop")
            crop_file = crops / f"{Path(filename).stem}_det{n}.png"
            if not cv2.imwrite(str(crop_file), cropped):
                raise RuntimeError("Could not write crop")
            lpr = lprnet_recognize(lprnet, cropped, cfg)
            secondary = recognize_crop(paddle, crop_file.read_bytes())
            other = secondary[0] if secondary else {"text": "", "confidence": 0.0}
            readings.append({
                "crop": crop_file.name, "box": [round(x, 2) for x in box],
                "detectorConfidence": round(confidence, 4),
                "lprnetGreedy": lpr["text"],
                "lprnetConfidence": round(lpr["confidence"], 4),
                "paddle": other["text"],
                "paddleConfidence": round(float(other["confidence"]), 4)})
        item = {"file": filename, "expectedRequired": required,
                "detectionCount": len(detections), "readings": readings,
                "lprnetHits": [x for x in required if any(y["lprnetGreedy"] == x for y in readings)],
                "paddleHits": [x for x in required if any(y["paddle"] == x for y in readings)]}
        print("REAL_PHOTO", json.dumps(item), flush=True)
        result.append(item)
    summary = {
        "totalPhotos": len(result),
        "photosWithDetections": sum(x["detectionCount"] > 0 for x in result),
        "requiredPlates": sum(len(x["expectedRequired"]) for x in result),
        "lprnetGreedyExactHits": sum(len(x["lprnetHits"]) for x in result),
        "paddleExactHits": sum(len(x["paddleHits"]) for x in result)}
    (args.out / "results.json").write_text(json.dumps({
        "kind": "real-photo-yolo-ocr-diagnostic", "notProductionCppCli": True,
        "summary": summary, "cases": result}, indent=2)+"\n", encoding="utf-8")
    lines = ["# Real vehicle photos: YOLO bbox and two OCR models", "",
             "Three repository JPEG fixtures. This is NOT the full C++ CLI, native beam,",
             "perspective/multi-crop engine or production decision policy.", "",
             "| Photo | Expected | YOLO detections | LPRNet greedy exact | Paddle exact |",
             "|---|---|---:|---|---|"]
    for c in result:
        lines.append("| %s | %s | %d | %s | %s |" % (
            c["file"], ",".join(c["expectedRequired"]), c["detectionCount"],
            ",".join(c["lprnetHits"]) or "none",
            ",".join(c["paddleHits"]) or "none"))
    report = "\n".join(lines) + "\n"
    (args.out / "results.md").write_text(report, encoding="utf-8")
    print("REAL_PHOTO_SUMMARY", json.dumps(summary), flush=True)
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary_file:
            summary_file.write(report)
    if summary["photosWithDetections"] == 0:
        raise RuntimeError("YOLO made zero detections across all real fixtures")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fixture-dir", type=Path, required=True)
    p.add_argument("--model-dir", type=Path, required=True)
    p.add_argument("--contract", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    run(p.parse_args())
