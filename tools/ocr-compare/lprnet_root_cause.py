#!/usr/bin/env python3
"""Diagnostic, not production accuracy: ablate plate preprocessing and export
verified ONNX logits for native C++ production beam search on 32 synthetic crops.
No Paddle inference is performed here; no status is accepted by this script.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import compare_real_ocr as comparison


def variants(image):
    import cv2

    height, width = image.shape[:2]
    if height != 132 or width != 520:
        raise ValueError("Unexpected synthetic plate dimensions")
    # The known synthetic renderer puts a blue region at x=8..61.
    # Crops are hypotheses, not production preprocessing changes.
    return {
        "full_bgr": image,
        "without_blue": image[:, 64:],
        "text_tight": image[18:115, 64:509],
        "full_rgb_swap": cv2.cvtColor(image, cv2.COLOR_BGR2RGB),
    }


def model_logits(session, image, contract):
    import cv2
    import numpy as np

    resized = cv2.resize(image, (160, 40), interpolation=cv2.INTER_LINEAR)
    tensor = ((resized.astype(np.float32) - 127.5) / 128.0).transpose(2, 0, 1)[None, ...]
    output = session.run([contract["ocr.output_name"]], {contract["ocr.input_name"]: tensor})[0]
    if output.shape != comparison.EXPECTED_SHAPE or not np.isfinite(output).all():
        raise ValueError("Invalid ONNX model output")
    return output


def run(args):
    import cv2
    import numpy as np
    import onnxruntime as ort

    config = comparison.parse_contract(args.contract)
    digest = comparison.validate_model(args.model_dir / config["ocr.model"], config)
    manifest = comparison.load_manifest(args.fixture_dir / "manifest.tsv")
    session = ort.InferenceSession(
        str(args.model_dir / config["ocr.model"]),
        providers=["CPUExecutionProvider"])
    if [x.name for x in session.get_inputs()] != [config["ocr.input_name"]] or \
       [x.name for x in session.get_outputs()] != [config["ocr.output_name"]]:
        raise ValueError("Model input/output names differ from pinned contract")
    args.out.mkdir(parents=True, exist_ok=True)
    counts = {name: 0 for name in ("full_bgr", "without_blue", "text_tight", "full_rgb_swap")}
    with (args.out / "native-beam-input.tsv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        for filename, expected in manifest:
            image = cv2.imread(str(args.fixture_dir / filename), cv2.IMREAD_COLOR)
            if image is None:
                raise RuntimeError("Cannot open fixture: " + filename)
            for variant, candidate in variants(image).items():
                output = model_logits(session, candidate, config)
                raw_greedy, char_scores = comparison.decode_greedy(output)
                timestep_major = np.ascontiguousarray(output[0].T, dtype="<f4")
                if timestep_major.shape != (24, 34):
                    raise ValueError("Invalid native CTC logits shape")
                stem = Path(filename).stem
                name = f"{stem}_{variant}.f32"
                path = args.out / name
                path.write_bytes(timestep_major.tobytes())
                winners = output[0].argmax(axis=0)
                blank_steps = int(np.count_nonzero(winners == comparison.BLANK))
                writer.writerow([
                    variant, filename, expected, str(path),
                    raw_greedy, blank_steps, format(
                        sum(char_scores) / len(char_scores) if char_scores else 0.0, ".9f"),
                ])
                counts[variant] += int(raw_greedy == expected)
    print("MODEL_SHA256", digest)
    print("PREPROCESS_ABLATION_GREEDY", counts, flush=True)
    if sum(counts.values()) > 128 or len(manifest) != 32:
        raise RuntimeError("Diagnostic input cardinality error")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-dir", type=Path, required=True)
    parser.add_argument("--model-dir", type=Path, default=ROOT / "models")
    parser.add_argument("--contract", type=Path, default=ROOT / "docs/lpr-cli-contract.example")
    parser.add_argument("--out", type=Path, required=True)
    run(parser.parse_args())
