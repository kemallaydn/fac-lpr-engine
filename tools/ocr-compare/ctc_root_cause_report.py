#!/usr/bin/env python3
"""Inspect LPRNet CTC logits to diagnose character dropout across real and synthetic crops.

Known ground truth is used ONLY for analysis and forced alignment, never to
change production recognition, acceptance thresholds or model predictions.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path

import numpy as np


def read_contract(path: Path) -> dict[str, str]:
    config = {}
    for row in path.read_text(encoding="utf-8").splitlines():
        row = row.strip()
        if not row or row.startswith("#"):
            continue
        key, sep, val = row.partition("=")
        if not sep:
            raise ValueError("Invalid model contract")
        config[key.strip()] = val.strip()
    return config


def ctc_log_softmax(logits: np.ndarray) -> np.ndarray:
    if logits.ndim != 2 or logits.shape[0] < 1 or not np.isfinite(logits).all():
        raise ValueError("CTC logits must be finite 2D timesteps x classes")
    shifted = logits.astype(np.float64) - logits.max(axis=1, keepdims=True)
    return shifted - np.log(np.exp(shifted).sum(axis=1, keepdims=True))


def extended_target(text: str, charset: str, blank: int) -> list[int]:
    mapping = {ch: i if i < blank else i + 1 for i, ch in enumerate(charset)}
    if not text or any(ch not in mapping for ch in text):
        raise ValueError("Target uses a character outside the pinned model alphabet")
    result = [blank]
    for ch in text:
        result.extend((mapping[ch], blank))
    return result


def can_skip(states: list[int], s: int, blank: int) -> bool:
    return s >= 2 and states[s] != blank and states[s] != states[s - 2]


def ctc_forward(log_p: np.ndarray, text: str, charset: str, blank: int) -> float:
    """Full CTC log P(text | image), summing all paths (not beam rank/confidence)."""
    states = extended_target(text, charset, blank)
    if blank >= log_p.shape[1]:
        raise ValueError("Blank class outside logits")
    previous = np.full(len(states), -math.inf, dtype=np.float64)
    previous[0] = log_p[0, blank]
    previous[1] = log_p[0, states[1]]
    for t in range(1, len(log_p)):
        current = np.full(len(states), -math.inf, dtype=np.float64)
        for s, cls in enumerate(states):
            score = previous[s]
            if s >= 1:
                score = np.logaddexp(score, previous[s - 1])
            if can_skip(states, s, blank):
                score = np.logaddexp(score, previous[s - 2])
            current[s] = score + log_p[t, cls]
        previous = current
    return float(np.logaddexp(previous[-2], previous[-1]))


def ctc_viterbi(log_p: np.ndarray, text: str, charset: str, blank: int) -> tuple[float, list[int]]:
    """Best individual CTC path forced to a known ground-truth plate."""
    states = extended_target(text, charset, blank)
    previous = np.full(len(states), -math.inf, dtype=np.float64)
    previous[0] = log_p[0, blank]
    previous[1] = log_p[0, states[1]]
    back = np.full((len(log_p), len(states)), -1, dtype=np.int32)
    for t in range(1, len(log_p)):
        current = np.full(len(states), -math.inf, dtype=np.float64)
        for s, cls in enumerate(states):
            options = [(float(previous[s]), s)]
            if s >= 1:
                options.append((float(previous[s - 1]), s - 1))
            if can_skip(states, s, blank):
                options.append((float(previous[s - 2]), s - 2))
            score, prev_s = max(options, key=lambda pair: pair[0])
            current[s] = score + log_p[t, cls]
            back[t, s] = prev_s
        previous = current
    state = len(states) - 2 + int(np.argmax(previous[-2:]))
    score = float(previous[state])
    if not math.isfinite(score):
        return score, []
    alignment = [state]
    for t in range(len(log_p) - 1, 0, -1):
        state = int(back[t, state])
        alignment.append(state)
    return score, list(reversed(alignment))


def deleted_from_greedy(truth: str, greedy: str) -> list[int]:
    """Only unambiguous pure deletions; don't invent positions for repeated chars."""
    if len(greedy) >= len(truth):
        return []
    choices: set[tuple[int, ...]] = set()

    def recurse(at_truth: int, at_greedy: int, matched: tuple[int, ...]) -> None:
        if len(choices) > 1:
            return
        if at_greedy == len(greedy):
            choices.add(tuple(i for i in range(len(truth)) if i not in matched))
            return
        for i in range(at_truth, len(truth)):
            if truth[i] == greedy[at_greedy]:
                recurse(i + 1, at_greedy + 1, matched + (i,))

    recurse(0, 0, ())
    return list(next(iter(choices))) if len(choices) == 1 else []


