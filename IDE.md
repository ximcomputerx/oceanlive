# 用 Qt Creator、Visual Studio 2019 和 CLion 继续开发

OceanLive 是 CMake 工程，源码目录是含有 `CMakeLists.txt` 的这一层。三套 IDE 都导入这个目录，不导入 `build\OceanLive.sln` 作为唯一的源码工程。解决方案是 CMake 生成的结果，改过 `CMakeLists.txt` 之后要重新配置。

语言是 C++17。界面是 Qt 6 Widgets，窗口来自 `src/ui/MainWindow.ui` 和 `src/ui/SettingsDialog.ui`。海面是 OpenGL 3.3 Core。桌面挂接使用 Win32。工程没有 `.pro` 文件，也没有 Qt Quick / QML。

## 本机已经验证的工具

| 用途 | 路径 |
| --- | --- |
| 源码 | `D:\Workspaces\PAPER\oceanlive` |
| Qt 6.7.2 MSVC 2019 64 位 | `D:\C++\Qt\6.7.2\msvc2019_64` |
| qmake | `D:\C++\Qt\6.7.2\msvc2019_64\bin\qmake.exe` |
| Visual Studio 2019 Community | `D:\Program Files (x86)\Microsoft Visual Studio\2019\Community` |
| CMake | `D:\C++\CMake\bin\cmake.exe` |
| Ninja | `D:\C++\Qt\Tools\Ninja\ninja.exe` |

换一台机器时，把上表换成自己的 Qt MSVC 64 位前缀和 VS 2019 安装位置。Qt 必须是 `msvc2019_64` 这一套，并包含 Core、Gui、Widgets、OpenGL、OpenGLWidgets、Network、Multimedia、MultimediaWidgets。编译器使用 MSVC 2019 的 x64 工具集，不要用 MinGW。

命令行发布构建仍然是：

```bat
build.bat D:\C++\Qt\6.7.2\msvc2019_64 Release
```

产物是 `build\Release\OceanLive.exe`。`build.bat` 会调用 `windeployqt`，把 Qt DLL 复制到这个目录。IDE 里直接运行时，套件或运行环境会把 Qt 的 `bin` 放进 `PATH`，一般不必先执行 `windeployqt`。要得到目录自带 DLL 的发布程序，再用 `build.bat`。

## 三个构建目录不要混用

每个 IDE 使用自己的构建目录。生成器不同，指向同一个目录会让 CMake 缓存失效。

| 工具 | 构建目录 | 生成器 |
| --- | --- | --- |
| `build.bat` 和 Visual Studio 解决方案 | `build` | Visual Studio 16 2019 |
| 命令行 Ninja | `build-ninja` | Ninja |
| Qt Creator | `build-qtcreator` | Ninja |
| CLion | `cmake-build-debug` 或 `cmake-build-release` | Ninja |

`build` 和 `build-ninja` 已经存在。Qt Creator 和 CLion 第一次配置时新建自己的目录。

## Qt Creator 19.0.1

Creator 19 可以打开这个 CMake 工程。Creator 自己用哪一版 Qt 编译，不影响项目。项目套件必须指向 Qt 6.7.2 MSVC 2019 64 位。

### 配置套件

打开 **编辑 → 首选项 → 套件**。英文界面是 Edit → Preferences → Kits。

1. 在 **Qt 版本** 里添加 `D:\C++\Qt\6.7.2\msvc2019_64\bin\qmake.exe`。
2. 在 **编译器** 里确认有 **MSVC 2019 64-bit** 的 C 和 C++。没有就重新检测 Visual Studio 2019。不要选 MinGW。
3. 在 **CMake** 里使用 `D:\C++\CMake\bin\cmake.exe`，或 Creator 自带的、版本不低于 3.21 的 CMake。
4. 在设备或构建工具里把 Ninja 指到 `D:\C++\Qt\Tools\Ninja\ninja.exe`。
5. 新建套件：
   - 名称：`Desktop Qt 6.7.2 MSVC2019 64bit`
   - 设备类型：桌面
   - 编译器：MSVC 2019 64 位
   - Qt 版本：刚才添加的 6.7.2
   - CMake：上面的 cmake
   - CMake 生成器：Ninja
   - 调试器：Windows CDB。没有 CDB 时仍可编译运行，只是不能断点调试。

### 打开工程

1. **文件 → 打开文件或项目**，选择 `D:\Workspaces\PAPER\oceanlive\CMakeLists.txt`。
2. 只勾选上面的套件。
3. 构建目录填 `D:\Workspaces\PAPER\oceanlive\build-qtcreator`。不要选 `build` 或 `build-ninja`。
4. 配置。套件会传入 Qt 路径，不必再写 `CMAKE_PREFIX_PATH`。
5. 构建并运行目标 **OceanLive**。

`MainWindow.ui` 和 `SettingsDialog.ui` 用 Creator 自带的 Designer 打开。改完保存后，下次构建会由 AUTOUIC 重新生成头文件。

单元测试是目标 `oceanlive_tests`。在项目树里对它选择运行。

## Visual Studio 2019

