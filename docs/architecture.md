# OceanLive 实现说明

首页说明见 [README.md](../README.md)。英文首页见 [README.en.md](../README.en.md)。

OceanLive 把实时渲染的海洋场景挂到 Windows 桌面背景层。桌面图标、任务栏和普通窗口仍在壁纸前面。默认画面由 OpenGL 3.3 上的多层 Gerstner 海浪生成，不需要网络。也可以改成循环播放本地视频，画面仍画在同一个桌面壁纸窗口里。

版本 1.2.0。技术栈是 C++17、Qt 6 Widgets、Win32、OpenGL 3.3 Core 和 CMake。

## 运行

启动 `OceanLive.exe` 后使用主窗口：

- 启动壁纸：把渲染窗口挂到桌面背景层。
- 暂停 / 恢复：暂停只影响动画。用户暂停后，全屏程序退出不会自动恢复。
- 停止：销毁壁纸窗口和对应的 OpenGL 资源。
- 关闭主窗口：缩到系统托盘，程序继续运行。托盘菜单里的“退出”才会结束进程。

命令行：

- `--minimized`：启动后只显示托盘。开机启动项使用这个参数。
- `--dump-preview 目录`：按固定时间和 1920×1080 画布输出晴日、低雾、强直射光、远景裁切、近景裁切、日落和深蓝，并记录 4K 全屏帧率。若程序目录里有 `preview-v13/sunny.png`，会一并复制为修改前对照。
- `--self-test 报告路径`：挂接壁纸并写入验收记录，结束时会停止壁纸。
- `--video-desktop-test 报告路径`：生成一张测试图案视频，挂到桌面背景层，检查帧缓冲、暂停和缺失文件提示。这不是海洋素材。

配置写在 `%LOCALAPPDATA%\OceanLive\OceanLive\OceanLive.ini`。日志写在同一目录的 `logs\oceanlive.log`。诊断区里拿不到的 CPU、GPU 或 OpenGL 字段会显示“不可用”。

界面语言在设置的通用页，可选跟随系统、简体中文和 English。新增界面句子后，执行 `cmake --build build-ninja --target update_translations`，再补 `i18n/OceanLive_en.ts`。

## 桌面挂接

Windows 10 上，程序向 `Progman` 发送未公开消息 `0x052C`（带 1 秒超时）。Explorer 会把 `SHELLDLL_DefView` 放到一个 `WorkerW`，并在它后面留下另一个全屏 `WorkerW`。OceanLive 把壁纸窗口 `SetParent` 到后面的那个 `WorkerW`。

在本机 Windows 10 19045 上，发送消息之前图标层在 `Progman` 里；发送之后图标层位于一个全屏 `WorkerW`，后一个空的全屏 `WorkerW` 才是壁纸宿主。实现按这个结构查找，并拒绝那些只有一百多像素的无关 `WorkerW`。

如果找不到独立的背景 `WorkerW`，程序会退回到 Windows 11 24H2 文档中描述的做法：把分层子窗口放到 `Progman` 里、`SHELLDLL_DefView` 下面。这条路径在 Windows 10 上不会使用。`0x052C` 和 `WorkerW` 都不是稳定的公开 API，系统更新后可能失效。找不到宿主时，状态栏会说明壁纸没有启动。

每个选中的显示器各有一个渲染窗口。这样可以覆盖负坐标、不同分辨率和不同 DPI，也可以只在主显示器或某一台显示器上显示，避免一块超大帧缓冲和显示器之间的错位。

壁纸窗口使用 `WS_CHILD`、`WS_EX_NOACTIVATE` 和 `WS_EX_TOOLWINDOW`，并去掉 `WS_EX_APPWINDOW` 和 `WS_EX_TOPMOST`。它不覆盖任务栏，也不应该进入 Alt+Tab。资源管理器重启后，后台每秒检查宿主是否还在，失效就重新挂接。

## 海洋渲染

海浪分成三层，都在 OpenGL 3.3 里完成，没有引入第三方海洋库：

- 远景长波和中景风浪用最多 12 组 Gerstner 波。它们共用一个风向，只做有限的方向偏移，波长不是整数倍，波峰沿横向被正弦扰动。
- 顶点按距离几何分布，远处的三角形不会一下子变得很稀。
- 近景细波只改片元法线，用带解析导数的值噪声，并随距离放大采样尺度，避免远景铺出重复块。
- 高频顶点波在大约 120 米后衰减到 0，海平线附近只留下长波。

光照使用 GGX 高光和 Schlick 菲涅尔。太阳盘和光晕只画在天空上，海面闪光来自同一条太阳方向上的 GGX，不再把天空光晕反射第二遍。太阳方位和高度是观察空间里的角度：正角度在画面右侧，高度是相对视线中心的仰角。OpenGL 观察空间朝 -Z 看，太阳方向的 Z 分量因此是负的，否则太阳会落到摄像机背后。海平线以上的水面片元会被丢弃。晴日主题的大气雾和海平线雾默认很低，远景只做轻微的蓝色空气透视，不再把整片海混成灰白地平线色。日落和深蓝保留各自更高的雾化。

