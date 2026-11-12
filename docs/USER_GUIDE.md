# User guide

## App purpose

Audio Monitor is a desktop utility for observing Windows playback output and machine-wide network activity. It is designed for the interactive desktop session on Windows 11 and follows the Windows default Multimedia audio endpoint. It automatically reconnects when that default changes, including between built-in speakers, headphones, and HDMI audio.

## Start the app

### Option 1: Visual Studio

1. Open `AudioMonitor.sln`.
2. Select `Release` and `x64`.
3. Build the `AudioMonitor.Native` project.
4. Build the `AudioMonitor.UI` project.
5. Press `F5` to start debugging, or press `Ctrl+F5` to run without debugging.

### Option 2: Command line

Open **Developer PowerShell for Visual Studio** with the Desktop development with C++ workload installed, then run:

```powershell
msbuild app\AudioMonitor.UI\AudioMonitor.UI.csproj /t:Build /p:Configuration=Release /p:Platform=x64
& .\app\AudioMonitor.UI\bin\x64\Release\net10.0-windows\win-x64\AudioMonitor.UI.exe
```

## Keyboard controls

Inside the running app:

- `Enter` — start monitoring
- `Esc` — stop monitoring
- `Space` — stop monitoring
- `Alt+F4` — close the application

The same actions are available from the window buttons.

## What to expect

When monitoring starts, the dashboard will display:

- RMS output level in dBFS
- Peak output level in dBFS
- An eight-band frequency spectrum (63 Hz, 125 Hz, 250 Hz, 500 Hz, 1 kHz, 2 kHz, 4 kHz, and 8 kHz) analyzed from live decoded PCM with a 4096-point FFT
- Network RX rate in Mbps
- Network TX rate in Mbps
- Sample rate and channel count

## Troubleshooting

### Native DLL not found

The UI depends on `AudioMonitor.Native.dll` from the native capture project. Rebuild the native C++ DLL and ensure it is copied into the same output folder as the WPF app.

### No audio readings

- Ensure a sound is playing on the default output device.
- Make sure the Windows audio endpoint has permission to record loopback output.
- Confirm the app is running in the active desktop session, not a background service host.
- The app follows the Windows default Multimedia output and reconnects when it changes, including built-in speakers, headphones, and HDMI render endpoints. You do not need to select a device in the app. If Windows does not switch its default Multimedia output, choose the desired device in Windows Sound settings.

### No network values

- Check that the machine has active network interfaces.
- Confirm that traffic is actually flowing through the interfaces.
- Network telemetry is machine-wide, not a browser- or app-specific breakdown.

## Limitations

- The app monitors the default render device only.
- Network values are interface totals, not per-process attribution.
- It cannot recover protected media bitrate or app-specific codec metadata from arbitrary protected streams.
