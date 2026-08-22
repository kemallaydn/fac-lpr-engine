#!/usr/bin/env python3
import argparse
import json
import pathlib
import statistics
import sys

PERCENTILES = ("p50Ms", "p95Ms", "p99Ms")


def load(path: pathlib.Path):
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if data.get("schemaVersion") != 1:
        raise ValueError(f"unsupported benchmark schema in {path}")
    if data.get("warmupIterations", 0) < 10 or data.get("measuredIterations", 0) < 50:
        raise ValueError(f"benchmark sample too small in {path}")
    return data


def median_report(paths):
    reports = [load(path) for path in paths]
    if not reports:
        raise ValueError("at least one report is required")
    stages = set(reports[0].get("stages", {}))
    for report in reports[1:]:
        stages &= set(report.get("stages", {}))
    if "total" not in stages:
        raise ValueError("total stage missing from benchmark reports")

    result = {
        "framesPerSecond": statistics.median(r["framesPerSecond"] for r in reports),
        "recognitionsPerSecond": statistics.median(r["recognitionsPerSecond"] for r in reports),
        "stages": {},
    }
    for stage in sorted(stages):
        result["stages"][stage] = {
            metric: statistics.median(r["stages"][stage][metric] for r in reports)
            for metric in PERCENTILES
        }
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", nargs="+", type=pathlib.Path, required=True)
    parser.add_argument("--current", nargs="+", type=pathlib.Path, required=True)
    parser.add_argument("--latency-tolerance-percent", type=float, default=15.0)
    parser.add_argument("--throughput-tolerance-percent", type=float, default=12.0)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    if args.latency_tolerance_percent < 0 or args.throughput_tolerance_percent < 0:
        parser.error("tolerances cannot be negative")

    baseline = median_report(args.baseline)
    current = median_report(args.current)
    latency_factor = 1.0 + args.latency_tolerance_percent / 100.0
    throughput_factor = 1.0 - args.throughput_tolerance_percent / 100.0

    failures = []
    comparisons = []
    common_stages = sorted(set(baseline["stages"]) & set(current["stages"]))
    for stage in common_stages:
        for metric in PERCENTILES:
            base = baseline["stages"][stage][metric]
            value = current["stages"][stage][metric]
            limit = base * latency_factor
            passed = value <= limit
            comparisons.append({"kind": "latency", "stage": stage, "metric": metric,
                                "baseline": base, "current": value, "limit": limit, "passed": passed})
            if not passed:
                failures.append(f"{stage}.{metric}: {value:.3f}ms > {limit:.3f}ms")

    for metric in ("framesPerSecond", "recognitionsPerSecond"):
        base = baseline[metric]
        value = current[metric]
        limit = base * throughput_factor
        passed = value >= limit
        comparisons.append({"kind": "throughput", "metric": metric,
                            "baseline": base, "current": value, "limit": limit, "passed": passed})
        if not passed:
            failures.append(f"{metric}: {value:.3f} < {limit:.3f}")

    output = {
        "schemaVersion": 1,
        "baselineSampleCount": len(args.baseline),
        "currentSampleCount": len(args.current),
        "latencyTolerancePercent": args.latency_tolerance_percent,
        "throughputTolerancePercent": args.throughput_tolerance_percent,
        "passed": not failures,
        "failures": failures,
        "comparisons": comparisons,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")

    if failures:
        for failure in failures:
            print(f"REGRESSION: {failure}", file=sys.stderr)
        return 1
    print("performance regression gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
