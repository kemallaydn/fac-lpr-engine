#!/usr/bin/env python3
"""Dry-run authorization from real video OCR observations.

This is NOT the native C++ decision, Java AccessService, or physical barrier driver.
The test intentionally fails closed when corroboration or permission is absent.
"""
import argparse
import json
from collections import Counter
from pathlib import Path

def simulate(report, registered_plate, active_permission=True, online=True):
    observations = []
    for frame in report["cases"]:
        for reading in frame["readings"]:
            if (reading["agrees"] and reading["detectorConfidence"] >= 0.5
                    and reading["lprnetConfidence"] >= 0.8
                    and reading["paddleConfidence"] >= 0.8):
                observations.append((frame["frame"], reading["lprnetGreedy"]))
    counts = Counter(plate for _, plate in observations)
    plate, count = counts.most_common(1)[0] if counts else (None, 0)
    recognized = count >= 3
    authorized = recognized and plate == registered_plate and active_permission
    pulse = bool(authorized and online)
    return {"plate": plate, "corroboratedFrames": count, "recognizedInDiagnostic": recognized,
            "registeredPlate": registered_plate, "activePermission": active_permission,
            "barrierOnline": online, "simulatedPulse": pulse,
            "notNativeCppDecision": True, "notFacAccessBackendExecution": True,
            "physicalCommandSent": False}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--registered-plate", default="34EZS794")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.results.read_text())
    if report.get("kind") != "video-real-model-diagnostic":
        raise RuntimeError("Expected real model video diagnostics, not synthetic inputs")
    cases = {
        "authorized_online": simulate(report, args.registered_plate),
        "not_registered": simulate(report, "00NOT999"),
        "permission_revoked": simulate(report, args.registered_plate, active_permission=False),
        "barrier_offline": simulate(report, args.registered_plate, online=False),
    }
    if not cases["authorized_online"]["simulatedPulse"]:
        raise AssertionError("Video OCR did not corroborate the registered plate")
    for key in ("not_registered", "permission_revoked", "barrier_offline"):
        if cases[key]["simulatedPulse"]:
            raise AssertionError(f"Unsafe simulated pulse: {key}")
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "barrier-dry-run.json").write_text(json.dumps(cases, indent=2) + "\n")
    print("BARRIER_DRY_RUN", json.dumps(cases), flush=True)

if __name__ == "__main__":
    main()
