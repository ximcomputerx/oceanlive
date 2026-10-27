[中文](README.md) | **English**

# OceanLive

OceanLive is a live wallpaper for the Windows desktop. It draws a real-time ocean behind the desktop icons. The taskbar and normal windows stay in front of the picture. The default scene is a multi-layer Gerstner ocean on OpenGL 3.3 and does not need a network connection. It can also loop a local video in the same wallpaper window.

Version 1.2.0. The stack is C++17, Qt 6 Widgets, Win32, and CMake.

## Features

- Draw a live ocean on the desktop background while icons and the taskbar stay clickable.
- Start, pause, resume, and stop the wallpaper from the main window. Closing the main window leaves the program in the system tray. Exit from the tray menu ends the process.
- Pause stops the animation only. After a manual pause, leaving a fullscreen application does not resume playback.
- Open one render window per monitor, or show the wallpaper on a single selected monitor.
- Low, medium, and high quality, plus themes such as sunny, sunset, and deep blue.
- Play a local MP4, MOV, AVI, MKV, or WebM file. Playback loops and starts muted. The program does not ship a video file.

## Requirements

- Windows 10 or Windows 11
- The Visual Studio 2019 MSVC x64 toolset
- CMake 3.21 or newer
- A 64-bit MSVC build of Qt 6, with Core, Gui, Widgets, OpenGL, OpenGLWidgets, Network, Multimedia, and MultimediaWidgets

Qt does not have to be installed in a fixed location. Steps for Qt Creator, Visual Studio, and CLion are in [IDE.md](IDE.md).

## Build

```bat
build.bat <Qt-prefix> Release
```

`<Qt-prefix>` is the directory of the Qt MSVC 64-bit kit. You can also set `CMAKE_PREFIX_PATH` or `Qt6_DIR` and then run `build.bat`. The script loads the Visual Studio 2019 x64 environment, generates `build\OceanLive.sln`, builds Release, and runs `windeployqt` so the Qt runtime is copied into `build\Release\`.

For a debug build, use `Debug` instead of `Release`.

`oceanlive_tests` covers parameter clamping, fullscreen detection, monitor selection, and autostart entries. `OceanLiveDesktopProbe` prints the current desktop window hierarchy.

## Run

Start `OceanLive.exe` and use the main window.

- `--minimized` starts in the tray only. The autostart entry uses this argument.

Settings are stored in `%LOCALAPPDATA%\OceanLive\OceanLive\OceanLive.ini`. Logs are in `logs\oceanlive.log` in that same directory.

The interface language is on the General page of Settings: follow the system, Simplified Chinese, or English.

The installer script is `packaging\OceanLive.iss`. Build Release first, then compile the script with Inno Setup. It installs for the current user and does not ask for administrator rights. Autostart writes only `HKCU\...\Run\OceanLive`, and turning it off deletes only that value.

## Desktop hosting

The wallpaper window is placed behind the desktop icons through Explorer's undocumented `WorkerW` structure. That path is not a stable public API and can break after a Windows update. If no host is found, the wallpaper does not start and the status bar explains why.

Rendering, video playback, and the hosting details are written up in Chinese in [docs/architecture.md](docs/architecture.md).

## License

Source code in this repository is under the [Apache License 2.0](LICENSE).

Copyright 2026 ximcomputerx

Qt 6 is LGPL. Video decoding uses the FFmpeg libraries shipped with Qt Multimedia. This repository does not contain those third-party binaries. Keep dynamic linking when you distribute a build, and follow the Qt and FFmpeg licenses separately.
