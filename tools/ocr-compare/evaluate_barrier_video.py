#!/usr/bin/env python3
"""Sample a real video and run the repository's pinned YOLO, LPRNet and PaddleOCR models.

Diagnostic only: greedy OCR is not the native C++ beam/fusion/temporal policy.
No ground truth is inferred from model predictions.
"""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ocr-compare"))
from evaluate_real_photos import crop_plate, detect, verify_yolo
from compare_real_ocr import lprnet_recognize, parse_contract, validate_model


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--video", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--model-dir", type=Path, default=ROOT / "models")
    parser.add_argument("--contract", type=Path, default=ROOT / "docs/lpr-cli-contract.example")
    parser.add_argument("--sample-seconds", type=float, default=0.5)
    parser.add_argument("--max-frames", type=int, default=90)
    args = parser.parse_args()
    if not 0 < args.sample_seconds <= 10 or not 1 <= args.max_frames <= 500:
        parser.error("Invalid sampling limits")
    import cv2
    import onnxruntime as ort
    from paddleocr import TextRecognition
    from worker import recognize_crop
    cfg = parse_contract(args.contract)
    verify_yolo(args.model_dir / cfg["detector.model"], cfg)
    validate_model(args.model_dir / cfg["ocr.model"], cfg)
    detector = ort.InferenceSession(str(args.model_dir / cfg["detector.model"]), providers=["CPUExecutionProvider"])
    lprnet = ort.InferenceSession(str(args.model_dir / cfg["ocr.model"]), providers=["CPUExecutionProvider"])
    paddle = TextRecognition(model_name="PP-OCRv5_mobile_rec", device="cpu")
    capture = cv2.VideoCapture(str(args.video))
    if not capture.isOpened():
        raise RuntimeError("Video cannot be opened")
    fps = capture.get(cv2.CAP_PROP_FPS)
    if not 0 < fps < 240:
        raise RuntimeError("Invalid video FPS")
    step = max(1, round(fps * args.sample_seconds))
    args.out.mkdir(parents=True, exist_ok=True)
    cases = []
    index = 0
    try:
        while len(cases) < args.max_frames:
            ok, frame = capture.read()
            if not ok:
                break
            if index % step:
                index += 1
                continue
            started = time.perf_counter()
            detections = detect(detector, frame, cfg)
            readings = []
            for n, (confidence, _, box) in enumerate(detections[:4]):
                crop = crop_plate(frame, box)
                if crop.size == 0:
                    continue
                success, encoded = cv2.imencode(".png", crop)
                if not success:
                    raise RuntimeError("Crop encoding failed")
                lpr = lprnet_recognize(lprnet, crop, cfg)
                secondary = recognize_crop(paddle, encoded.tobytes())
                other = secondary[0] if secondary else {"text": "", "confidence": 0.0}
                readings.append({"detectorConfidence": round(confidence, 4),
                                 "bbox": [round(float(v), 1) for v in box],
                                 "lprnetGreedy": lpr["text"], "lprnetConfidence": round(lpr["confidence"], 4),
                                 "paddle": other["text"], "paddleConfidence": round(float(other["confidence"]), 4),
                                 "agrees": bool(lpr["text"] and lpr["text"] == other["text"])})
            case = {"frame": index, "timeSeconds": round(index / fps, 3),
                    "detectionCount": len(detections), "readings": readings,
                    "latencyMs": round((time.perf_counter() - started) * 1000, 2)}
            cases.append(case)
            print("VIDEO_FRAME", json.dumps(case), flush=True)
            index += 1
    finally:
        capture.release()
    if not cases:
        raise RuntimeError("No video frames decoded")
    detected = sum(bool(c["readings"]) for c in cases)
    agreements = sum(r["agrees"] for c in cases for r in c["readings"])
    report = {"kind": "video-real-model-diagnostic", "notProductionCppPipeline": True,
              "notNativeBeamOrTemporalDecision": True, "fps": fps,
              "sampledFrames": len(cases), "framesWithDetections": detected,
              "ocrAgreements": agreements, "cases": cases}
    (args.out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    lines = ["# Real barrier video diagnostic", "",
             "Actual YOLO + LPRNet greedy + PaddleOCR inference on sampled frames.",
             "**Not** the production C++ beam/fusion/temporal policy; no ACCEPTED/REVIEW/REJECTED is asserted.",
             "No labelled ground truth is available, so correctness cannot be scored.", "",
             f"FPS: {fps:.2f}; sampled frames: {len(cases)}; frames with detections: {detected}; OCR agreements: {agreements}.", "",
             "| Time (s) | Detections | LPRNet greedy | PaddleOCR | Agreement | Latency (ms) |",
             "|---:|---:|---|---|---|---:|"]
    for c in cases:
        first = c["readings"][0] if c["readings"] else None
        lines.append(f'| {c["timeSeconds"]:.2f} | {c["detectionCount"]} | '
                     f'{first["lprnetGreedy"] if first else "-"} | '
                     f'{first["paddle"] if first else "-"} | '
                     f'{"yes" if first and first["agrees"] else "no"} | {c["latencyMs"]:.1f} |')
    md = "\n".join(lines) + "\n"
    (args.out / "report.md").write_text(md)
    import os
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as output:
            output.write(md)
    print("VIDEO_SUMMARY", json.dumps({k: v for k, v in report.items() if k != "cases"}), flush=True)


if __name__ == "__main__":
    main()
