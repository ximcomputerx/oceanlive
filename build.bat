rem SPDX-License-Identifier: Apache-2.0
rem 关闭命令回显
@echo off
rem 启用命令扩展，并让变量改动留在脚本内
setlocal EnableExtensions
rem 切到本脚本所在目录
cd /d "%~dp0"

rem 用法：build.bat [Qt前缀] [Release 或 Debug]
rem 已经设置 CMAKE_PREFIX_PATH 时可以不传 Qt 前缀

rem 第一个参数作为 Qt 安装前缀
set "QT_PREFIX=%~1"
rem 参数为空时改用环境里的 CMAKE_PREFIX_PATH
if "%QT_PREFIX%"=="" set "QT_PREFIX=%CMAKE_PREFIX_PATH%"
rem 仍为空且默认目录里有 Qt6 配置时，才使用这条默认路径
if "%QT_PREFIX%"=="" if exist "D:\C++\Qt\6.7.2\msvc2019_64\lib\cmake\Qt6\Qt6Config.cmake" set "QT_PREFIX=D:\C++\Qt\6.7.2\msvc2019_64"

rem 还是没有 Qt 时说明原因并失败退出
if "%QT_PREFIX%"=="" (
    rem 报告没有找到 Qt 6
    echo [OceanLive] Qt 6 was not found.
    rem 提示传入 64 位前缀，或设置环境变量
    echo Pass the MSVC 64-bit prefix, or set CMAKE_PREFIX_PATH or Qt6_DIR.
    rem 给出一条示例
    echo Example: build.bat C:\Qt\6.7.2\msvc2019_64 Release
    rem 以错误码 1 结束
    exit /b 1
    rem 结束缺少 Qt 的分支
)

rem 前缀里必须有 Qt6 配置文件
if not exist "%QT_PREFIX%\lib\cmake\Qt6\Qt6Config.cmake" (
    rem 报告这个前缀下没有 Qt6Config
    echo [OceanLive] Qt6Config.cmake was not found under %QT_PREFIX%
    rem 配置不存在时失败退出
    exit /b 1
    rem 结束 Qt 配置缺失的分支
)

rem 默认编 Release
set "MODE=Release"
rem 第二个参数是 Debug 时才改为调试
if /I "%~2"=="Debug" set "MODE=Debug"

rem 先找社区版的 64 位环境脚本
set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
rem 没有再试专业版
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
rem 再试 Build Tools
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
rem 再试 D 盘上的社区版
if not exist "%VCVARS%" set "VCVARS=D:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
rem 最后试 D 盘上的专业版
if not exist "%VCVARS%" set "VCVARS=D:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
rem 五处都没有就无法编译
if not exist "%VCVARS%" (
    rem 报告找不到 VS2019 的 64 位工具
    echo [OceanLive] Visual Studio 2019 x64 tools were not found.
    rem 提示安装 v142，或在脚本里改 VCVARS
    echo Install the MSVC v142 toolset, or edit VCVARS in build.bat.
    rem 找不到编译器时失败退出
    exit /b 1
    rem 结束找不到编译环境的分支
)

rem 载入 64 位编译环境
call "%VCVARS%"
rem 环境脚本失败就停止
if errorlevel 1 exit /b 1

rem 用 VS2019 生成器配置，Qt 前缀只从变量传入
cmake -S "%~dp0." -B "%~dp0build" -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH="%QT_PREFIX%"
rem 配置失败就停止
if errorlevel 1 exit /b 1

rem 按 MODE 并行编译
cmake --build "%~dp0build" --config %MODE% --parallel
rem 编译失败就停止
if errorlevel 1 exit /b 1

rem 记下这一配置下的可执行文件
set "EXE=%~dp0build\%MODE%\OceanLive.exe"
rem Debug 部署调试库，否则部署发布库
if /I "%MODE%"=="Debug" (
    rem 部署调试依赖和运行库，不复制 Qt 翻译
    "%QT_PREFIX%\bin\windeployqt.exe" --debug --no-translations --compiler-runtime "%EXE%"
    rem 调试部署结束，下面是发布部署
) else (
    rem 部署发布依赖和运行库，不复制 Qt 翻译
    "%QT_PREFIX%\bin\windeployqt.exe" --release --no-translations --compiler-runtime "%EXE%"
    rem 发布部署结束
)
rem 部署失败就停止
if errorlevel 1 exit /b 1

rem 打印编好的程序路径
echo [OceanLive] Build finished: %EXE%
rem 成功退出
exit /b 0