用已经装好的 VS 2019 Community，工作负载里需要“使用 C++ 的桌面开发”。可选安装 Qt VS Tools，用来高亮 Qt 宏和打开 `.ui`。不装也能编译，因为 `uic`、`moc` 和 `rcc` 由 CMake 在构建时调用。

### 方式一：打开现有解决方案

这和 `build.bat` 使用同一份生成结果。

1. 若还没有 `build\OceanLive.sln`，先在源码目录执行：

   ```bat
   build.bat D:\C++\Qt\6.7.2\msvc2019_64 Release
   ```

2. 启动 Visual Studio 2019。
3. **文件 → 打开 → 项目/解决方案**，选择 `D:\Workspaces\PAPER\oceanlive\build\OceanLive.sln`。
4. 解决方案配置选 **Release** 或 **Debug**，平台选 **x64**。
5. 在解决方案资源管理器里把 **OceanLive** 设为启动项目。
6. 生成解决方案。启动项目是 `OceanLive`。测试项目是 `oceanlive_tests`。

Release 目录在执行过 `build.bat` 或 `windeployqt` 之后，exe 旁边已经有 Qt DLL，可以直接调试运行。Debug 如果提示缺少 `Qt6Cored.dll`，把 `D:\C++\Qt\6.7.2\msvc2019_64\bin` 加到该项目的调试环境变量 `PATH` 前面，或再执行 `build.bat D:\C++\Qt\6.7.2\msvc2019_64 Debug`。

修改 `CMakeLists.txt` 后，在源码目录重新运行上面的 `build.bat`，或只重新配置：

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH=D:\C++\Qt\6.7.2\msvc2019_64
```

然后在 Visual Studio 里提示重新加载解决方案时选择重载。

### 方式二：用“打开文件夹”让 VS 自己跑 CMake

1. **文件 → 打开 → 文件夹**，选择 `D:\Workspaces\PAPER\oceanlive`。
2. 在 CMake 设置里把生成器留在 Ninja 或改成 Visual Studio 16 2019。
3. 增加 CMake 变量 `CMAKE_PREFIX_PATH`，值为 `D:\C++\Qt\6.7.2\msvc2019_64`。
4. 构建目录不要填已经存在的 `build`。可以用 `build-vs-folder`。
5. 保存后等待配置完成，生成目标 **OceanLive**。

方式一和 `build.bat` 的发布目录一致。方式二适合只在 IDE 里改代码。两套不要指向同一个构建目录。

## CLion

CLion 打开 CMake 工程即可，不需要把工程转成 `.pro`。构建仍然靠 CMake 的 AUTOMOC、AUTOUIC 和 AUTORCC。CLion 没有 Qt Creator 那样的界面设计器；要改界面布局时，用 Qt Creator 打开同一个 `.ui`，或把外部工具指到：

`D:\C++\Qt\6.7.2\msvc2019_64\bin\designer.exe`

### 工具链

1. **文件 → 打开**，选择 `D:\Workspaces\PAPER\oceanlive`。看到 `CMakeLists.txt` 后确认打开为 CMake 工程。
2. **设置 → 构建、执行、部署 → 工具链**。
3. 添加 **Visual Studio**，版本选 **2019**，架构选 **amd64**。C 和 C++ 编译器都应是 VS 2019 的 `cl.exe`。
4. CMake 使用 `D:\C++\CMake\bin\cmake.exe`，或 CLion 自带的 3.21 以上版本。
5. 把这条 Visual Studio 工具链设为默认。

### CMake 配置

打开 **设置 → 构建、执行、部署 → CMake**。Debug 和 Release 都按下面填写。

- 构建目录：保持 CLion 默认的 `cmake-build-debug` 和 `cmake-build-release`。
- 生成器：Ninja。
- CMake 选项：

  ```text
  -DCMAKE_PREFIX_PATH=D:/C++/Qt/6.7.2/msvc2019_64
  ```

应用后等待 CMake 重新加载。目标列表里应出现 `OceanLive`、`oceanlive_tests` 和 `OceanLiveDesktopProbe`。

### 运行

运行配置选 **OceanLive**。在该配置的环境变量里增加：

```text
PATH=D:\C++\Qt\6.7.2\msvc2019_64\bin;%PATH%
```

这样调试器启动时能找到 Qt DLL。缺少这一项时，程序会在进入 `main` 之前因为找不到 `Qt6Core.dll` 退出。

单元测试把运行目标改成 `oceanlive_tests`。它是控制台程序，输出里应有 `failures=0`。

## 导入之后都会遇到的事

- 程序有单实例限制。桌面或托盘里已经有 OceanLive 时，IDE 再运行会立刻退出。先从托盘选择退出。
- 关闭主窗口只会缩到托盘。要用调试器停掉进程时，从托盘退出，或在 IDE 里结束进程。
- 着色器在 `resources/shaders`，由 `resources/resources.qrc` 打进可执行文件。改完着色器需要重新构建，只保存文件不会更新正在运行的壁纸。
- 三个可运行目标：`OceanLive` 是壁纸程序，`oceanlive_tests` 是逻辑测试，`OceanLiveDesktopProbe` 只打印桌面窗口层级。
- 正式发布目录仍以 `build.bat` 生成的 `build\Release\OceanLive.exe` 为准。Qt Creator 和 CLion 的构建目录用来开发和调试。
