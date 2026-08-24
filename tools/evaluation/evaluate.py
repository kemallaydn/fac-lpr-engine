#!/usr/bin/env python3
import argparse
import json
import math
import statistics
import subprocess
from pathlib import Path


def levenshtein(a: str, b: str) -> int:
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(cur[-1] + 1, prev[j] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def load_manifest(path: Path):
    rows = []
    for raw in path.read_text(encoding='utf-8').splitlines():
        if not raw or raw.startswith('#'):
            continue
        parts = raw.split('\t')
        if len(parts) < 3:
            raise ValueError(f'invalid manifest row: {raw}')
        rows.append({
            'filename': parts[0],
            'expected_status': parts[1],
            'required': [x for x in parts[2].split(',') if x],
            'optional': [x for x in (parts[3] if len(parts) > 3 else '').split(',') if x],
        })
    if not rows:
        raise ValueError('manifest contains no evaluation rows')
    return rows


def run_cli(cli: Path, image: Path, model_dir: Path, config: Path):
    proc = subprocess.run(
        [str(cli), str(image), '--model-dir', str(model_dir), '--config', str(config), '--json'],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    if proc.returncode != 0:
        return {'recognitions': [], 'totalLatencyMs': math.inf, 'failures': [{'provider': 'cli', 'code': proc.returncode}], 'cliError': proc.stderr.strip()}
    return json.loads(proc.stdout)


def score_run(name, rows, fixture_dir, cli, model_dir, config, version):
    exact_hits = 0
    char_scores = []
    detected = 0
    aligned = 0
    false_accepts = 0
    false_reviews = 0
    statuses = {'ACCEPTED': 0, 'REVIEW': 0, 'REJECTED': 0}
    latencies = []
    details = []

    for row in rows:
        result = run_cli(cli, fixture_dir / row['filename'], model_dir, config)
        recognitions = result.get('recognitions', [])
        predicted = [str(r.get('plate', '')) for r in recognitions if r.get('plate')]
        accepted = [r for r in recognitions if r.get('status') == 'ACCEPTED']
        reviews = [r for r in recognitions if r.get('status') == 'REVIEW']
        expected = row['required'] + row['optional']
        required = row['required']

        if recognitions:
            detected += 1
        if any(float(r.get('geometryScore', 0.0)) > 0.0 for r in recognitions):
            aligned += 1
        for r in recognitions:
            status = r.get('status', 'REJECTED')
            statuses[status] = statuses.get(status, 0) + 1

        top = predicted[0] if predicted else ''
        if required and top in required:
            exact_hits += 1
        if required:
            best = min((levenshtein(top, gt) / max(1, len(gt)) for gt in required), default=1.0)
            char_scores.append(max(0.0, 1.0 - best))

        if accepted and not any(r.get('plate') in expected for r in accepted):
            false_accepts += 1
        if reviews and not any(r.get('plate') in expected for r in reviews):
            false_reviews += 1

        latency = float(result.get('totalLatencyMs', math.inf))
        if math.isfinite(latency):
            latencies.append(latency)
        details.append({'file': row['filename'], 'expected': required, 'predicted': predicted, 'latencyMs': latency})

    n = len(rows)
    metrics = {
        'datasetSize': n,
        'exactPlateAccuracy': exact_hits / n,
        'characterAccuracy': statistics.fmean(char_scores) if char_scores else 0.0,
        'detectionRecall': detected / n,
        'alignmentSuccess': aligned / n,
        'falseAcceptCount': false_accepts,
        'falseReviewCount': false_reviews,
        'statusDistribution': statuses,
        'latencyMs': {
            'mean': statistics.fmean(latencies) if latencies else None,
            'p50': statistics.median(latencies) if latencies else None,
            'throughputPerSecond': (1000.0 / statistics.fmean(latencies)) if latencies and statistics.fmean(latencies) > 0 else None,
        },
    }
    return {'name': name, 'version': version, 'modelDir': str(model_dir), 'config': str(config), 'metrics': metrics, 'cases': details}


def delta(a, b):
    keys = ['exactPlateAccuracy', 'characterAccuracy', 'detectionRecall', 'alignmentSuccess']
    out = {k: b['metrics'][k] - a['metrics'][k] for k in keys}
    out['falseAcceptCount'] = b['metrics']['falseAcceptCount'] - a['metrics']['falseAcceptCount']
    out['falseReviewCount'] = b['metrics']['falseReviewCount'] - a['metrics']['falseReviewCount']
    am = a['metrics']['latencyMs']['mean']
    bm = b['metrics']['latencyMs']['mean']
    out['meanLatencyMs'] = None if am is None or bm is None else bm - am
    return out


def render_markdown(report):
    a, b = report['runs']
    d = report['deltaBMinusA']
    lines = [
        '# FAC LPR Evaluation Report', '',
        f"Dataset: `{report['dataset']}`", '',
        '| Metric | A | B | Δ B-A |', '|---|---:|---:|---:|'
    ]
    for key, label in [
        ('exactPlateAccuracy', 'Exact plate accuracy'),
        ('characterAccuracy', 'Character accuracy'),
        ('detectionRecall', 'Detection recall'),
        ('alignmentSuccess', 'Alignment success')]:
        lines.append(f"| {label} | {a['metrics'][key]:.4f} | {b['metrics'][key]:.4f} | {d[key]:+.4f} |")
    lines.append(f"| False accepted | {a['metrics']['falseAcceptCount']} | {b['metrics']['falseAcceptCount']} | {d['falseAcceptCount']:+d} |")
    lines.append(f"| False review | {a['metrics']['falseReviewCount']} | {b['metrics']['falseReviewCount']} | {d['falseReviewCount']:+d} |")
    lines += ['', '## Versions', '', f"- A: {a['version']}", f"- B: {b['version']}", '', '> Alignment success is counted when at least one recognition has a positive geometry score.']
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description='Compare two FAC LPR engine/model configurations on the same golden dataset.')
    p.add_argument('--manifest', required=True, type=Path)
    p.add_argument('--fixture-dir', required=True, type=Path)
    p.add_argument('--cli-a', required=True, type=Path)
    p.add_argument('--model-dir-a', required=True, type=Path)
    p.add_argument('--config-a', required=True, type=Path)
    p.add_argument('--version-a', required=True)
    p.add_argument('--cli-b', required=True, type=Path)
    p.add_argument('--model-dir-b', required=True, type=Path)
    p.add_argument('--config-b', required=True, type=Path)
    p.add_argument('--version-b', required=True)
    p.add_argument('--json-out', required=True, type=Path)
    p.add_argument('--markdown-out', required=True, type=Path)
    args = p.parse_args()

    rows = load_manifest(args.manifest)
    a = score_run('A', rows, args.fixture_dir, args.cli_a, args.model_dir_a, args.config_a, args.version_a)
    b = score_run('B', rows, args.fixture_dir, args.cli_b, args.model_dir_b, args.config_b, args.version_b)
    report = {'schemaVersion': 1, 'dataset': str(args.manifest), 'runs': [a, b], 'deltaBMinusA': delta(a, b)}
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.markdown_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    args.markdown_out.write_text(render_markdown(report), encoding='utf-8')


if __name__ == '__main__':
    main()
