#!/usr/bin/env python3
"""Minimal FAC LPR Engine v1 consumer using only ctypes and the public C ABI."""

from __future__ import annotations

import argparse
import ctypes as C
import os
import pathlib
import sys

FAC_LPR_ABI_VERSION_V1 = 1
FAC_LPR_STATUS_OK = 0
FAC_LPR_STATUS_CONFIGURATION_ERROR = 1
FAC_LPR_STATUS_BUFFER_TOO_SMALL = 10
FAC_LPR_PIXEL_FORMAT_BGR8 = 1


class VersionInfo(C.Structure):
    _fields_ = [
        ("struct_size", C.c_uint32),
        ("abi_version", C.c_uint32),
        ("semantic_major", C.c_uint32),
        ("semantic_minor", C.c_uint32),
        ("semantic_patch", C.c_uint32),
        ("abi_major", C.c_uint32),
    ]


class EngineConfig(C.Structure):
    _fields_ = [
        ("struct_size", C.c_uint32),
        ("abi_version", C.c_uint32),
        ("reserved_flags", C.c_uint32),
        ("reserved_zero", C.c_uint32),
    ]


class ImageView(C.Structure):
    _fields_ = [
        ("struct_size", C.c_uint32),
        ("abi_version", C.c_uint32),
        ("data", C.POINTER(C.c_uint8)),
        ("data_size", C.c_size_t),
        ("width", C.c_uint32),
        ("height", C.c_uint32),
        ("stride_bytes", C.c_uint32),
        ("pixel_format", C.c_int),
    ]


class FacLpr:
    def __init__(self, library_path: pathlib.Path) -> None:
        self._lib = C.CDLL(os.fspath(library_path))
        self._lib.fac_lpr_get_version_v1.argtypes = [C.POINTER(VersionInfo)]
        self._lib.fac_lpr_get_version_v1.restype = C.c_int
        self._lib.fac_lpr_engine_create_v1.argtypes = [
            C.POINTER(EngineConfig),
            C.POINTER(C.c_void_p),
        ]
        self._lib.fac_lpr_engine_create_v1.restype = C.c_int
        self._lib.fac_lpr_engine_recognize_v1.argtypes = [
            C.c_void_p,
            C.POINTER(ImageView),
            C.c_void_p,
            C.c_size_t,
            C.POINTER(C.c_size_t),
        ]
        self._lib.fac_lpr_engine_recognize_v1.restype = C.c_int
        self._lib.fac_lpr_engine_destroy_v1.argtypes = [C.POINTER(C.c_void_p)]
        self._lib.fac_lpr_engine_destroy_v1.restype = C.c_int
        self._lib.fac_lpr_get_last_error_v1.argtypes = [
            C.POINTER(C.c_char),
            C.c_size_t,
            C.POINTER(C.c_size_t),
        ]
        self._lib.fac_lpr_get_last_error_v1.restype = C.c_int

    def version(self) -> VersionInfo:
        value = VersionInfo(C.sizeof(VersionInfo), FAC_LPR_ABI_VERSION_V1, 0, 0, 0, 0)
        status = self._lib.fac_lpr_get_version_v1(C.byref(value))
        self._check(status, "get_version")
        return value

    def create(self) -> C.c_void_p:
        config = EngineConfig(C.sizeof(EngineConfig), FAC_LPR_ABI_VERSION_V1, 0, 0)
        handle = C.c_void_p()
        status = self._lib.fac_lpr_engine_create_v1(C.byref(config), C.byref(handle))
        self._check(status, "engine_create")
        if not handle.value:
            raise RuntimeError("engine_create returned a null handle")
        return handle

    def recognize_bgr(self, handle: C.c_void_p, pixels: bytes, width: int, height: int) -> int:
        stride = width * 3
        if len(pixels) < stride * height:
            raise ValueError("pixel buffer is smaller than width*height*3")
        storage = (C.c_uint8 * len(pixels)).from_buffer_copy(pixels)
        image = ImageView(
            C.sizeof(ImageView),
            FAC_LPR_ABI_VERSION_V1,
            C.cast(storage, C.POINTER(C.c_uint8)),
            len(pixels),
            width,
            height,
            stride,
            FAC_LPR_PIXEL_FORMAT_BGR8,
        )
        required = C.c_size_t(0)
        return int(
            self._lib.fac_lpr_engine_recognize_v1(
                handle, C.byref(image), None, 0, C.byref(required)
            )
        )

    def destroy(self, handle: C.c_void_p) -> None:
        status = self._lib.fac_lpr_engine_destroy_v1(C.byref(handle))
        self._check(status, "engine_destroy")
        if handle.value:
            raise RuntimeError("engine_destroy did not clear the handle")

    def last_error(self) -> str:
        required = C.c_size_t(0)
        status = self._lib.fac_lpr_get_last_error_v1(None, 0, C.byref(required))
        if status != FAC_LPR_STATUS_BUFFER_TOO_SMALL or required.value == 0:
            raise RuntimeError(f"last_error size query failed with status {status}")
        buffer = C.create_string_buffer(required.value)
        status = self._lib.fac_lpr_get_last_error_v1(buffer, len(buffer), C.byref(required))
        if status != FAC_LPR_STATUS_OK:
            raise RuntimeError(f"last_error retrieval failed with status {status}")
        return buffer.value.decode("utf-8", errors="replace")

    def _check(self, status: int, operation: str) -> None:
        if status != FAC_LPR_STATUS_OK:
            raise RuntimeError(f"{operation} failed with status {status}: {self.last_error()}")


def run_smoke(library: pathlib.Path, iterations: int) -> None:
    api = FacLpr(library)
    version = api.version()
    if version.abi_major != FAC_LPR_ABI_VERSION_V1:
        raise RuntimeError(f"unexpected ABI major: {version.abi_major}")

    # Current v1 create() intentionally creates a handle without a configured
    # recognition pipeline. Passing a real image buffer must therefore cross
    # the ABI safely and return the documented configuration error.
    one_bgr_pixel = bytes((0, 0, 0))
    for _ in range(iterations):
        handle = api.create()
        status = api.recognize_bgr(handle, one_bgr_pixel, 1, 1)
        if status != FAC_LPR_STATUS_CONFIGURATION_ERROR:
            api.destroy(handle)
            raise RuntimeError(f"unexpected recognize status: {status}")
        if not api.last_error():
            api.destroy(handle)
            raise RuntimeError("recognize failure did not expose error detail")
        api.destroy(handle)

    print(
        f"ctypes smoke ok: engine={version.semantic_major}."
        f"{version.semantic_minor}.{version.semantic_patch} abi={version.abi_major} "
        f"iterations={iterations}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=pathlib.Path, help="path to fac_lpr_engine DLL/SO/dylib")
    parser.add_argument("--iterations", type=int, default=250)
    args = parser.parse_args()
    if args.iterations <= 0:
        parser.error("--iterations must be positive")
    run_smoke(args.library.resolve(), args.iterations)
    return 0


if __name__ == "__main__":
    sys.exit(main())