低、中、高画质分别是 5、8、12 组波浪，以及 120×72、200×120、320×190 的网格。主题切换大约 0.6 秒内插值天空和海水颜色，并带上该主题的太阳位置、风向和晴空参数。天空不再绘制云。主窗口只保留速度、波浪、画质、帧率和画面来源。太阳、雾化、天空饱和度、颜色和视频文件在设置窗口的“动画”页。旧配置里如果还没有 `ocean/atmosphereHaze`，首次读取会改用当前主题的晴空默认值，波浪、反射和视频路径仍保留。保存设置时会去掉已经不用的 `ocean/cloudStrength`。

暂停时停止渲染定时器。视频模式暂停时同时暂停解码。目标帧率用定时器触发重绘，并打开垂直同步。

## 本地视频

设置里选择 MP4、MOV、AVI、MKV 或 WebM。播放器只有一个 `QMediaPlayer`，默认循环、默认静音。帧通过 `QVideoSink` 送进现有 OpenGL 壁纸窗口，不另开播放窗口，也不覆盖图标和任务栏。预览和桌面壁纸共用这一路画面。文件缺失或解码失败会提示，不会显示成已经在播放。停止壁纸后 `stop()` 并清空媒体源。

Qt 6.7 的 Windows 后端是 FFmpeg，随 `windeployqt` 带上 `Qt6Multimedia` 和 `avcodec` 等 DLL，许可证与现有 Qt 依赖相同，没有再引入别的解码器。程序不附带海洋视频。

在本机上，把 `QVideoWidget` 作为壁纸窗口的子窗口也能画出画面。正式路径仍用 `QVideoSink`，这样预览和多显示器不会各建一个解码器。

## 构建

需要 Windows 10/11、Visual Studio 2019 的 MSVC x64 工具集、CMake 3.21 或更高版本，以及 Qt 6.x 的 MSVC 64 位套件。需要的 Qt 模块是 Core、Gui、Widgets、OpenGL、OpenGLWidgets、Network、Multimedia、MultimediaWidgets。Network 只用于单实例本地套接字。MultimediaWidgets 只在视频嵌入探测里使用。用 Qt Creator 19、Visual Studio 2019 或 CLion 打开工程的步骤写在 [IDE.md](IDE.md)。

不要求 Qt 安装在固定路径：

```bat
build.bat C:\Qt\6.7.2\msvc2019_64 Release
```

也可以先设置 `CMAKE_PREFIX_PATH` 或 `Qt6_DIR`，再运行 `build.bat`。脚本会调用 VS 2019 x64 环境，生成 `build\OceanLive.sln`，编译 Release，并用 `windeployqt` 把 Qt 运行时复制到 `build\Release\`。

手动配置：

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.7.2\msvc2019_64
cmake --build build --config Release --parallel
C:\Qt\6.7.2\msvc2019_64\bin\windeployqt.exe --release --no-translations --compiler-runtime build\Release\OceanLive.exe
```

`oceanlive_tests` 覆盖参数钳制、全屏判断、显示器选择和启动项读写。`OceanLiveDesktopProbe` 用来打印当前桌面窗口层级。

调试版同样用 `build.bat <Qt前缀> Debug`。主窗口的“显示渲染诊断”打开后，会显示帧率、CPU 帧时间、GPU 计时查询、工作集、OpenGL 字符串和宿主句柄。显存只在驱动提供 `GL_NVX_gpu_memory_info` 时显示，读到的是整卡占用和专用显存，不是 OceanLive 单独的分配。没有该扩展时显示“不可用”。

## 安装包

`windeployqt` 生成的 `build\Release` 目录就是安装包的内容。`packaging\OceanLive.iss` 是一份 Inno Setup 脚本，安装到每用户目录，不请求管理员权限。需要先完成 Release 构建，再编译这个脚本。开机启动只写当前用户的 `HKCU\...\Run\OceanLive`，关闭时只删除这一项。

## 已知限制

- 桌面宿主依赖未公开的 Explorer 结构。Windows 11 24H2 的分层子窗口路径已写好，但本仓库的实机验证发生在 Windows 10 19045。
- OpenGL 窗口加 `WS_EX_LAYERED` 在部分驱动上会黑屏，所以 Windows 10 路径不使用分层窗口。
- 全屏检测是按窗口矩形和样式做的启发式判断，极端的无边框窗口仍可能误判。
- 多显示器逻辑按每屏一个窗口实现。当前开发机只有一块 3840×2160、150% DPI 的屏幕，没有在混合 DPI 或多显示器上跑过。
- 视频模式依赖 Qt Multimedia 的 FFmpeg 插件。4K 视频会在 CPU 上把帧转成 RGBA 再上传，负载高于实时海洋。程序不附带视频文件。停止后，Windows 原来的静态壁纸会重新露出来。
- 远景长波在透视下仍会形成一些接近水平的波列。这是单一风向的结果，不是一张平铺纹理，但还没有照片级的碎浪。
