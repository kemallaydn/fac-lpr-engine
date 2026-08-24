#!/usr/bin/env python3
import argparse
import re
from pathlib import Path

SEMVER = re.compile(r'^v(?P<version>(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$')
PROJECT_VERSION = re.compile(r'project\s*\(.*?VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', re.S | re.I)


def changelog_section(text: str, version: str) -> str:
    pattern = re.compile(rf'^## \[{re.escape(version)}\](?:\s+-\s+[^\n]+)?\s*$\n(?P<body>.*?)(?=^## \[|\Z)', re.M | re.S)
    match = pattern.search(text)
    if not match:
        raise ValueError(f'CHANGELOG.md has no [{version}] section')
    return match.group('body').strip()


def validate(tag: str, changelog: Path, project_file: Path | None = None) -> tuple[str, str]:
    match = SEMVER.fullmatch(tag)
    if not match:
        raise ValueError('release tag must be semantic version format vMAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]')
    version = match.group('version')
    body = changelog_section(changelog.read_text(encoding='utf-8'), version)
    if '### Breaking ABI' not in body:
        raise ValueError('release changelog section must explicitly contain a Breaking ABI subsection')
    if project_file is not None:
        project_match = PROJECT_VERSION.search(project_file.read_text(encoding='utf-8'))
        if not project_match:
            raise ValueError('could not resolve project VERSION from CMakeLists.txt')
        if project_match.group(1) != version:
            raise ValueError(f'tag version {version} does not match project version {project_match.group(1)}')
    return version, body


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    parser.add_argument('--changelog', type=Path, default=Path('CHANGELOG.md'))
    parser.add_argument('--project-file', type=Path, default=Path('CMakeLists.txt'))
    parser.add_argument('--notes-out', type=Path)
    args = parser.parse_args()
    version, body = validate(args.tag, args.changelog, args.project_file)
    if args.notes_out:
        args.notes_out.parent.mkdir(parents=True, exist_ok=True)
        args.notes_out.write_text(f'# FAC LPR Engine {args.tag}\n\n{body}\n', encoding='utf-8')
    print(version)


if __name__ == '__main__':
    main()
