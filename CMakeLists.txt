# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.21) # 至少需要 CMake 3.21
project(OceanLive VERSION 1.0.0 LANGUAGES CXX) # 工程版本 1.0.0，只编译 C++

# Qt 路径通过 CMAKE_PREFIX_PATH 或 Qt6_DIR 传入，不写死某一台机器的安装位置。
# 例如：cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.7.2/msvc2019_64

set(CMAKE_CXX_STANDARD 17) # 固定为 C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON) # 编译器达不到就失败
set(CMAKE_CXX_EXTENSIONS OFF) # 不使用编译器扩展
set(CMAKE_AUTOMOC ON) # 自动 moc
set(CMAKE_AUTOUIC ON) # 自动处理 ui
set(CMAKE_AUTORCC ON) # 自动编译 qrc
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui) # ui 文件目录

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network Multimedia MultimediaWidgets LinguistTools) # 界面、OpenGL、多媒体和翻译工具

set(OCEANLIVE_SOURCES # 主程序源文件
    src/main.cpp # 入口；dump-preview 在界面前返回，并只建一个播放器
    src/app/AppCommands.cpp # 预览导出和桌面自测
    src/utils/Logger.cpp # 日志
    src/utils/SingleInstance.cpp # 单实例
    src/utils/FullscreenHeuristic.cpp # 全屏应用判断
    src/render/OceanParams.cpp # 海面参数
    src/render/VideoPlayer.cpp # 唯一视频播放器的实现
    src/render/ShaderManager.cpp # 着色器加载
    src/app/VideoDesktopTest.cpp # 视频壁纸自测
    src/render/OceanSurface.cpp # 海面绘制
    src/render/OceanView.cpp # 海面视图
    src/platform/win32/WorkerWHost.cpp # 桌面宿主定位和挂接
    src/platform/win32/DisplayManager.cpp # 显示器枚举
    src/platform/win32/SystemMonitor.cpp # 全屏、电池和会话
    src/platform/win32/Autostart.cpp # 启动项，运行名保持 OceanLive
    src/platform/win32/DarkTitle.cpp # 深色标题栏
    src/wallpaper/WallpaperWindow.cpp # 嵌进桌面的窗口
    src/wallpaper/WallpaperManager.cpp # 壁纸启停和重新挂接
    src/services/AppLanguage.cpp # 语言目录
    src/services/SettingsManager.cpp # 设置读写
    src/services/TrayController.cpp # 托盘
    src/ui/IconFactory.cpp # 图标
    src/ui/MainWindow.cpp # 主窗口
    src/ui/SettingsDialog.cpp # 设置对话框
    src/ui/MainWindow.ui # 主窗口布局
    src/ui/SettingsDialog.ui # 设置对话框布局
    resources/resources.qrc # 样式表和着色器
) # 结束主程序源文件

add_executable(OceanLive WIN32 ${OCEANLIVE_SOURCES}) # 窗口程序，不带控制台
target_include_directories(OceanLive PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src) # 私有包含 src

get_filename_component(OCEANLIVE_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE) # 由 Qt6_DIR 回推前缀，不写死盘符
set(OCEANLIVE_QT_TRANSLATIONS "${OCEANLIVE_QT_PREFIX}/translations") # Qt 自带翻译所在目录
if(NOT EXISTS "${OCEANLIVE_QT_TRANSLATIONS}/qtbase_zh_CN.qm" OR NOT EXISTS "${OCEANLIVE_QT_TRANSLATIONS}/qtbase_en.qm") # 中英基础翻译必须都在
    message(FATAL_ERROR "Qt base translations were not found in ${OCEANLIVE_QT_TRANSLATIONS}") # 缺文件就停止配置
