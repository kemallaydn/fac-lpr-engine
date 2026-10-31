#!/usr/bin/env python3
"""Run real LPRNet ONNX and PaddleOCR recognition on known-label plate crops.

This is a crop-only synthetic OCR experiment, NOT an end-to-end YOLO/CLI
test, NOT production candidate fusion, and NOT real-world accuracy evidence.
Model download/inference/manifest failures are errors, never fake success.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "paddle-worker"))
import worker as paddle_worker  # noqa: E402

CHARSET = "0123456789ABCDEFGHIJKLMNOPRSTUVYZ"
BLANK = len(CHARSET)
EXPECTED_SHAPE = (1, 34, 24)


def parse_contract(path: Path) -> dict[str, str]:
    config = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if "=" not in line:
            raise ValueError("invalid model contract entry")
        key, value = line.split("=", 1)
        config[key.strip()] = value.strip()
    return config


def validate_model(path: Path, config: dict[str, str]) -> str:
    if not path.is_file():
        raise FileNotFoundError("LPRNet model artifact is missing: " + str(path))
    size = path.stat().st_size
    expected_size = int(config["ocr.size_bytes"])
    if size != expected_size:
        raise ValueError("LPRNet model size mismatch")
    digest = hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
    if digest.lower() != config["ocr.sha256"].lower():
        raise ValueError("LPRNet model SHA-256 mismatch")
    if config["ocr.charset"] != CHARSET or int(config["ocr.blank_index"]) != BLANK:
        raise ValueError("LPRNet charset/blank differs from inspected model contract")
    if config["ocr.output_layout"] != "bct":
        raise ValueError("Unsupported LPRNet output layout")
    return digest


def decode_greedy(logits):
    """Match native CTC greedy: blank breaks repetitions, mean char softmax."""
    import numpy as np

    array = np.asarray(logits)
    if array.shape != EXPECTED_SHAPE or not np.isfinite(array).all():
        raise ValueError("Unexpected or non-finite LPRNet tensor output")
    time_major = array[0].T.astype(np.float64)
    shifted = time_major - time_major.max(axis=1, keepdims=True)
    probs = np.exp(shifted)
    probs /= probs.sum(axis=1, keepdims=True)
    text, confidences = [], []
    previous = -1
    for index, cls in enumerate(time_major.argmax(axis=1).tolist()):
        if cls != BLANK and cls != previous:
            text.append(CHARSET[cls])
            confidences.append(float(probs[index, cls]))
        previous = cls
    return "".join(text), confidences


def lprnet_recognize(session, image, config):
    import cv2
    import numpy as np

    # Match docs/lpr-cli-contract.example: BGR, 160x40, (pixel-127.5)/128.
    resized = cv2.resize(image, (160, 40), interpolation=cv2.INTER_LINEAR)
    tensor = ((resized.astype(np.float32) - 127.5) / 128.0)
    tensor = tensor.transpose(2, 0, 1)[None, ...]
    input_name = config["ocr.input_name"]
    output_name = config["ocr.output_name"]
    if [node.name for node in session.get_inputs()] != [input_name]:
        raise ValueError("ONNX LPRNet input name mismatch")
    if [node.name for node in session.get_outputs()] != [output_name]:
        raise ValueError("ONNX LPRNet output name mismatch")
    output = session.run([output_name], {input_name: tensor})[0]
    text, chars = decode_greedy(output)
    return {"text": text, "confidence": sum(chars) / len(chars) if chars else 0.0,
            "characterConfidences": chars}


def edit_distance(a: str, b: str) -> int:
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        now = [i]
        for j, cb in enumerate(b, 1):
            now.append(min(now[-1] + 1, prev[j] + 1, prev[j - 1] + (ca != cb)))
        prev = now
    return prev[-1]


def review_evidence(lpr: dict, paddle: dict) -> dict:
    """No invented Paddle per-character scores, and NO automatic correction.

    A differing high-confidence Paddle plate with a low-confidence LPR
    position may suggest a candidate for human review. This report NEVER
    upgrades an uncertain result to automatically accepted.
    """
    a, b = lpr["text"], paddle["text"]
    differences = [i for i in range(len(a)) if a[i] != b[i]] if len(a) == len(b) else []
    suggestion = None
    scores = lpr["characterConfidences"]
    if (len(differences) == 1 and len(scores) == len(a) and
            scores[differences[0]] < 0.65 and paddle["confidence"] >= 0.85):
        suggestion = b
    return {
        "agrees": bool(a and b and a == b),
        "reviewRequired": not (a and b and a == b),
        "differentPositionsZeroBased": differences,
        "paddleReviewSuggestion": suggestion,
    }


def load_manifest(path: Path):
    rows = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        parts = raw.split("\t")
        if len(parts) < 3 or not parts[0].endswith(".png") or not parts[2]:
            raise ValueError("Invalid synthetic OCR manifest entry")
        truth = parts[2]
        if "," in truth or not truth.isalnum() or truth != truth.upper():
            raise ValueError("Synthetic crop requires a single normalized truth")
        rows.append((parts[0], truth))
    if len(rows) != 32 or len(set(n for n, _ in rows)) != len(rows):
        raise ValueError("Expected exactly 32 unique labelled synthetic plate crops")
    return rows


def summarize(cases, key):
    n = len(cases)
    exact = sum(case[key]["text"] == case["expected"] for case in cases)
    char = sum(
        max(0.0, 1.0 - edit_distance(case[key]["text"], case["expected"]) /
            max(1, len(case["expected"])))
        for case in cases
    ) / n
    return {"exactCount": exact, "total": n, "exactRate": exact / n,
            "meanNormalizedCharacterAccuracy": char}


def render_markdown(report):
    summary = report["summary"]
    lines = [
        "# Real OCR comparison on synthetic plate crops", "",
        "This report runs actual LPRNet ONNX and PaddleOCR inference. It does not",
        "measure YOLO detection, production C++ fusion, or real-world accuracy.", "",
        "| Metric | LPRNet | PaddleOCR |",
        "|---|---:|---:|",
        "| Exact plate matches | %s / 32 | %s / 32 |" %
        (summary["lprnet"]["exactCount"], summary["paddle"]["exactCount"]),
        "| Exact rate | %.1f%% | %.1f%% |" %
        (100 * summary["lprnet"]["exactRate"], 100 * summary["paddle"]["exactRate"]),
        "| Mean normalized character accuracy | %.1f%% | %.1f%% |" %
        (100 * summary["lprnet"]["meanNormalizedCharacterAccuracy"],
         100 * summary["paddle"]["meanNormalizedCharacterAccuracy"]),
        "", "Agreement: %s / 32; disagreements: %s / 32." %
        (summary["agreements"], summary["disagreements"]),
        "Review suggestions are diagnostics only. Neither model may silently",
        "override the other and this test cannot mark production OCR accepted.", "",
        "| Crop | Expected | LPRNet | PaddleOCR | Agreement |",
        "|---|---|---|---|---|",
    ]
    for case in report["cases"]:
        lines.append("| %s | %s | %s | %s | %s |" %
                     (case["file"], case["expected"], case["lprnet"]["text"],
                      case["paddle"]["text"], "yes" if case["review"]["agrees"] else "no"))
    return "\n".join(lines) + "\n"


def run(args):
    import cv2
    import onnxruntime as ort
    from paddleocr import TextRecognition

    contract = parse_contract(args.contract)
    model_path = args.model_dir / contract["ocr.model"]
    digest = validate_model(model_path, contract)
    manifest = load_manifest(args.fixture_dir / "manifest.tsv")
    session = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
    model = TextRecognition(model_name="PP-OCRv5_mobile_rec", device="cpu")
    cases = []
    for filename, truth in manifest:
        path = args.fixture_dir / filename
        image = cv2.imread(str(path), cv2.IMREAD_COLOR)
        if image is None or image.size == 0:
            raise ValueError("Cannot read generated crop: " + filename)
        started = time.perf_counter()
        lpr = lprnet_recognize(session, image, contract)
        lpr_ms = round(1000 * (time.perf_counter() - started), 2)
        started = time.perf_counter()
        paddle_candidates = paddle_worker.recognize_crop(model, path.read_bytes())
        paddle_ms = round(1000 * (time.perf_counter() - started), 2)
        paddle = paddle_candidates[0] if paddle_candidates else {"text": "", "confidence": 0.0}
        if not math.isfinite(float(paddle["confidence"])):
            raise ValueError("PaddleOCR produced nonfinite confidence")
        review = review_evidence(lpr, paddle)
        cases.append({
            "file": filename, "variant": filename.rsplit("_", 1)[-1].removesuffix(".png"),
            "expected": truth, "lprnet": lpr, "paddle": paddle,
            "review": review, "latencyMs": {"lprnet": lpr_ms, "paddle": paddle_ms},
        })
        print("%s: expected=%s lprnet=%s paddle=%s agreement=%s" %
              (filename, truth, lpr["text"], paddle["text"], review["agrees"]), flush=True)
    summary = {
        "lprnet": summarize(cases, "lprnet"), "paddle": summarize(cases, "paddle"),
        "agreements": sum(c["review"]["agrees"] for c in cases),
        "disagreements": sum(c["review"]["reviewRequired"] for c in cases),
        "byVariant": {},
    }
    for variant in ("clean", "blur", "dark", "tilted"):
        subset = [c for c in cases if c["variant"] == variant]
        if len(subset) != 8:
            raise ValueError("Unexpected synthetic dataset variant count")
        summary["byVariant"][variant] = {
            "lprnet": summarize(subset, "lprnet"),
            "paddle": summarize(subset, "paddle"),
        }
    report = {
        "schemaVersion": 1,
        "kind": "synthetic-crop-real-model-ocr-comparison",
        "notProductionAcceptance": True, "lprModelSha256": digest,
        "paddleModelName": "PP-OCRv5_mobile_rec",
        "summary": summary, "cases": cases,
    }
    if args.native_input is not None:
        args.native_input.parent.mkdir(parents=True, exist_ok=True)
        with args.native_input.open("w", encoding="utf-8") as output:
            for case in cases:
                lpr = case["lprnet"]
                paddle = case["paddle"]
                values = [
                    case["file"], case["expected"], lpr["text"],
                    format(lpr["confidence"], ".9f"),
                    ";".join(format(n, ".9f") for n in lpr["characterConfidences"]),
                    paddle["text"], format(paddle["confidence"], ".9f"),
                ]
                if any("\t" in value or "\n" in value for value in values):
                    raise ValueError("Unexpected delimiter in OCR model output")
                output.write("\t".join(values) + "\n")
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.md_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.md_out.write_text(render_markdown(report), encoding="utf-8")
    github_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if github_summary:
        with Path(github_summary).open("a", encoding="utf-8") as out:
            out.write(render_markdown(report))
    print("LPRNet correct: %d/32; Paddle correct: %d/32; agreement: %d/32" %
          (summary["lprnet"]["exactCount"], summary["paddle"]["exactCount"],
           summary["agreements"]))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fixture-dir", type=Path, required=True)
    p.add_argument("--model-dir", type=Path, default=ROOT / "models")
    p.add_argument("--contract", type=Path, default=ROOT / "docs/lpr-cli-contract.example")
    p.add_argument("--native-input", type=Path, help="Write actual model predictions for compiled C++ policy")
    p.add_argument("--json-out", type=Path, required=True)
    p.add_argument("--md-out", type=Path, required=True)
    args = p.parse_args()
    run(args)


if __name__ == "__main__":
    main()
