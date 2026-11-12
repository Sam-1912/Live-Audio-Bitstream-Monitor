using System.Diagnostics;
using System.Text.Json;

namespace AudioMonitor.UI;

public sealed record MediaProbeResult(
    string Codec, string? Profile, int SampleRate, int Channels,
    long? BitRate, string BitrateBasis);

public static class MediaProbe
{
    // Inspects an explicitly supplied accessible media file using ffprobe.
    // It does not inspect arbitrary protected app playback sessions.
    public static async Task<MediaProbeResult?> InspectFileAsync(
        string ffprobePath, string mediaPath, CancellationToken cancellationToken = default)
    {
        var psi = new ProcessStartInfo(ffprobePath) {
            UseShellExecute = false, RedirectStandardOutput = true,
            RedirectStandardError = true, CreateNoWindow = true
        };
        psi.ArgumentList.Add("-v"); psi.ArgumentList.Add("error");
        psi.ArgumentList.Add("-select_streams"); psi.ArgumentList.Add("a:0");
        psi.ArgumentList.Add("-show_entries");
        psi.ArgumentList.Add("stream=codec_name,profile,sample_rate,channels,bit_rate");
        psi.ArgumentList.Add("-of"); psi.ArgumentList.Add("json");
        psi.ArgumentList.Add(mediaPath);

        using var p = Process.Start(psi) ?? throw new InvalidOperationException("Could not start ffprobe.");
        string json = await p.StandardOutput.ReadToEndAsync(cancellationToken);
        await p.WaitForExitAsync(cancellationToken);
        if (p.ExitCode != 0) throw new InvalidOperationException(await p.StandardError.ReadToEndAsync(cancellationToken));
        using var doc = JsonDocument.Parse(json);
        if (!doc.RootElement.TryGetProperty("streams", out var streams) || streams.GetArrayLength() == 0)
            return null;
        var s = streams[0];
        string Get(string k) => s.TryGetProperty(k, out var v) ? v.ToString() : "";
        long.TryParse(Get("bit_rate"), out var br);
        int.TryParse(Get("sample_rate"), out var sr);
        int.TryParse(Get("channels"), out var ch);
        return new MediaProbeResult(Get("codec_name"), Get("profile"), sr, ch,
            br > 0 ? br : null, "ffprobe stream metadata; not a live protected-app measurement");
    }
}
