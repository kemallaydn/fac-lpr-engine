#!/usr/bin/env python3
import argparse
import gzip
import os
import pathlib
import tarfile
import time
import zipfile


def normalized_epoch(value: int) -> int:
    # ZIP cannot represent timestamps before 1980-01-01.
    return max(value, 315532800)


def files(root: pathlib.Path):
    return sorted((path for path in root.rglob("*") if path.is_file()), key=lambda p: p.as_posix())


def write_tar_gz(root: pathlib.Path, output: pathlib.Path, epoch: int):
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as gz:
            with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for path in files(root):
                    relative = path.relative_to(root.parent).as_posix()
                    info = archive.gettarinfo(str(path), arcname=relative)
                    info.mtime = epoch
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    with path.open("rb") as handle:
                        archive.addfile(info, handle)


def write_zip(root: pathlib.Path, output: pathlib.Path, epoch: int):
    output.parent.mkdir(parents=True, exist_ok=True)
    timestamp = time.gmtime(normalized_epoch(epoch))[:6]
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files(root):
            relative = path.relative_to(root.parent).as_posix()
            info = zipfile.ZipInfo(relative, date_time=timestamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100644 & 0xFFFF) << 16
            if os.access(path, os.X_OK):
                info.external_attr = (0o100755 & 0xFFFF) << 16
            archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--epoch", type=int, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    if not root.is_dir():
        parser.error(f"root is not a directory: {root}")
    if args.output.name.endswith(".tar.gz"):
        write_tar_gz(root, args.output, args.epoch)
    elif args.output.suffix.lower() == ".zip":
        write_zip(root, args.output, args.epoch)
    else:
        parser.error("output must end in .tar.gz or .zip")


if __name__ == "__main__":
    main()
