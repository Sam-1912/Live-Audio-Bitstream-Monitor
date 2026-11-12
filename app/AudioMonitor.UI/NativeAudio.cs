using System.IO;
using System.Runtime.InteropServices;
namespace AudioMonitor.UI;

internal static class NativeAudio
{
    private static readonly string LibraryPath = Path.Combine(AppContext.BaseDirectory, "AudioMonitor.Native.dll");

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void MetricsCallback(double rmsDb, double peakDb,
        uint sampleRate, uint channels, ulong frames, double estimatedBitrateBps,
        double leftRmsDb, double rightRmsDb,
        double band0Db, double band1Db, double band2Db, double band3Db,
        double band4Db, double band5Db, double band6Db, double band7Db);

    internal static int AudioMonitor_Start(MetricsCallback callback)
    {
        if (!File.Exists(LibraryPath))
        {
            throw new FileNotFoundException(
                "AudioMonitor.Native.dll was not found in the app output folder. Build the native AudioMonitor.Native project first and then run the UI again.",
                LibraryPath);
        }

        return StartNative(callback);
    }

    internal static void AudioMonitor_Stop()
    {
        if (File.Exists(LibraryPath))
        {
            StopNative();
        }
    }

    [DllImport("AudioMonitor.Native.dll", EntryPoint = "AudioMonitor_Start", CallingConvention = CallingConvention.StdCall)]
    private static extern int StartNative(MetricsCallback callback);

    [DllImport("AudioMonitor.Native.dll", EntryPoint = "AudioMonitor_Stop", CallingConvention = CallingConvention.StdCall)]
    private static extern void StopNative();
}
