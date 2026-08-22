#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--layout', type=Path, required=True)
    p.add_argument('--symbols', type=Path, required=True)
    p.add_argument('--abi-major', type=int, required=True)
    args = p.parse_args()

    baseline = json.loads(args.baseline.read_text(encoding='utf-8'))
    layout = json.loads(args.layout.read_text(encoding='utf-8'))
    exported = {line.strip().lstrip('_') for line in args.symbols.read_text(encoding='utf-8').splitlines() if line.strip()}

    breaks = []
    for symbol in baseline['symbols']:
        if symbol not in exported:
            breaks.append(f'missing exported symbol: {symbol}')

    for name, expected in baseline['structs'].items():
        actual = layout.get(name)
        if actual is None:
            breaks.append(f'missing struct layout: {name}')
            continue
        if actual.get('size') != expected.get('size'):
            breaks.append(f'struct size changed: {name}: {expected.get("size")} -> {actual.get("size")}')

    baseline_major = int(baseline['abi_major'])
    if breaks and args.abi_major <= baseline_major:
        raise SystemExit('ABI compatibility failure without ABI major bump:\n- ' + '\n- '.join(breaks))

    if args.abi_major < baseline_major:
        raise SystemExit(f'ABI major regressed: {baseline_major} -> {args.abi_major}')

    print(json.dumps({
        'compatible': not breaks,
        'baselineAbiMajor': baseline_major,
        'currentAbiMajor': args.abi_major,
        'breakingChanges': breaks,
        'newSymbols': sorted(exported - set(baseline['symbols'])),
    }, indent=2))


if __name__ == '__main__':
    main()
