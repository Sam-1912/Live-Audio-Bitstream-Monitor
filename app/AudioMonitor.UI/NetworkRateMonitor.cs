using System.Net.NetworkInformation;
namespace AudioMonitor.UI;

// Machine-wide interface counters. This is deliberately NOT labelled per-process.
internal sealed class NetworkRateMonitor
{
    private long _rx, _tx;
    private DateTime _last = DateTime.UtcNow;
    public (double RxMbps, double TxMbps) Sample()
    {
        long rx = 0, tx = 0;
        foreach (var nic in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (nic.OperationalStatus != OperationalStatus.Up) continue;
            try {
                var s = nic.GetIPv4Statistics();
                rx += s.BytesReceived;
                tx += s.BytesSent;
            } catch { }
        }
        var now = DateTime.UtcNow;
        var seconds = Math.Max((now - _last).TotalSeconds, 0.001);
        double rxRate = Math.Max(0, rx - _rx) * 8.0 / seconds / 1_000_000.0;
        double txRate = Math.Max(0, tx - _tx) * 8.0 / seconds / 1_000_000.0;
        _rx = rx; _tx = tx; _last = now;
        return (rxRate, txRate);
    }
}
