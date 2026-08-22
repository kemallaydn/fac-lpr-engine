using System.Runtime.InteropServices;
using System.Text;

namespace FacLpr.Sample;

internal enum FacLprStatus : int
{
    Ok = 0,
    ConfigurationError = 1,
    ModelLoadError = 2,
    InferenceError = 3,
    InvalidImage = 4,
    ProviderError = 5,
    Cancelled = 6,
    Timeout = 7,
    ResourceExhausted = 8,
    InternalError = 9,
    BufferTooSmall = 10
}

internal enum FacLprPixelFormat : int
{
    Gray8 = 0,
    Bgr8 = 1,
    Rgb8 = 2
}

[StructLayout(LayoutKind.Sequential)]
internal struct FacLprVersionInfoV1
{
    public uint StructSize;
    public uint AbiVersion;
    public uint SemanticMajor;
    public uint SemanticMinor;
    public uint SemanticPatch;
    public uint AbiMajor;
}

[StructLayout(LayoutKind.Sequential)]
internal struct FacLprEngineConfigV1
{
    public uint StructSize;
    public uint AbiVersion;
    public uint ReservedFlags;
    public uint ReservedZero;
}

[StructLayout(LayoutKind.Sequential)]
internal struct FacLprImageViewV1
{
    public uint StructSize;
    public uint AbiVersion;
    public IntPtr Data;
    public UIntPtr DataSize;
    public uint Width;
    public uint Height;
    public uint StrideBytes;
    public FacLprPixelFormat PixelFormat;
}

internal static class FacLprNative
{
    internal const uint AbiVersionV1 = 1;
    private const string LibraryName = "fac_lpr_engine";

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern FacLprStatus fac_lpr_get_version_v1(ref FacLprVersionInfoV1 version);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern FacLprStatus fac_lpr_engine_create_v1(
        ref FacLprEngineConfigV1 config,
        out IntPtr handle);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern FacLprStatus fac_lpr_engine_recognize_v1(
        IntPtr handle,
        ref FacLprImageViewV1 image,
        IntPtr outputBuffer,
        UIntPtr outputCapacity,
        out UIntPtr requiredOutputSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern FacLprStatus fac_lpr_engine_destroy_v1(ref IntPtr handle);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    internal static extern FacLprStatus fac_lpr_get_last_error_v1(
        IntPtr buffer,
        UIntPtr bufferCapacity,
        out UIntPtr requiredSize);

    internal static string ReadLastError()
    {
        var status = fac_lpr_get_last_error_v1(IntPtr.Zero, UIntPtr.Zero, out var required);
        if (status != FacLprStatus.BufferTooSmall || required == UIntPtr.Zero)
        {
            return string.Empty;
        }

        var length = checked((int)required.ToUInt64());
        var bytes = new byte[length];
        var pin = GCHandle.Alloc(bytes, GCHandleType.Pinned);
        try
        {
            status = fac_lpr_get_last_error_v1(
                pin.AddrOfPinnedObject(),
                (UIntPtr)bytes.Length,
                out _);
            if (status != FacLprStatus.Ok)
            {
                return string.Empty;
            }
            var terminator = Array.IndexOf(bytes, (byte)0);
            if (terminator < 0)
            {
                terminator = bytes.Length;
            }
            return Encoding.UTF8.GetString(bytes, 0, terminator);
        }
        finally
        {
            pin.Free();
        }
    }
}

internal sealed class FacLprException : Exception
{
    internal FacLprException(FacLprStatus status, string operation, string nativeDetail)
        : base(string.IsNullOrWhiteSpace(nativeDetail)
            ? $"{operation} failed with native status {status}."
            : $"{operation} failed with native status {status}: {nativeDetail}")
    {
        Status = status;
    }

    internal FacLprStatus Status { get; }
}

internal sealed class FacLprEngine : IDisposable
{
    private IntPtr _handle;