def describe_case(logits: np.ndarray, expected: str, greedy: str,
                  charset: str, blank: int, beam_top1: str = "") -> dict:
    log_p = ctc_log_softmax(logits)
    probabilities = np.exp(log_p)
    winners = logits.argmax(axis=1)
    _, forced_path = ctc_viterbi(log_p, expected, charset, blank)
    deleted = deleted_from_greedy(expected, greedy)
    chars = []
    for pos, char in enumerate(expected):
        cls = extended_target(char, charset, blank)[1]
        aligned = [t for t, s in enumerate(forced_path) if s == 2 * pos + 1]
        global_peak_t = int(np.argmax(probabilities[:, cls]))
        winner = int(winners[global_peak_t])
        chars.append({
            "position": pos,
            "char": char,
            "deletedInGreedy": pos in deleted,
            "alignedTimesteps": aligned,
            "alignedPeakProbability": round(max(
                (float(probabilities[t, cls]) for t in aligned), default=0.0), 6),
            "globalPeakProbability": round(float(probabilities[global_peak_t, cls]), 6),
            "globalPeakTimestep": global_peak_t,
            "globalPeakWinner": "<blank>" if winner == blank else charset[
                winner if winner < blank else winner - 1],
            "argmaxFrameCount": int(np.count_nonzero(winners == cls)),
            "alignedTopCompetitor": [
                {"t": t, "blank": round(float(probabilities[t, blank]), 6),
                 "correctChar": round(float(probabilities[t, cls]), 6),
                 "winnerClass": int(winners[t]),
                 "winnerProbability": round(float(probabilities[t, winners[t]]), 6)}
                for t in aligned],
        })
    truth_lp = ctc_forward(log_p, expected, charset, blank)
    beam_lp = ctc_forward(log_p, beam_top1, charset, blank) if beam_top1 else None
    return {
        "expected": expected, "greedy": greedy, "beamTop1": beam_top1,
        "blankWinningTimesteps": int(np.count_nonzero(winners == blank)),
        "totalTimesteps": len(winners),
        "deletedPositions": deleted,
        "correctSequenceCtcLogProbability": round(truth_lp, 6),
        "beamSequenceCtcLogProbability": round(beam_lp, 6) if beam_lp is not None else None,
        "logProbabilityAdvantageCorrectMinusBeam": round(
            truth_lp - beam_lp, 6) if beam_lp is not None else None,
        "characters": chars,
    }


def load_native_rows(tsv: Path, cfg: dict[str, str], mode: str,
                     beam_lookup: dict[str, str]) -> list[dict]:
    classes = int(cfg["ocr.expected_classes"])
    steps = int(cfg["ocr.expected_timesteps"])
    charset = cfg["ocr.charset"]
    blank = int(cfg["ocr.blank_index"])
    if blank != len(charset) or classes != len(charset) + 1:
        raise ValueError("Unsupported pinned CTC class mapping")
    output = []
    with tsv.open(encoding="utf-8", newline="") as fh:
        for row in csv.reader(fh, delimiter="\t"):
            if not row:
                continue
            if mode == "real":
                if len(row) != 6:
                    raise ValueError("Expected six real TSV fields")
                filename, truth, binary, greedy, _, detector = row
                variant, extra = "yolo_bbox", {"detectorConfidence": float(detector)}
            else:
                if len(row) != 7:
                    raise ValueError("Expected seven synthetic TSV fields")
                variant, filename, truth, binary, greedy, _, _ = row
                extra = {}
            raw = np.fromfile(binary, dtype="<f4")
            if raw.size != steps * classes:
                raise ValueError(f"Incorrect ONNX logits shape: {binary}")
            entry = describe_case(raw.reshape(steps, classes), truth, greedy,
                                  charset, blank, beam_lookup.get(filename, "")
                                  if mode == "real" else "")
            entry.update({"file": filename, "variant": variant, **extra})
            output.append(entry)
    if mode == "real" and (
        len(output) != 3 or {r["file"] for r in output} !=
        {"plate_01.jpg", "plate_02.jpg", "plate_03.jpg"}
    ):
        raise ValueError("Expected all three real fixture photos")
    if mode == "synthetic" and (len(output) != 128 or
                                len({(r["variant"], r["file"]) for r in output}) != 128):
        raise ValueError("Expected all four variants of 32 synthetic photos")
    return output


