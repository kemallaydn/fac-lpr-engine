#!/usr/bin/env python3
"""Export REAL plate photo YOLO-crop LPRNet ONNX logits to native C++ beam.

Uses the same pinned detector, OCR contract and bounding-box crop as
evaluate_real_photos.py, which is NOT production perspective/multi-crop.
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

from compare_real_ocr import decode_greedy, parse_contract, validate_model
from evaluate_real_photos import crop_plate, detect, manifest, verify_yolo
from lprnet_root_cause import model_logits


def run(args):
    cfg = parse_contract(args.contract)
    verify_yolo(args.models / cfg["detector.model"], cfg)
    validate_model(args.models / cfg["ocr.model"], cfg)
    examples = manifest(args.fixtures)
    detector = ort.InferenceSession(
        str(args.models / cfg["detector.model"]), providers=["CPUExecutionProvider"])
    lpr = ort.InferenceSession(
        str(args.models / cfg["ocr.model"]), providers=["CPUExecutionProvider"])
    output = args.out
    output.mkdir(parents=True, exist_ok=True)
    ctc_dir = output / "ctc"
    crop_dir = output / "crops"
    ctc_dir.mkdir(exist_ok=True)
    crop_dir.mkdir(exist_ok=True)
    rows = []
    details = []
    for filename, required in examples:
        if len(required) != 1:
            raise ValueError("One required primary plate expected per real photo")
        frame = cv2.imread(str(args.fixtures / filename), cv2.IMREAD_COLOR)
        if frame is None or not frame.size:
            raise ValueError("Missing/unreadable real vehicle photo: " + filename)
        detections = detect(detector, frame, cfg)
        if not detections:
            raise RuntimeError("No YOLO detection for real fixture " + filename)
        confidence, _, bbox = detections[0]
        cropped = crop_plate(frame, bbox)
        if cropped is None or cropped.size == 0:
            raise ValueError("Empty top YOLO crop")
        stem = Path(filename).stem
        image_path = crop_dir / (stem + ".png")
        if not cv2.imwrite(str(image_path), cropped):
            raise IOError("Could not save real photo crop")
        logits = model_logits(lpr, cropped, cfg)
        greedy, chars = decode_greedy(logits)
        native = np.ascontiguousarray(logits[0].T, dtype="<f4")
        if native.shape != (24, 34):
            raise RuntimeError("ONNX logits shape mismatch")
        binary = ctc_dir / (stem + ".f32")
        binary.write_bytes(native.tobytes())
        blank_steps = int(np.count_nonzero(logits[0].argmax(axis=0) == 33))
        rows.append((filename, required[0], str(binary), greedy,
                     str(blank_steps), f"{confidence:.9f}"))
        item = {"file": filename, "truth": required[0], "greedy": greedy,
                "greedyConfidence": sum(chars)/len(chars) if chars else 0.0,
                "detectorConfidence": confidence, "blankSteps": blank_steps}
        print("REAL_NATIVE_EXPORT", json.dumps(item), flush=True)
        details.append(item)
    if len(rows) != 3:
        raise RuntimeError("Require exactly three original real fixture cases")
    with (output / "native-real-input.tsv").open("w", encoding="utf-8", newline="") as file:
        csv.writer(file, delimiter="\t", lineterminator="\n").writerows(rows)
    (output / "export.json").write_text(
        json.dumps({"kind":"real-photo-native-beam-input", "cases":details}, indent=2)+"\n",
        encoding="utf-8")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fixtures", type=Path, required=True)
    p.add_argument("--models", type=Path, required=True)
    p.add_argument("--contract", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    run(p.parse_args())
