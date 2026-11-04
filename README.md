# BorderWatch 5.0 source (Native C++ Rewrite)

This is the shareable source for BorderWatch 5.0, rewritten completely in native C++20. This rewrite eliminates all external runtime dependencies (PowerShell and .NET), making the application significantly more optimized, instantaneous to launch, and lightweight (~6 MB RAM, 0.0% CPU when idle).

Personal settings, Roblox logs and previous builds are excluded.

## Build on Windows

Requirements: Windows 10/11 and Visual Studio 2022 C++ Build Tools (or Community/Enterprise) with the Windows 10/11 SDK. No external package managers or separate runtimes are required.

Open PowerShell in the project folder and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Build-Native.ps1
```

The single standalone executable is written to `dist\BorderWatch.exe`. `Build-Native.ps1` compiles and embeds all resources from `assets/` directly into the binary; no private configuration is needed.

Test the built executable without changing the normal app data:

```powershell
.\dist\BorderWatch.exe --self-test "$PWD\dist\self-test"
```

Read `dist\self-test\self-test.txt` for the result, or `single-file-error.txt` in that directory if verification failed.

## Files

- `Build-Native.ps1`: builds the single-file native Windows executable using MSVC.
- `CMakeLists.txt`: cross-generator CMake build configuration.
- `cpp\include\`: C++ header definitions (UI, scanner, audio, toasts, config, watchlist).
- `cpp\src\`: native C++ implementations:
  - `MainWindow.cpp`: modern fluent panel, status cards, hotkeys, tray and event loop.
  - `ModernControls.cpp`: custom vector controls (toggles, sliders, dropdowns, steppers, history box).
  - `DetectedWindow.cpp`: Detected Players roster window with smooth inertia scrolling.
  - `ToastManager.cpp`: interactive on-screen floating toast notifications with avatar loading.
  - `LogScanner.cpp`: high-speed AUT log file parsing, initial scan, and player detection.
  - `AudioPlayer.cpp`: low-latency sound playback via embedded miniaudio engine.
  - `Watchlist.cpp`: GitHub watchlist import, JSON validation, and fallback handling.
  - `StartupManager.cpp`: per-user Windows autostart shortcut management.
  - `HotkeyManager.cpp`: global keyboard hook for Tab + I toggle.
- `cpp\res\BorderWatch.rc`: application manifest, Windows version metadata (5.0.0.0), and embedded resources.
- `cpp\third_party\yyjson`: embedded high-performance C JSON parser.
- `assets\`: application icon, audio alerts (`meow-1.mp3`, `ping.wav`, etc.), default `settings.example.json`, and initial `players.json` snapshot.

## New appearance and sound controls

The interface is built with hardware-accelerated GDI+, smooth animated toggles, and smooth inertia scrolling. Choose MP3 / WAV validates and copies custom audio files into the app data sounds directory. Volume is adjustable in real time via a slider. All preferences auto-save instantly with discrete visual confirmation.

## Behavior

The default watchlist URL is https://raw.githubusercontent.com/agentyautja/borderpatrol-list/main/players.json, automatically synced every 15 minutes in a background thread without interrupting the UI. Downloads are parsed as JSON data, never executed. Failed or invalid downloads retain the last valid list.

The application reads AUT activity in Roblox logs, including an initial scan on startup to identify players already in the server. These logs do not expose a complete live roster or reliable leave events; detection cannot guarantee every player or same-server rejoin.

In-app self-updates check https://github.com/xnldev/BorderWatch releases with 1 click, downloading and atomically replacing the binary.

Autostart is opt-in and creates only the current user's BorderWatch shortcut. Keep the EXE in the same location after enabling. Quit app fully stops the process; the header cross and Tab+I hide the panel while background scanning continues.

Runtime data is stored under `%LOCALAPPDATA%\BorderWatch`. Avatar cache is automatically LRU-pruned to ~50 recent avatars. To use a separate test profile, run `BorderWatch.exe --data-dir "C:\your\test-folder"`.

## Assets and antivirus

Audio source information is in `assets\SOUND-LICENSE.txt`. The supplied meow-1.mp3 is included unchanged; its source/license was not provided. This archive does not assign new license terms to third-party audio or the GitHub list.

Builds are unsigned. Windows may display a SmartScreen reputation warning. Microsoft Defender reported no threats when checked; that result does not guarantee other antivirus results or apply to modified builds.