    internal FacLprEngine()
    {
        var config = new FacLprEngineConfigV1
        {
            StructSize = (uint)Marshal.SizeOf<FacLprEngineConfigV1>(),
            AbiVersion = FacLprNative.AbiVersionV1
        };
        var status = FacLprNative.fac_lpr_engine_create_v1(ref config, out _handle);
        ThrowIfFailed(status, "fac_lpr_engine_create_v1");
        if (_handle == IntPtr.Zero)
        {
            throw new InvalidOperationException("Native engine returned a null handle after successful creation.");
        }
    }

    internal byte[] RecognizeBgr(byte[] pixels, uint width, uint height, uint strideBytes)
    {
        ObjectDisposedException.ThrowIf(_handle == IntPtr.Zero, this);
        ArgumentNullException.ThrowIfNull(pixels);
        if (width == 0 || height == 0 || strideBytes < checked(width * 3U))
        {
            throw new ArgumentOutOfRangeException(nameof(width), "Invalid BGR image dimensions/stride.");
        }
        var requiredBytes = checked((ulong)strideBytes * height);
        if ((ulong)pixels.LongLength < requiredBytes)
        {
            throw new ArgumentException("Pixel buffer is smaller than stride * height.", nameof(pixels));
        }

        var imagePin = GCHandle.Alloc(pixels, GCHandleType.Pinned);
        try
        {
            var image = new FacLprImageViewV1
            {
                StructSize = (uint)Marshal.SizeOf<FacLprImageViewV1>(),
                AbiVersion = FacLprNative.AbiVersionV1,
                Data = imagePin.AddrOfPinnedObject(),
                DataSize = (UIntPtr)pixels.LongLength,
                Width = width,
                Height = height,
                StrideBytes = strideBytes,
                PixelFormat = FacLprPixelFormat.Bgr8
            };

            var status = FacLprNative.fac_lpr_engine_recognize_v1(
                _handle,
                ref image,
                IntPtr.Zero,
                UIntPtr.Zero,
                out var required);
            if (status != FacLprStatus.BufferTooSmall && status != FacLprStatus.Ok)
            {
                ThrowIfFailed(status, "fac_lpr_engine_recognize_v1(size query)");
            }
            if (required == UIntPtr.Zero)
            {
                return Array.Empty<byte>();
            }

            var output = new byte[checked((int)required.ToUInt64())];
            var outputPin = GCHandle.Alloc(output, GCHandleType.Pinned);
            try
            {
                status = FacLprNative.fac_lpr_engine_recognize_v1(
                    _handle,
                    ref image,
                    outputPin.AddrOfPinnedObject(),
                    (UIntPtr)output.Length,
                    out var exactRequired);
                ThrowIfFailed(status, "fac_lpr_engine_recognize_v1");
                if (exactRequired.ToUInt64() > (ulong)output.LongLength)
                {
                    throw new InvalidOperationException("Native result size exceeded the caller-owned buffer.");
                }
                return output;
            }
            finally
            {
                outputPin.Free();
            }
        }
        finally
        {
            imagePin.Free();
        }
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
        {
            return;
        }
        var handle = _handle;
        var status = FacLprNative.fac_lpr_engine_destroy_v1(ref handle);
        _handle = IntPtr.Zero;
        ThrowIfFailed(status, "fac_lpr_engine_destroy_v1");
        GC.SuppressFinalize(this);
    }

    internal static FacLprVersionInfoV1 GetVersion()
    {
        var version = new FacLprVersionInfoV1
        {
            StructSize = (uint)Marshal.SizeOf<FacLprVersionInfoV1>(),
            AbiVersion = FacLprNative.AbiVersionV1
        };
        var status = FacLprNative.fac_lpr_get_version_v1(ref version);
        ThrowIfFailed(status, "fac_lpr_get_version_v1");
        return version;
    }

    private static void ThrowIfFailed(FacLprStatus status, string operation)
    {
        if (status == FacLprStatus.Ok)
        {
            return;
        }
        throw new FacLprException(status, operation, FacLprNative.ReadLastError());
    }
}
