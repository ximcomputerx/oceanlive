**中文** | [English](README.en.md)

# OceanLive

OceanLive 是 Windows 桌面动态壁纸。它把实时渲染的海洋画在桌面图标后面，任务栏和普通窗口仍在画面前面。默认画面是 OpenGL 3.3 上的多层 Gerstner 海浪，不需要网络。也可以循环播放本地视频，画面仍在同一个壁纸窗口里。

版本 1.2.0。技术栈是 C++17、Qt 6 Widgets、Win32 和 CMake。

## 功能

- 在桌面背景层显示实时海洋，图标和任务栏保持可点击。
- 主窗口可以启动、暂停、恢复和停止壁纸。关闭主窗口后程序留在系统托盘，托盘里的「退出」才会结束进程。
- 暂停只停动画。用户手动暂停后，全屏程序退出不会自动恢复。
- 可以为每台显示器各开一个渲染窗口，也可以只选其中一台。
- 低、中、高三档画质，以及晴日、日落、深蓝等主题。
- 可以改播本地 MP4、MOV、AVI、MKV 或 WebM。默认循环、默认静音。程序不附带视频文件。

## 环境

- Windows 10 或 Windows 11
- Visual Studio 2019 的 MSVC x64 工具集
- CMake 3.21 或更高版本
- Qt 6 的 MSVC 64 位套件，模块包括 Core、Gui、Widgets、OpenGL、OpenGLWidgets、Network、Multimedia、MultimediaWidgets

Qt 不要求装在固定路径。用 Qt Creator、Visual Studio 或 CLion 打开工程的步骤见 [IDE.md](IDE.md)。

## 构建

```bat
build.bat <Qt前缀> Release
```

`<Qt前缀>` 是 Qt 的 MSVC 64 位套件目录。也可以先设置 `CMAKE_PREFIX_PATH` 或 `Qt6_DIR`，再运行 `build.bat`。脚本会调用 VS 2019 x64 环境，生成 `build\OceanLive.sln`，编译 Release，并用 `windeployqt` 把 Qt 运行时复制到 `build\Release\`。

调试构建把 `Release` 换成 `Debug`。

`oceanlive_tests` 覆盖参数钳制、全屏判断、显示器选择和启动项读写。`OceanLiveDesktopProbe` 只打印当前桌面窗口层级。

## 运行

启动 `OceanLive.exe` 后使用主窗口。

- `--minimized`：启动后只显示托盘。开机启动项使用这个参数。

配置写在 `%LOCALAPPDATA%\OceanLive\OceanLive\OceanLive.ini`。日志写在同一目录的 `logs\oceanlive.log`。

界面语言在设置的通用页，可选跟随系统、简体中文和 English。

安装包脚本是 `packaging\OceanLive.iss`。先完成 Release 构建，再用 Inno Setup 编译。安装到当前用户目录，不请求管理员权限。开机启动只写 `HKCU\...\Run\OceanLive`，关闭时只删除这一项。

## 桌面挂接

程序通过资源管理器未公开的 `WorkerW` 结构，把壁纸窗口放到桌面图标后面。这条路径不是稳定的公开 API，系统更新后可能失效。找不到宿主时，壁纸不会启动，状态栏会说明原因。

渲染、视频播放和挂接的详细说明见 [docs/architecture.md](docs/architecture.md)。

## 许可证

本仓库源码以 [Apache License 2.0](LICENSE) 授权。

Copyright 2026 ximcomputerx

Qt 6 使用 LGPL。视频解码使用 Qt Multimedia 附带的 FFmpeg 动态库。本仓库不包含这些第三方二进制文件。发布安装包时保持动态链接，并另行遵守 Qt 与 FFmpeg 的许可证。
