#!/usr/bin/env python3
import argparse
import pathlib
import sys


def read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace").strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vcpkg-installed", type=pathlib.Path, required=True)
    parser.add_argument("--triplet", required=True)
    parser.add_argument("--onnx-root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    share = args.vcpkg_installed / args.triplet / "share"
    if not share.is_dir():
        raise SystemExit(f"vcpkg share directory not found: {share}")

    notices = []
    for copyright_file in sorted(share.glob("*/copyright")):
        package = copyright_file.parent.name
        text = read_text(copyright_file)
        if text:
            notices.append((f"vcpkg:{package}", text))

    onnx_candidates = [
        args.onnx_root / "LICENSE",
        args.onnx_root / "LICENSE.txt",
        args.onnx_root / "ThirdPartyNotices.txt",
    ]
    onnx_files = [path for path in onnx_candidates if path.is_file()]
    if not onnx_files:
        raise SystemExit("ONNX Runtime license/notice file not found under supplied root")
    for path in onnx_files:
        notices.append((f"onnxruntime:{path.name}", read_text(path)))

    if not notices:
        raise SystemExit("no third-party notices discovered")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("FAC LPR Engine - Third-Party Notices\n")
        handle.write("====================================\n\n")
        handle.write(
            "This file is generated from the dependency installation used to build the release.\n"
            "Model files are intentionally not part of the engine binary/package and their licenses\n"
            "must travel with the separately provisioned model artifacts.\n\n"
        )
        for name, text in notices:
            handle.write(f"--- {name} ---\n{text}\n\n")

    print(f"wrote {len(notices)} third-party notice entries to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
