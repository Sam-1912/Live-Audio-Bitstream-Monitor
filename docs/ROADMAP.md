# Production roadmap: remaining requested stages

## Stage 1 — per-application audio
The native starter captures the default render endpoint. Add process-loopback capture using
`ActivateAudioInterfaceAsync` with `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK` and
`AUDIOCLIENT_ACTIVATION_PARAMS`. Use one capture client per target process tree, handle
async activation completion, and keep the capture worker in the interactive user session.
Do not assume the default endpoint stream identifies individual apps.

## Stage 2 — per-process/per-flow network
Implement a dedicated ETW session for TCP/IP and UDP/IP events, and WFP ALE flow
establishment events to map flow tuples to owning PIDs. Maintain per-flow byte counters
from suitable packet/transport events, handle IPv4/IPv6, UDP, retransmits, process exit,
PID reuse, VPN interfaces, and loopback. Test accounting against Windows Performance
Monitor and controlled transfers. Do not infer media bytes from all traffic of a browser PID.

## Stage 3 — encoded media bitrate
For files or streams the user is authorized to inspect, use libavformat/ffprobe to read
codec parameters and packet sizes. Compute a rolling bitrate from audio packet payload
sizes over a defined window. For HLS/DASH, parse manifests and audio segment payloads,
not merely the declared variant BANDWIDTH. For protected apps, report unavailable unless
the application exposes supported metadata. Never relabel NIC throughput as codec bitrate.

## Stage 4 — dashboard and persistence
Add per-app rows, time-series charts, bounded channels, SQLite tables for sessions,
network flows, audio metrics and codec observations. Timestamp all measurements with
QueryPerformanceCounter-derived monotonic time plus UTC wall-clock time.

## Stage 5 — operational hardening
Add ETW/WFP diagnostics, device change recovery, service/UI separation, installer,
signing, structured logs, retention controls, and tests for sleep/resume, hot-plug,
multi-device, multiple users, and process restarts.
