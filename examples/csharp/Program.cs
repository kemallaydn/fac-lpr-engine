using System.Runtime.InteropServices;

const uint AbiV1 = 1;
const int StatusOk = 0;
const int StatusConfigurationError = 1;
const int PixelFormatBgr8 = 1;

var version = new VersionInfo
{
    StructSize = (uint)Marshal.SizeOf<VersionInfo>(),
    AbiVersion = AbiV1,
};
Check(Native.fac_lpr_get_version_v1(ref version), "get_version");
if (version.AbiMajor != AbiV1)
{
    throw new InvalidOperationException($"Unexpected ABI major {version.AbiMajor}");
}

for (var iteration = 0; iteration < 250; iteration++)
{
    var config = new EngineConfig
    {
        StructSize = (uint)Marshal.SizeOf<EngineConfig>(),
        AbiVersion = AbiV1,
    };
    var handle = IntPtr.Zero;
    Check(Native.fac_lpr_engine_create_v1(ref config, out handle), "engine_create");
    if (handle == IntPtr.Zero)
    {
        throw new InvalidOperationException("engine_create returned null");
    }

    var pixels = new byte[] { 0, 0, 0 };
    var pin = GCHandle.Alloc(pixels, GCHandleType.Pinned);
    try
    {
        var image = new ImageView
        {
            StructSize = (uint)Marshal.SizeOf<ImageView>(),
            AbiVersion = AbiV1,
            Data = pin.AddrOfPinnedObject(),
            DataSize = (nuint)pixels.Length,
            Width = 1,
            Height = 1,
            StrideBytes = 3,
            PixelFormat = PixelFormatBgr8,
        };
        var status = Native.fac_lpr_engine_recognize_v1(
            handle, ref image, IntPtr.Zero, 0, out _);
        if (status != StatusConfigurationError)
        {
            throw new InvalidOperationException($"Unexpected recognize status {status}");
        }
    }
    finally
    {
        pin.Free();
    }

    Check(Native.fac_lpr_engine_destroy_v1(ref handle), "engine_destroy");
    if (handle != IntPtr.Zero)
    {
        throw new InvalidOperationException("engine_destroy did not clear handle");
    }
}

Console.WriteLine($"P/Invoke smoke ok: {version.SemanticMajor}.{version.SemanticMinor}.{version.SemanticPatch}, ABI {version.AbiMajor}");

static void Check(int status, string operation)
{
    if (status != StatusOk)
    {
        throw new InvalidOperationException($"{operation} failed with status {status}");
    }
}

[StructLayout(LayoutKind.Sequential)]
struct VersionInfo
{
    public uint StructSize;
    public uint AbiVersion;
    public uint SemanticMajor;
    public uint SemanticMinor;
    public uint SemanticPatch;
    public uint AbiMajor;
}

[StructLayout(LayoutKind.Sequential)]
struct EngineConfig
{
    public uint StructSize;
    public uint AbiVersion;
    public uint ReservedFlags;
    public uint ReservedZero;
}

[StructLayout(LayoutKind.Sequential)]
struct ImageView
{
    public uint StructSize;
    public uint AbiVersion;
    public IntPtr Data;
    public nuint DataSize;
    public uint Width;
    public uint Height;
    public uint StrideBytes;
    public int PixelFormat;
}

static partial class Native
{
    private const string Library = "fac_lpr_engine";

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int fac_lpr_get_version_v1(ref VersionInfo version);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int fac_lpr_engine_create_v1(ref EngineConfig config, out IntPtr handle);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int fac_lpr_engine_recognize_v1(
        IntPtr handle,
        ref ImageView image,
        IntPtr outputBuffer,
        nuint outputCapacity,
        out nuint requiredOutputSize);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    public static extern int fac_lpr_engine_destroy_v1(ref IntPtr handle);
}
