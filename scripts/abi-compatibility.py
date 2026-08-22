#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path

PROTOTYPE = re.compile(
    r'FAC_LPR_API\s+(?P<ret>[A-Za-z_][A-Za-z0-9_\s\*]*)\s+FAC_LPR_CALL\s+'
    r'(?P<name>fac_lpr_[A-Za-z0-9_]+)\s*\((?P<args>.*?)\)\s*;',
    re.S,
)


def normalize_space(text: str) -> str:
    text = re.sub(r'\s+', ' ', text.strip())
    text = re.sub(r'\s*\*\s*', '* ', text)
    text = re.sub(r'\*\s+\*\s*', '** ', text)
    text = re.sub(r'\*\s+([A-Za-z_])', r'* \1', text)
    text = re.sub(r'\s*,\s*', ', ', text)
    return text.strip()


def parse_signatures(header: Path) -> dict[str, str]:
    text = header.read_text(encoding='utf-8')
    signatures = {}
    for match in PROTOTYPE.finditer(text):
        ret = normalize_space(match.group('ret'))
        name = match.group('name')
        args = normalize_space(match.group('args'))
        signatures[name] = f'{ret} {name}({args})'
    if not signatures:
        raise ValueError('no FAC_LPR_API prototypes found in public header')
    return signatures


def parse_symbols(path: Path) -> list[str]:
    names = set()
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        for match in re.finditer(r'(?<![A-Za-z0-9])_?(fac_lpr_[A-Za-z0-9_]+)\b', line):
            names.add(match.group(1))
    return sorted(names)


def project_version(cmake: Path) -> str:
    text = cmake.read_text(encoding='utf-8')
    match = re.search(r'project\s*\(.*?VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', text, re.S | re.I)
    if not match:
        raise ValueError('could not resolve project VERSION')
    return match.group(1)


def major(version: str) -> int:
    return int(version.split('.', 1)[0])


def capture(header: Path, layout: Path, symbols: Path, cmake: Path, output: Path) -> None:
    data = {
        'schemaVersion': 1,
        'semanticVersion': project_version(cmake),
        'abiMajor': 1,
        'symbols': parse_symbols(symbols),
        'signatures': parse_signatures(header),
        'layouts': json.loads(layout.read_text(encoding='utf-8')),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def compare(baseline: dict, current: dict) -> list[str]:
    breaks = []
    current_symbols = set(current['symbols'])
    for symbol in baseline['symbols']:
        if symbol not in current_symbols:
            breaks.append(f'removed exported symbol: {symbol}')

    for name, signature in baseline['signatures'].items():
        if name not in current['signatures']:
            breaks.append(f'removed public function declaration: {name}')
        elif current['signatures'][name] != signature:
            breaks.append(f'changed function signature: {name}')

    for name, layout in baseline['layouts'].items():
        if name not in current['layouts']:
            breaks.append(f'removed public struct layout: {name}')
        elif current['layouts'][name] != layout:
            breaks.append(
                f'changed struct layout: {name} baseline={layout} current={current["layouts"][name]}')
    return breaks


def enforce(baseline_path: Path, current_path: Path, report_path: Path | None) -> int:
    baseline = json.loads(baseline_path.read_text(encoding='utf-8'))
    current = json.loads(current_path.read_text(encoding='utf-8'))
    breaks = compare(baseline, current)
    major_bumped = major(current['semanticVersion']) > major(baseline['semanticVersion'])
    report = {
        'schemaVersion': 1,
        'baselineVersion': baseline['semanticVersion'],
        'currentVersion': current['semanticVersion'],
        'breakingChanges': breaks,
        'majorVersionBumped': major_bumped,
        'compatible': not breaks,
        'gatePassed': (not breaks) or major_bumped,
    }
    if report_path:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report['gatePassed'] else 2


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='command', required=True)

    cap = sub.add_parser('capture')
    cap.add_argument('--header', type=Path, required=True)
    cap.add_argument('--layout-json', type=Path, required=True)
    cap.add_argument('--symbols-file', type=Path, required=True)
    cap.add_argument('--project-file', type=Path, default=Path('CMakeLists.txt'))
    cap.add_argument('--output', type=Path, required=True)

    check = sub.add_parser('check')
    check.add_argument('--baseline', type=Path, required=True)
    check.add_argument('--current', type=Path, required=True)
    check.add_argument('--report', type=Path)

    args = parser.parse_args()
    if args.command == 'capture':
        capture(args.header, args.layout_json, args.symbols_file, args.project_file, args.output)
        raise SystemExit(0)
    raise SystemExit(enforce(args.baseline, args.current, args.report))


if __name__ == '__main__':
    main()