def produce_report(real: list[dict], synthetic: list[dict], out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    (out / "ctc-diagnostics.json").write_text(json.dumps({
        "kind": "ctc-character-dropout-and-crop-diagnostics",
        "truthUsedOnlyForAnalysis": True,
        "real": real, "synthetic": synthetic,
    }, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# LPRNet CTC: character deletion diagnostics", "",
        "Diagnostic only: 3 real YOLO crops and 32 synthetic crops x 4 variants.",
        "No production OCR, ranking, confidence or acceptance changes.",
        "Raw CTC scores are NOT calibrated plate-correctness probabilities.", "",
        "## Real photos", "",
        "| File | Ground truth | Greedy | C++ beam top-1 | Blank frames | CTC log odds (truth minus beam) |",
        "|---|---|---|---|---:|---:|",
    ]
    for r in real:
        lines.append(
            f'| {r["file"]} | {r["expected"]} | {r["greedy"]} | {r["beamTop1"]} '
            f'| {r["blankWinningTimesteps"]}/24 | {r["logProbabilityAdvantageCorrectMinusBeam"]} |')
    lines.extend(["", "## Unambiguously deleted characters", "",
                  "| File | Deleted character | Position | Argmax frames for character | Max P(character) | Top class at its peak | Forced CTC alignment frames |",
                  "|---|---|---:|---:|---:|---|---|"])
    found = 0
    for r in real:
        for char in r["characters"]:
            if not char["deletedInGreedy"]:
                continue
            found += 1
            lines.append(
                f'| {r["file"]} | {char["char"]} | {char["position"]} | '
                f'{char["argmaxFrameCount"]} | {char["globalPeakProbability"]:.4f} | '
                f'{char["globalPeakWinner"]} | {char["alignedTimesteps"]} |')
    if not found:
        lines.append("| No unambiguous deletion | | | | | | |")
    groups = defaultdict(list)
    for r in synthetic:
        groups[r["variant"]].append(r)
    lines.extend(["", "## Synthetic crop comparison", "",
                  "| Variant | Cases | Greedy exact | Average blank-winning frames | Pure deletion cases |",
                  "|---|---:|---:|---:|---:|"])
    for variant, items in sorted(groups.items()):
        lines.append(
            f'| {variant} | {len(items)} | '
            f'{sum(x["expected"] == x["greedy"] for x in items)} | '
            f'{sum(x["blankWinningTimesteps"] for x in items)/len(items):.2f} | '
            f'{sum(bool(x["deletedPositions"]) for x in items)} |')
    lines.extend(["", "Forced alignment explains how the target **could** be emitted, "
                  "not whether the model favors it. CTC sequence log probabilities "
                  "sum all paths and do not reproduce beam pruning/grammar effects.",
                  "Three real photos cannot validate a production fix.", ""])
    (out / "ctc-diagnostics.md").write_text("\n".join(lines), encoding="utf-8")
    print("CTC_REAL_SUMMARY", json.dumps([{
        "file": r["file"],
        "deleted": [(c["char"], c["position"], c["argmaxFrameCount"],
                     c["globalPeakProbability"], c["alignedTimesteps"])
                    for c in r["characters"] if c["deletedInGreedy"]],
        "truthMinusBeamCTCLogOdds": r["logProbabilityAdvantageCorrectMinusBeam"],
    } for r in real]), flush=True)


def run(args) -> None:
    cfg = read_contract(args.contract)
    beam_json = json.loads(args.beam_json.read_text(encoding="utf-8"))
    beam_lookup = {entry["file"]: entry["beamTop1"] for entry in beam_json["cases"]}
    real = load_native_rows(args.real_input, cfg, "real", beam_lookup)
    synthetic = load_native_rows(args.synthetic_input, cfg, "synthetic", {})
    produce_report(real, synthetic, args.out)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--contract", type=Path, required=True)
    p.add_argument("--real-input", type=Path, required=True)
    p.add_argument("--synthetic-input", type=Path, required=True)
    p.add_argument("--beam-json", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    run(p.parse_args())
