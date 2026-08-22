#!/usr/bin/env python3
import argparse
import csv
import json
import math
import pathlib
import sys
from collections import defaultdict


def sigmoid(value: float) -> float:
    if value >= 0:
        z = math.exp(-value)
        return 1.0 / (1.0 + z)
    z = math.exp(value)
    return z / (1.0 + z)


def logit(probability: float, epsilon: float) -> float:
    p = min(max(probability, epsilon), 1.0 - epsilon)
    return math.log(p / (1.0 - p))


def fit_logistic(samples, epsilon: float, max_iterations: int = 100):
    # One-dimensional Platt-style logistic fit on logit(raw confidence).
    slope = 1.0
    intercept = 0.0
    l2 = 1.0e-6
    for _ in range(max_iterations):
        g_s = l2 * slope
        g_i = l2 * intercept
        h_ss = l2
        h_si = 0.0
        h_ii = l2
        for raw, label in samples:
            x = logit(raw, epsilon)
            p = sigmoid(slope * x + intercept)
            error = p - label
            weight = max(p * (1.0 - p), 1.0e-12)
            g_s += error * x
            g_i += error
            h_ss += weight * x * x
            h_si += weight * x
            h_ii += weight
        determinant = h_ss * h_ii - h_si * h_si
        if determinant <= 1.0e-18 or not math.isfinite(determinant):
            raise ValueError("singular calibration Hessian")
        step_s = (h_ii * g_s - h_si * g_i) / determinant
        step_i = (-h_si * g_s + h_ss * g_i) / determinant
        slope -= step_s
        intercept -= step_i
        if not math.isfinite(slope) or not math.isfinite(intercept):
            raise ValueError("non-finite calibration parameters")
        if max(abs(step_s), abs(step_i)) < 1.0e-8:
            break
    if slope <= 0.0:
        raise ValueError("fitted calibration slope is non-positive; segment is not usable")
    return slope, intercept


def metrics(samples, slope: float, intercept: float, epsilon: float, bins: int = 10):
    predictions = [sigmoid(slope * logit(raw, epsilon) + intercept) for raw, _ in samples]
    labels = [label for _, label in samples]
    brier = sum((p - y) ** 2 for p, y in zip(predictions, labels)) / len(samples)
    ece = 0.0
    for index in range(bins):
        low = index / bins
        high = (index + 1) / bins
        members = [i for i, p in enumerate(predictions) if low <= p < high or (index == bins - 1 and p == 1.0)]
        if not members:
            continue
        confidence = sum(predictions[i] for i in members) / len(members)
        accuracy = sum(labels[i] for i in members) / len(members)
        ece += (len(members) / len(samples)) * abs(confidence - accuracy)
    return {"brier": brier, "ece": ece}


def load_samples(path: pathlib.Path):
    groups = defaultdict(list)
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        required = {"provider", "crop_type", "raw_confidence", "label"}
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise ValueError("CSV missing columns: " + ", ".join(sorted(missing)))
        for line_number, row in enumerate(reader, 2):
            provider = row["provider"].strip()
            crop_type = row["crop_type"].strip()
            if not provider:
                raise ValueError(f"line {line_number}: provider is empty")
            raw = float(row["raw_confidence"])
            label = int(row["label"])
            if not math.isfinite(raw) or raw < 0.0 or raw > 1.0:
                raise ValueError(f"line {line_number}: raw_confidence must be in [0,1]")
            if label not in (0, 1):
                raise ValueError(f"line {line_number}: label must be 0 or 1")
            groups[(provider, crop_type)].append((raw, label))
    if not groups:
        raise ValueError("validation CSV contains no samples")
    return groups


def main() -> int:
    parser = argparse.ArgumentParser(description="Fit FAC LPR logistic confidence calibration")
    parser.add_argument("input_csv", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--model-version", required=True)
    parser.add_argument("--dataset-version", required=True)
    parser.add_argument("--minimum-samples", type=int, default=100)
    parser.add_argument("--probability-epsilon", type=float, default=1.0e-5)
    args = parser.parse_args()

    if args.minimum_samples <= 0:
        parser.error("--minimum-samples must be positive")
    if not 0.0 < args.probability_epsilon < 0.5:
        parser.error("--probability-epsilon must be in (0, 0.5)")
    if not args.model_version.strip() or not args.dataset_version.strip():
        parser.error("model/dataset versions cannot be empty")

    try:
        groups = load_samples(args.input_csv)
        segments = []
        rejected = []
        for (provider, crop_type), samples in sorted(groups.items()):
            if len(samples) < args.minimum_samples:
                rejected.append({"provider": provider, "cropType": crop_type, "sampleCount": len(samples), "reason": "minimum_samples"})
                continue
            positives = sum(label for _, label in samples)
            if positives == 0 or positives == len(samples):
                rejected.append({"provider": provider, "cropType": crop_type, "sampleCount": len(samples), "reason": "single_class"})
                continue
            slope, intercept = fit_logistic(samples, args.probability_epsilon)
            segment_metrics = metrics(samples, slope, intercept, args.probability_epsilon)
            segments.append({
                "provider": provider,
                "cropType": crop_type,
                "slope": slope,
                "intercept": intercept,
                "sampleCount": len(samples),
                "metrics": segment_metrics,
            })
        if not segments:
            raise ValueError("no calibration segment met minimum sample/class requirements")

        payload = {
            "schemaVersion": 1,
            "modelVersion": args.model_version,
            "datasetVersion": args.dataset_version,
            "minimumSamples": args.minimum_samples,
            "probabilityEpsilon": args.probability_epsilon,
            "segments": segments,
            "rejectedSegments": rejected,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {len(segments)} calibration segments to {args.output}")
        return 0
    except (OSError, ValueError, csv.Error) as error:
        print(f"calibration fit failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
