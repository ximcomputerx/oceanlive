// SPDX-License-Identifier: Apache-2.0
#include "platform/win32/DisplayManager.h" // 显示器列表
#include "render/VideoPlayer.h" // 整个进程只创建一个播放器
#include "services/AppLanguage.h" // 按设置安装语言
#include "services/SettingsManager.h" // 本地设置
#include "services/TrayController.h" // 托盘
#include "ui/IconFactory.h" // 窗口图标
#include "ui/MainWindow.h" // 主窗口
#include "utils/Logger.h" // 启动日志
#include "utils/SingleInstance.h" // 保证只有一个实例
#include "version.h" // 程序名、组织名和版本字符串
#include "wallpaper/WallpaperManager.h" // 壁纸生命周期

#include <QApplication> // 界面应用
#include <QFile> // 读取样式表
#include <QFont> // 默认字体
#include <QMetaType> // 注册渲染统计类型
#include <QSurfaceFormat> // 默认 OpenGL 格式
#include <QTimer> // 延迟恢复壁纸

#include "app/AppCommands.h" // 预览导出和两项自测

int main(int argc, char* argv[]) { // 程序入口
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL); // 壁纸走桌面 OpenGL
    QSurfaceFormat format; // 所有窗口共用的表面格式
    format.setVersion(3, 3); // 要求 OpenGL 3.3
    format.setProfile(QSurfaceFormat::CoreProfile); // 核心配置
    format.setDepthBufferSize(24); // 24 位深度
    format.setSwapInterval(1); // 打开垂直同步
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer); // 双缓冲
    format.setRenderableType(QSurfaceFormat::OpenGL); // 明确使用 OpenGL
    QSurfaceFormat::setDefaultFormat(format); // 必须在创建窗口之前设置

    QApplication application(argc, argv); // 界面对象，放在默认格式之后
    QApplication::setOrganizationName(QString::fromLatin1(kAppOrganization)); // 配置目录用的组织名
    QApplication::setApplicationName(QString::fromLatin1(kAppName)); // 程序名保持 OceanLive，启动项也用它
    QApplication::setApplicationVersion(QString::fromLatin1(kAppVersion)); // 版本沿用 version.h，不在这里改写
    QApplication::setQuitOnLastWindowClosed(false); // 关掉主窗口后托盘仍留着
    QApplication::setWindowIcon(IconFactory::appIcon()); // 应用图标
    QFont font(QStringLiteral("Microsoft YaHei UI"), 10); // 界面字体
    application.setFont(font); // 设为默认字体
    qRegisterMetaType<RenderStats>("RenderStats"); // 渲染统计可以排队跨线程

    QFile style(QStringLiteral(":/styles/dark.qss")); // 资源里的深色样式
    if (style.open(QIODevice::ReadOnly)) { // 打得开才套用
        application.setStyleSheet(QString::fromUtf8(style.readAll())); // 整份样式表
    } // 结束读取样式

    Logger::install(); // 安装文件日志
    Logger::info(QStringLiteral("App"), QStringLiteral("OceanLive %1 启动").arg(QString::fromLatin1(kAppVersion))); // 日志保持中文原文

    const QStringList arguments = application.arguments(); // 含程序名的命令行
    if (arguments.contains(QStringLiteral("--dump-preview"))) { // 只要预览图，不进正式界面
        return runDumpPreview(application, arguments); // dump-preview 在单实例和主界面之前返回
    } // 结束预览导出

    SingleInstance instance; // 单实例
    if (!instance.acquire()) { // 已经有一个 OceanLive 在跑
        return 0; // 第二个进程直接退出，不创建界面
    } // 结束抢占失败

    SettingsManager settings; // 读取本地设置
    AppLanguage::install(settings.current().language); // 按设置装翻译
    DisplayManager displays; // 枚举显示器
    VideoPlayer video; // 整个进程只有这一个视频播放器
    WallpaperManager wallpapers(&settings, &displays); // 壁纸管理器
    wallpapers.setVideoPlayer(&video); // 壁纸共用这个播放器
    MainWindow window(&settings, &wallpapers, &displays); // 主窗口
    window.setVideoPlayer(&video); // 主窗口也用同一个播放器
    TrayController tray(&window, &settings, &wallpapers); // 托盘菜单
    QObject::connect(&instance, &SingleInstance::raised, &window, &MainWindow::bringToFront); // 再次启动时把窗口带到前面
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &wallpapers, &WallpaperManager::stop); // 退出前卸下壁纸

    const bool selfTest = arguments.contains(QStringLiteral("--self-test")); // 桌面挂接自测
    const bool videoTest = arguments.contains(QStringLiteral("--video-desktop-test")); // 视频壁纸自测
    const bool minimized = settings.current().startMinimized || arguments.contains(QStringLiteral("--minimized")); // 设置或参数要求先不显示主窗口
    if (!minimized || selfTest) { // 自测需要主窗口存在并显示
        window.show(); // 显示主窗口
    } // 结束是否显示
    tray.show(); // 托盘始终出现

    if (selfTest) { // 自测跑完就退出
        return runSelfTest(application, window, wallpapers, arguments); // 不进入日常事件循环
    } // 结束桌面自测
    if (videoTest) { // 视频自测同样跑完即退出
        return runVideoDesktopTest(application, window, wallpapers, &settings, &video, arguments); // 传入那一个播放器
    } // 结束视频自测
    if (settings.current().resumeOnLaunch) { // 上次退出时期望壁纸在跑
        QTimer::singleShot(400, &wallpapers, &WallpaperManager::start); // 等事件循环起来再挂壁纸
    } // 结束启动恢复
    return application.exec(); // 正常消息循环
} // 结束入口