endif() # 结束翻译文件检查
qt_add_resources(OceanLive oceanlive_qtbase_i18n # 把 Qt 基础翻译打进资源
    PREFIX "/i18n" # 资源前缀保持 /i18n
    BASE "${OCEANLIVE_QT_TRANSLATIONS}" # 去掉磁盘目录，只留文件名
    FILES # 嵌入的 qm
        "${OCEANLIVE_QT_TRANSLATIONS}/qtbase_zh_CN.qm" # 中文基础翻译
        "${OCEANLIVE_QT_TRANSLATIONS}/qtbase_en.qm" # 英文基础翻译
) # 结束 Qt 基础翻译资源
qt_add_translations(OceanLive # 编译本程序自己的翻译
    SOURCE_TARGETS OceanLive # 从主目标收集文案
    TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/i18n/OceanLive_en.ts" # 英文翻译源文件
) # 结束程序翻译

target_link_libraries(OceanLive PRIVATE # 主程序私有依赖
    Qt6::Core # 核心
    Qt6::Gui # 图形与 OpenGL 上下文
    Qt6::Widgets # 控件
    Qt6::OpenGL # OpenGL 封装
    Qt6::OpenGLWidgets # OpenGL 窗口部件
    Qt6::Network # 网络模块
    Qt6::Multimedia # 视频播放
    Qt6::MultimediaWidgets # 视频控件，自测对照会用到
    opengl32 # 系统 OpenGL
    mfplat # 媒体基础库
    mfreadwrite # 自测里写测试视频
    mfuuid # MF 标识
    user32 # 窗口和消息
    gdi32 # 位图抓取
    shell32 # 外壳
    advapi32 # 注册表启动项
    shcore # 显示器缩放
    dwmapi # 窗口外观
    wtsapi32 # 会话锁定
    psapi # 进程内存
    ole32 # 组件对象模型
) # 结束链接库

target_compile_definitions(OceanLive PRIVATE NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00) # Unicode，并且以 Windows 10 为目标

if(MSVC) # 下面的选项只给 MSVC
    target_compile_options(OceanLive PRIVATE /W3 /utf-8 /MP) # 三级警告、UTF-8 源文件、并行编译
    target_link_options(OceanLive PRIVATE # 清单在链接时嵌入
        "/MANIFEST:EMBED" # 嵌入清单
        "/MANIFESTUAC:NO" # 不另生成 UAC 清单
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest" # 使用仓库里的清单
    ) # 结束清单选项
endif() # 结束主程序的 MSVC 分支

add_executable(OceanLiveDesktopProbe tools/probe_desktop.cpp) # 只打印桌面层级的探针
target_link_libraries(OceanLiveDesktopProbe PRIVATE user32) # 探针只需要 user32
if(MSVC) # 探针的编译选项
    target_compile_options(OceanLiveDesktopProbe PRIVATE /W3 /utf-8) # 警告和 UTF-8
    target_compile_definitions(OceanLiveDesktopProbe PRIVATE UNICODE _UNICODE) # 使用宽字符接口
endif() # 结束探针的 MSVC 分支

option(OCEANLIVE_BUILD_TESTS "Build OceanLive logic tests" ON) # 默认编译逻辑测试
if(OCEANLIVE_BUILD_TESTS) # 关闭该选项就不加入测试目标
    add_executable(oceanlive_tests # 不启动壁纸窗口的逻辑测试
        tests/test_logic.cpp # 测试入口
        src/utils/FullscreenHeuristic.cpp # 全屏判断
        src/utils/Logger.cpp # 日志
        src/render/OceanParams.cpp # 参数夹取和主题
        src/services/AppLanguage.cpp # 语言选择
        src/platform/win32/Autostart.cpp # 启动项读写
        src/platform/win32/DisplayManager.cpp # 显示器选择
    ) # 结束测试源文件
    target_include_directories(oceanlive_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src) # 测试包含 src
    target_link_libraries(oceanlive_tests PRIVATE Qt6::Core Qt6::Gui user32 gdi32 shcore advapi32) # 测试用到的库
    target_compile_definitions(oceanlive_tests PRIVATE NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00) # 与主程序相同的 Windows 宏
    if(MSVC) # 测试也按 UTF-8 编译
        target_compile_options(oceanlive_tests PRIVATE /W3 /utf-8) # 三级警告
    endif() # 结束测试的 MSVC 分支
endif() # 结束测试开关
