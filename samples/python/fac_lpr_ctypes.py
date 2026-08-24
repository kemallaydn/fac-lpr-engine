from __future__ import annotations

import argparse
import ctypes as C
from pathlib import Path
from typing import Final

ABI_V1: Final[int] = 1
STATUS_OK: Final[int] = 0
STATUS_BUFFER_TOO_SMALL: Final[int] = 10
PIXEL_FORMAT_BGR8: Final[int] = 1


class VersionInfoV1(C.Structure):
    _fields_ = [
        ("struct_size", C.c_uint32),
        ("abi_version", C.c_uint32),
        ("semantic_major", C.c_uint32),
        ("semantic_minor", C.c_uint32),
        ("semantic_patch", C.c_uint32),
        ("abi_major", C.c_uint32),
    ]


class EngineConfigV1(C.Structure):
    _fields_ = [
        ("struct_size", C.c_uint32),
        ("abi_version", C.c_uint32),
        ("reserved_flags", C.c_uint32),
        ("reserved_zero", C.c_uint32),
    ]


class ImageViewV1(C.Structure):
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


class FacLprError(RuntimeError):
    def __init__(self, status: int, operation: str, detail: str = "") -> None:
        super().__init__(f"{operation} failed with status={status}" + (f": {detail}" if detail else ""))
        self.status = status


class NativeApi:
    def __init__(self, library_path: Path) -> None:
        self.lib = C.CDLL(str(library_path))
        self.lib.fac_lpr_get_version_v1.argtypes = [C.POINTER(VersionInfoV1)]
        self.lib.fac_lpr_get_version_v1.restype = C.c_int
        self.lib.fac_lpr_engine_create_v1.argtypes = [C.POINTER(EngineConfigV1), C.POINTER(C.c_void_p)]
        self.lib.fac_lpr_engine_create_v1.restype = C.c_int
        self.lib.fac_lpr_engine_recognize_v1.argtypes = [
            C.c_void_p,
            C.POINTER(ImageViewV1),
            C.c_void_p,
            C.c_size_t,
            C.POINTER(C.c_size_t),
        ]
        self.lib.fac_lpr_engine_recognize_v1.restype = C.c_int
        self.lib.fac_lpr_engine_destroy_v1.argtypes = [C.POINTER(C.c_void_p)]
        self.lib.fac_lpr_engine_destroy_v1.restype = C.c_int
        self.lib.fac_lpr_get_last_error_v1.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
        self.lib.fac_lpr_get_last_error_v1.restype = C.c_int

    def last_error(self) -> str:
        required = C.c_size_t()
        status = self.lib.fac_lpr_get_last_error_v1(None, 0, C.byref(required))
        if status != STATUS_BUFFER_TOO_SMALL or required.value == 0:
            return ""
        buffer = C.create_string_buffer(required.value)
        status = self.lib.fac_lpr_get_last_error_v1(buffer, len(buffer), C.byref(required))
        return buffer.value.decode("utf-8", errors="replace") if status == STATUS_OK else ""

    def check(self, status: int, operation: str) -> None:
        if status != STATUS_OK:
            raise FacLprError(status, operation, self.last_error())

    def version(self) -> VersionInfoV1:
        value = VersionInfoV1(C.sizeof(VersionInfoV1), ABI_V1, 0, 0, 0, 0)
        self.check(self.lib.fac_lpr_get_version_v1(C.byref(value)), "fac_lpr_get_version_v1")
        return value


class Engine:
    def __init__(self, api: NativeApi) -> None:
        self.api = api
        self.handle = C.c_void_p()
        config = EngineConfigV1(C.sizeof(EngineConfigV1), ABI_V1, 0, 0)
        api.check(api.lib.fac_lpr_engine_create_v1(C.byref(config), C.byref(self.handle)), "fac_lpr_engine_create_v1")
        if not self.handle.value:
            raise RuntimeError("native engine returned a null handle")

    def recognize_bgr(self, pixels: bytes | bytearray, width: int, height: int, stride: int) -> bytes:
        if not self.handle.value:
            raise RuntimeError("engine is closed")
        if width <= 0 or height <= 0 or stride < width * 3:
            raise ValueError("invalid BGR dimensions/stride")
        if len(pixels) < stride * height:
            raise ValueError("pixel buffer smaller than stride * height")

        owned = bytearray(pixels) if isinstance(pixels, bytes) else pixels
        array_type = C.c_uint8 * len(owned)
        pinned = array_type.from_buffer(owned)
        image = ImageViewV1(
            C.sizeof(ImageViewV1), ABI_V1,
            C.cast(pinned, C.POINTER(C.c_uint8)), len(owned),
            width, height, stride, PIXEL_FORMAT_BGR8,
        )
        required = C.c_size_t()
        status = self.api.lib.fac_lpr_engine_recognize_v1(
            self.handle, C.byref(image), None, 0, C.byref(required)
        )
        if status not in (STATUS_OK, STATUS_BUFFER_TOO_SMALL):
            self.api.check(status, "fac_lpr_engine_recognize_v1(size query)")
        if required.value == 0:
            return b""

        output = C.create_string_buffer(required.value)
        self.api.check(
            self.api.lib.fac_lpr_engine_recognize_v1(
                self.handle, C.byref(image), output, len(output), C.byref(required)
            ),
            "fac_lpr_engine_recognize_v1",
        )
        return bytes(output.raw[: required.value])

    def close(self) -> None:
        if not self.handle.value:
            return
        self.api.check(self.api.lib.fac_lpr_engine_destroy_v1(C.byref(self.handle)), "fac_lpr_engine_destroy_v1")
        if self.handle.value:
            raise RuntimeError("native destroy did not clear the handle")

    def __enter__(self) -> "Engine":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True, type=Path, help="Path to fac_lpr_engine.dll or libfac_lpr_engine.so")
    parser.add_argument("--iterations", type=int, default=10_000)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 1_000_000:
        raise ValueError("iterations must be between 1 and 1,000,000")

    api = NativeApi(args.library.resolve())
    version = api.version()
    print(f"FAC LPR semantic={version.semantic_major}.{version.semantic_minor}.{version.semantic_patch} abi={version.abi_major}")
    if version.abi_version != ABI_V1 or version.abi_major != 1:
        raise RuntimeError("unexpected ABI version")

    with Engine(api) as engine:
        try:
            engine.recognize_bgr(bytearray(3), 1, 1, 3)
        except FacLprError as error:
            print(f"recognize status mapping={error.status}")

    for index in range(args.iterations):
        with Engine(api):
            pass
        if (index + 1) % 1000 == 0:
            print(f"lifecycle {index + 1}/{args.iterations}")

    print("FAC LPR Python ctypes smoke PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
