using System.Diagnostics;
using FacLpr.Sample;

var iterations = 10_000;
for (var index = 0; index < args.Length; ++index)
{
    if (args[index] == "--iterations" && index + 1 < args.Length && int.TryParse(args[index + 1], out var parsed))
    {
        iterations = parsed;
        ++index;
    }
}
if (iterations <= 0 || iterations > 1_000_000)
{
    throw new ArgumentOutOfRangeException(nameof(iterations), "iterations must be between 1 and 1,000,000");
}

var version = FacLprEngine.GetVersion();
Console.WriteLine($"FAC LPR semantic={version.SemanticMajor}.{version.SemanticMinor}.{version.SemanticPatch} abi={version.AbiMajor}");
if (version.AbiVersion != FacLprNative.AbiVersionV1 || version.AbiMajor != 1)
{
    throw new InvalidOperationException("Unexpected native ABI version.");
}

using (var engine = new FacLprEngine())
{
    try
    {
        _ = engine.RecognizeBgr(new byte[3], 1, 1, 3);
    }
    catch (FacLprException error)
    {
        Console.WriteLine($"Recognize boundary status mapping: {error.Status}");
    }
}

GC.Collect();
GC.WaitForPendingFinalizers();
GC.Collect();
using var process = Process.GetCurrentProcess();
process.Refresh();
var baselineWorkingSet = process.WorkingSet64;

for (var iteration = 0; iteration < iterations; ++iteration)
{
    using var engine = new FacLprEngine();
    if ((iteration + 1) % 1_000 == 0)
    {
        Console.WriteLine($"lifecycle {iteration + 1}/{iterations}");
    }
}

GC.Collect();
GC.WaitForPendingFinalizers();
GC.Collect();
process.Refresh();
var finalWorkingSet = process.WorkingSet64;
var delta = finalWorkingSet - baselineWorkingSet;
Console.WriteLine($"working-set baseline={baselineWorkingSet} final={finalWorkingSet} delta={delta}");

// This is intentionally a broad smoke guard, not a substitute for native long-run/ASan tests.
// It catches catastrophic interop handle leaks without making allocator behavior a brittle ABI test.
const long catastrophicGrowthBytes = 128L * 1024L * 1024L;
if (delta > catastrophicGrowthBytes)
{
    throw new InvalidOperationException($"Interop lifecycle working-set growth exceeded {catastrophicGrowthBytes} bytes.");
}

Console.WriteLine("FAC LPR .NET interop smoke PASS");
