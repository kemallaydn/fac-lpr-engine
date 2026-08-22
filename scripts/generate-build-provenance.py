#!/usr/bin/env python3
import argparse
import hashlib
import json
import pathlib
import platform
import subprocess


def first_line(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8").strip().splitlines()[0]


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_version(command):
    try:
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        return (result.stdout or result.stderr).strip().splitlines()[0]
    except Exception:
        return "unavailable"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--source-date-epoch", type=int, required=True)
    parser.add_argument("--vcpkg-baseline", type=pathlib.Path, required=True)
    parser.add_argument("--onnx-version", type=pathlib.Path, required=True)
    parser.add_argument("--triplet", required=True)
    parser.add_argument("--artifact", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    payload = {
        "schemaVersion": 1,
        "source": {
            "commit": args.source_commit,
            "sourceDateEpoch": args.source_date_epoch,
        },
        "buildEnvironment": {
            "os": platform.system(),
            "osRelease": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "cmake": command_version(["cmake", "--version"]),
            "compiler": command_version(["c++", "--version"]),
            "triplet": args.triplet,
        },
        "dependencies": {
            "vcpkgBaseline": first_line(args.vcpkg_baseline),
            "onnxRuntimeVersion": first_line(args.onnx_version),
        },
        "reproducibleBuild": {
            "enabled": True,
            "archiveMetadataNormalized": True,
            "sourcePathsRemapped": True,
        },
    }
    if args.artifact:
        payload["artifact"] = {
            "name": args.artifact.name,
            "sizeBytes": args.artifact.stat().st_size,
            "sha256": sha256(args.artifact),
        }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
