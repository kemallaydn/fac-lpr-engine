#!/usr/bin/env python3
import argparse
import re
from pathlib import Path

SEMVER = re.compile(r'^v(?P<version>0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$')


def changelog_section(text: str, version: str) -> str:
    pattern = re.compile(rf'^## \[{re.escape(version)}\](?:\s+-\s+[^\n]+)?\s*$\n(?P<body>.*?)(?=^## \[|\Z)', re.M | re.S)
    match = pattern.search(text)
    if not match:
        raise ValueError(f'CHANGELOG.md has no [{version}] section')
    return match.group('body').strip()


def validate(tag: str, changelog: Path) -> tuple[str, str]:
    match = SEMVER.fullmatch(tag)
    if not match:
        raise ValueError('release tag must be semantic version format vMAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]')
    version = match.group('version')
    body = changelog_section(changelog.read_text(encoding='utf-8'), version)
    if '### Breaking ABI' not in body:
        raise ValueError('release changelog section must explicitly contain a Breaking ABI subsection')
    return version, body


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    parser.add_argument('--changelog', type=Path, default=Path('CHANGELOG.md'))
    parser.add_argument('--notes-out', type=Path)
    args = parser.parse_args()
    version, body = validate(args.tag, args.changelog)
    if args.notes_out:
        args.notes_out.parent.mkdir(parents=True, exist_ok=True)
        args.notes_out.write_text(f'# FAC LPR Engine {args.tag}\n\n{body}\n', encoding='utf-8')
    print(version)


if __name__ == '__main__':
    main()
