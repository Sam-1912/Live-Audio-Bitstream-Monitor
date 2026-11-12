using System.Drawing;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Forms = System.Windows.Forms;
using WpfBrushes = System.Windows.Media.Brushes;

namespace AudioMonitor.UI;

public partial class MainWindow : Window
{
    private readonly NetworkRateMonitor _network = new();
    private readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromSeconds(1) };
    private readonly DispatcherTimer _clockTimer = new() { Interval = TimeSpan.FromSeconds(1) };
    private readonly Forms.NotifyIcon _trayIcon;
    private readonly Forms.ContextMenuStrip _trayMenu;
    private readonly System.Windows.Controls.ProgressBar[] _spectrumBars;
    private NativeAudio.MetricsCallback? _callback;
    private double _rms = -120, _peak = -120;
    private double _renderBitrateBps;
    private uint _sampleRate, _channels;
    private ulong _frames;
    private long _lastAudioUiUpdate;
    private bool _started;

    public MainWindow()
    {
        InitializeComponent();
        _spectrumBars = [Band0Bar, Band1Bar, Band2Bar, Band3Bar, Band4Bar, Band5Bar, Band6Bar, Band7Bar];

        var iconUri = new Uri("pack://application:,,,/AudioMonitor.UI;component/Assets/AudioMonitor.png");
        Icon = BitmapFrame.Create(iconUri);

        _trayIcon = new Forms.NotifyIcon();
        _trayMenu = new Forms.ContextMenuStrip();
        var restoreItem = new Forms.ToolStripMenuItem("Restore");
        restoreItem.Click += (_, _) => RestoreFromTray();
        var exitItem = new Forms.ToolStripMenuItem("Exit");
        exitItem.Click += (_, _) => Close();
        _trayMenu.Items.Add(restoreItem);
        _trayMenu.Items.Add(exitItem);
        _trayIcon.ContextMenuStrip = _trayMenu;
        _trayIcon.Text = "Audio Monitor";
        _trayIcon.DoubleClick += (_, _) => RestoreFromTray();

        var trayIconPath = Path.Combine(AppContext.BaseDirectory, "Assets", "AudioMonitor.ico");
        if (File.Exists(trayIconPath))
        {
            _trayIcon.Icon = new Icon(trayIconPath);
        }

        KeyDown += OnKeyDown;
        _timer.Tick += (_, _) => RefreshNetwork();
        _clockTimer.Tick += (_, _) => ClockText.Text = DateTime.Now.ToString("HH:mm:ss");
        StateChanged += OnWindowStateChanged;
        Closed += (_, _) => StopCapture();
        Closed += (_, _) => _trayIcon.Dispose();
        _clockTimer.Start();
    }

    private void OnKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (e.Key == Key.Enter && !_started)
        {
            Start_Click(this, new RoutedEventArgs());
            e.Handled = true;
            return;
        }

        if ((e.Key == Key.Escape || e.Key == Key.Space) && _started)
        {
            Stop_Click(this, new RoutedEventArgs());
            e.Handled = true;
        }
    }

    private void OnWindowStateChanged(object? sender, EventArgs e)
    {
        if (WindowState == WindowState.Minimized && MinimizeToTrayCheckBox.IsChecked == true)
        {
            Hide();
            _trayIcon.Visible = true;
            _trayIcon.ShowBalloonTip(1200, "Audio Monitor", "Running in the background.", Forms.ToolTipIcon.Info);
        }
    }

    private void RestoreFromTray()
    {
        _trayIcon.Visible = false;
        Show();
        WindowState = WindowState.Normal;
        Activate();
    }

    private void Start_Click(object sender, RoutedEventArgs e)
    {
        if (_started) return;
        _callback = (rms, peak, rate, channels, frames, estimatedBitrateBps, leftRms, rightRms,
            band0, band1, band2, band3, band4, band5, band6, band7) => {
            long now = Stopwatch.GetTimestamp();
            if (now - Interlocked.Read(ref _lastAudioUiUpdate) < Stopwatch.Frequency / 30) return;
            Interlocked.Exchange(ref _lastAudioUiUpdate, now);

            _rms = rms; _peak = peak; _sampleRate = rate; _channels = channels; _frames = frames; _renderBitrateBps = estimatedBitrateBps;
            double[] bands = [band0, band1, band2, band3, band4, band5, band6, band7];
            Dispatcher.BeginInvoke(() => {
                RmsText.Text = $"{_rms:F1}";
                PeakText.Text = $"{_peak:F1}";
                RenderBitrateText.Text = $"{_renderBitrateBps / 1_000_000.0:F2} Mbps";
                FormatText.Text = $"{_sampleRate:N0} Hz · {_channels} channel(s) · Windows audio-engine mix format";
                LeftLevelBar.Value = Math.Clamp(leftRms + 60.0, 0.0, 60.0);
                RightLevelBar.Value = Math.Clamp(rightRms + 60.0, 0.0, 60.0);
                for (int i = 0; i < _spectrumBars.Length; i++)
                    _spectrumBars[i].Value = Math.Clamp(bands[i] + 60.0, 0.0, 60.0);
                bool hasSignal = Math.Max(leftRms, rightRms) > -90.0;
                LevelHintText.Text = hasSignal ? "Signal active" : "Digital silence";
                LevelHintText.Foreground = hasSignal ? WpfBrushes.MediumSpringGreen : WpfBrushes.SlateGray;
            });
        };
        try {
            int result = NativeAudio.AudioMonitor_Start(_callback);
            if (result != 0)
            {
                string reason = result < -1000
                    ? Marshal.GetExceptionForHR(result)?.Message ?? "Windows did not provide an error description."
                    : $"Native capture start returned {result}.";
                throw new InvalidOperationException($"WASAPI capture could not start (0x{result:X8}): {reason}");
            }
            _started = true;
            StartButton.IsEnabled = false; StopButton.IsEnabled = true;
            StatusText.Text = "Monitoring Windows default Multimedia output";
            CaptureStatusDot.Fill = WpfBrushes.MediumSpringGreen;
            FooterText.Text = "WASAPI loopback active · Play audio through the default Windows output";
            _network.Sample();
            _timer.Start();
        } catch (Exception ex) {
            StatusText.Text = "Capture failed";
            FooterText.Text = ex.Message;
            if (ex is FileNotFoundException)
            {
                FooterText.Text = "Build AudioMonitor.Native first, then start again. The app is waiting for the native WASAPI DLL.";
            }
        }
    }

    private void Stop_Click(object sender, RoutedEventArgs e) => StopCapture();

    private void StopCapture()
    {
        _timer.Stop();
        if (_started) {
            try { NativeAudio.AudioMonitor_Stop(); } catch { }
        }
        _started = false;
        if (!IsLoaded) return;
        StartButton.IsEnabled = true; StopButton.IsEnabled = false;
        StatusText.Text = "Stopped";
        CaptureStatusDot.Fill = WpfBrushes.SlateGray;
        LeftLevelBar.Value = 0;
        RightLevelBar.Value = 0;
        LevelHintText.Text = "Waiting for audio";
        LevelHintText.Foreground = WpfBrushes.SlateGray;
        FooterText.Text = "Capture stopped · Press Enter or Start monitoring to begin";
    }

    private void RefreshNetwork()
    {
        var (rx, tx) = _network.Sample();
        RxText.Text = $"{rx:F2} Mbps";
        TxText.Text = $"{tx:F2} Mbps";
    }
}
