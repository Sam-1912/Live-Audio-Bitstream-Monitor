# Audio Monitor — v1.0 Two-Channel Meter

**Version:** 1.0 · **Variant:** two channel meters (L/R), no frequency-band analyzer · **Git branch:** `v1-two-channel` · **Tag:** `v1.0-two-channel`

This is the original two-channel level-meter version. It displays left/right output levels and does **not** calculate frequency bands. For the frequency spectrum version, use [`main` (v2.0, 8-band spectrum)](https://github.com/Sam-1912/Live-Audio-Bitstream-Monitor/tree/main).

Audio Monitor is a Windows 11 desktop application that follows the Windows default Multimedia playback endpoint with WASAPI loopback and displays live audio and network metrics in a mixer-style WPF dashboard. When Windows changes the default output, capture reconnects automatically.

## What it does

- Captures the current Windows default Multimedia render device through the audio engine
- Automatically reconnects when the default output changes between built-in speakers, headphones, HDMI, or another render endpoint
- Measures RMS and peak values in dBFS
- Posts sample rate, channel count, and output format details
- Reads machine-wide RX/TX throughput from Windows network interfaces
- Supports optional file inspection with ffprobe for accessible media files

## Supported platform

- Windows 11 x64
- Visual Studio 2022 or newer
- .NET 10 SDK
- Desktop development with C++ workload

## Prerequisites

- Visual Studio 2022 (17.10+) with Desktop development with C++
- Windows 11 SDK
- .NET 10 SDK
- Optional: ffprobe.exe on PATH for media inspection

## Build and run

1. Open `AudioMonitor.sln` in Visual Studio.
2. Set the solution configuration to `Release` and platform to `x64`.
3. Build the `AudioMonitor.UI` project or the whole solution. The UI project builds the native WASAPI DLL automatically and copies it beside the executable.

From **Developer PowerShell for Visual Studio** (with the Desktop development with C++ workload installed), build and run the app with:

```powershell
msbuild app\AudioMonitor.UI\AudioMonitor.UI.csproj /t:Build /p:Configuration=Release /p:Platform=x64
& .\app\AudioMonitor.UI\bin\x64\Release\net10.0-windows\win-x64\AudioMonitor.UI.exe
```

## Keyboard controls

The app exposes a few direct keyboard shortcuts:

- In Visual Studio: `F5` starts the app in debug mode.
- In the app: `Enter` starts monitoring.
- In the app: `Esc` or `Space` stops monitoring.
- In the app: `Alt+F4` closes the window.
- Minimize the app window to keep it running in the system tray when the `Minimize to tray` toggle is enabled.

## UI polish and convenience

The dashboard now includes:

- a custom application icon and tray icon
- a live clock in the footer
- a minimize-to-tray feature for background monitoring
- a cleaner desktop presentation for quick monitoring sessions

## Usage

1. Launch the app.
2. Press `Enter` or click `Start monitoring`.
3. Allow any Windows permission prompts for audio capture.
4. Watch the live RMS/peak and network throughput values.
5. Press `Esc` or `Space` to stop the capture when finished.

## Important notes

- This app captures decoded PCM from the default render endpoint; it does not recover original encoded bitrate from protected or arbitrary app streams.
- The capture automatically follows Windows' default Multimedia playback endpoint and reconnects when it changes, such as when switching between built-in speakers, headphones, or HDMI audio. No in-app device selection is needed; Windows must make the chosen endpoint the default Multimedia output.
- Network values are machine-wide interface counters, not per-process browser or app attribution.
- For media inspection, ffprobe must be pointed at an accessible file that the current user can read.

## Project structure

- `app/AudioMonitor.UI` — WPF desktop front end
- `native/AudioCapture.cpp` — WASAPI loopback implementation
- `docs/` — additional documentation

## Troubleshooting

- If the build reports missing C++ tools, install Visual Studio's Desktop development with C++ workload and a Windows SDK, then rebuild the UI project. Its build also compiles and copies `AudioMonitor.Native.dll`.
- If you plug in headphones or switch to HDMI while monitoring, the app follows the Windows default Multimedia output and reconnects automatically. If Windows does not make the new device the default Multimedia output, select it in Windows Sound settings.
- If audio values stay flat at `-120 dBFS`, ensure the default playback device is active and a media stream is playing.
- If the app shows no network values, ensure at least one active network interface is available and the machine is transmitting or receiving traffic.

## Security and operational constraints

- Run the app as the logged-in user in the interactive desktop session.
- Do not use this tool in Session 0 or as a background service.
- Do not claim hidden codec, DRM, or app-specific bitrate recovery from passthrough or protected streams.
